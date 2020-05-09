#include "ProloGreet.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLightDM/Power>
#include <QLightDM/SessionsModel>
#include <QScreen>
#include <QWebChannel>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QtConcurrent/QtConcurrent>

// Protocol:
// greeter                       lightdm                      companion
//   authenticate(username) --->
//                          <---  prompt()
//   respond(password)      --->
//                          <---  authenticationComplete()
// (password confirmed correct now)
//   setup                  ------------------------------->
//                          <-------------------------------   message ...
//                          <-------------------------------   success
//   startSession(kde)      --->

namespace {

void SetWebviewOptions(QWebEngineView* view) {
  view->setContextMenuPolicy(Qt::NoContextMenu);
  using S = QWebEngineSettings;
  auto settings = view->settings();
  settings->setAttribute(S::FocusOnNavigationEnabled, true);
  settings->setAttribute(S::FullScreenSupportEnabled, true);
  settings->setAttribute(S::JavascriptEnabled, true);
  settings->setAttribute(S::LocalContentCanAccessFileUrls, true);
  settings->setAttribute(S::LocalContentCanAccessRemoteUrls, false);
}

class AutoCancellableTimer {
 public:
  AutoCancellableTimer(QObject* parent, int msec, std::function<void()> cb)
      : timer_(new QTimer(parent)) {
    QObject::connect(timer_, &QTimer::timeout, std::move(cb));
    timer_->setSingleShot(true);
    timer_->start(msec);
  }

  ~AutoCancellableTimer() {
    timer_->stop();
    timer_->deleteLater();
  }

 private:
  QTimer* timer_;
};

}  // namespace

ProloGreet::ProloGreet(Options options, QWidget* parent)
    : state_(), options_(std::move(options)), QWidget(parent) {
  webview_ = new QWebEngineView(this);
  SetWebviewOptions(webview_);
  connect(webview_, &QWebEngineView::loadFinished, this,
          &ProloGreet::OnWebviewLoadFinish);

  status_info_ = new QLabel("Prologin greeter is starting up…", this);
  status_info_->setAlignment(Qt::AlignCenter);

  layout_ = new QStackedLayout;
  layout_->setMargin(0);
  layout_->addWidget(status_info_);
  layout_->addWidget(webview_);
  layout_->setCurrentWidget(status_info_);
  setLayout(layout_);
  QRect screenRect = QGuiApplication::primaryScreen()->geometry();
  setGeometry(screenRect);

  connect(this, &ProloGreet::CompanionSuccess, this,
          &ProloGreet::OnCompanionSuccess);
  connect(this, &ProloGreet::CompanionError, this,
          &ProloGreet::OnCompanionError);

  companion_sock_ = new QLocalSocket(this);
  connect(companion_sock_, &QLocalSocket::disconnected, this,
          &ProloGreet::OnCompanionSocketDisconnected);
  connect(companion_sock_,
          QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
          this, &ProloGreet::OnCompanionSocketError);

  prolojs_ = new ProloJs(this);

  // Establish a communication channel between us and the webview JS context.
  channel_ = new QWebChannel();
  channel_->registerObject("prologin", prolojs_);
  webview_->page()->setWebChannel(channel_);

  // LightDM APIs.
  lightdm_ = new QLightDM::Greeter(this);
  // We want to die, not be reset.
  lightdm_->setResettable(false);
  lightdm_power_ = new QLightDM::PowerInterface(this);
  lightdm_sessions_ = new QLightDM::SessionsModel(
      QLightDM::SessionsModel::SessionType::LocalSessions, this);

  connect(lightdm_, &QLightDM::Greeter::idle, this, &QApplication::quit);
  connect(lightdm_, &QLightDM::Greeter::showMessage, this,
          &ProloGreet::OnLightDMMessage);
  connect(lightdm_, &QLightDM::Greeter::showPrompt, this,
          &ProloGreet::OnLightDMPrompt);
  connect(lightdm_, &QLightDM::Greeter::authenticationComplete, this,
          &ProloGreet::OnLightDMAuthenticationComplete);

  keyboard_ = new KeyboardModel(this);
  connect(keyboard_, &KeyboardModel::currentLayoutChanged,
          [this](int id) { prolojs_->OnKeyboardLayoutChange(id); });
  connect(keyboard_, &KeyboardModel::layoutsChanged,
          [this]() { prolojs_->OnKeyboardLayoutsChange(); });
  connect(keyboard_, &KeyboardModel::capsLockStateChanged,
          [this](bool enabled) { prolojs_->OnCapsLockChange(enabled); });
  connect(keyboard_, &KeyboardModel::numLockStateChanged,
          [this](bool enabled) { prolojs_->OnNumLockChange(enabled); });
  keyboard_->initialize();
}

bool ProloGreet::Start() {
  // Connect to LightDM.
  status_info_->setText("Connecting to LightDM…");
  if (!lightdm_->connectSync()) {
    status_info_->setText("Could not connect to LightDM.");
    return false;
  }

  // Connect to the companion and ensure it's working.
  companion_sock_->connectToServer(options_.companion_socket);
  if (!companion_sock_->waitForConnected(2 * 1000)) {
    status_info_->setText(QString("Could not connect to companion socket: %s")
                              .arg(options_.companion_socket));
    return false;
  }
  companion_sock_->write("ping\n");
  companion_sock_->flush();
  if (!companion_sock_->waitForReadyRead(5 * 1000) ||
      companion_sock_->readLine().trimmed().toStdString() != "pong") {
    status_info_->setText("Did not receive 'pong' from companion.");
    return false;
  }

  // Load the requested URL. Fallback to internal log-in screen after some time.
  webview_uses_fallback_ = false;
  webview_->load(options_.url);
  layout_->setCurrentWidget(webview_);
  QTimer::singleShot(options_.fallback_delay,
                     [this]() { MaybeFallbackToInternalGreeter(); });
  return true;
}

void ProloGreet::StartLightDmAuthentication(const QString& username,
                                            const QString& password,
                                            const QString& session) {
  if (state_.state != AuthState::IDLE) {
    qWarning() << "illegal state in" << __FUNCTION__;
    return;
  }
  state_.username = username;
  state_.password = password;
  state_.session = session;
  state_.state = AuthState::WAITING_FOR_PROMPT;
  lightdm_->authenticate(username);
}

void ProloGreet::OnLightDMMessage(const QString& message,
                                  QLightDM::Greeter::MessageType type) {
  qDebug() << "received LightDM message" << type << message;
}

void ProloGreet::OnLightDMPrompt(const QString& prompt,
                                 QLightDM::Greeter::PromptType type) {
  if (state_.state != AuthState::WAITING_FOR_PROMPT) {
    qWarning() << "illegal state in" << __FUNCTION__;
    return;
  }
  if (type != QLightDM::Greeter::PromptType::PromptTypeSecret) {
    qWarning() << "unexpected prompt";
    return;
  }
  state_.state = AuthState::WAITING_FOR_AUTHENTICATION_COMPLETE;
  lightdm_->respond(state_.password);
}

void ProloGreet::OnLightDMAuthenticationComplete() {
  if (state_.state != AuthState::WAITING_FOR_AUTHENTICATION_COMPLETE) {
    qWarning() << "illegal state in" << __FUNCTION__;
    return;
  }

  if (lightdm_->authenticationUser() != state_.username) {
    qWarning() << "LightDM user is not the user we're authenticating.";
    return;
  }

  if (!lightdm_->isAuthenticated()) {
    lightdm_->cancelAuthentication();
    state_.state = AuthState::IDLE;
    emit prolojs_->OnLoginError("incorrect password");
    return;
  }

  state_.state = AuthState::WAITING_FOR_COMPANION;
  emit prolojs_->OnStatusMessage("authenticated! setting up…");
  QtConcurrent::run(this, &ProloGreet::SetupWithCompanion);
}

void ProloGreet::SetupWithCompanion() {
  if (state_.state != AuthState::WAITING_FOR_COMPANION) {
    qWarning() << "illegal state in" << __FUNCTION__;
    return;
  }
  companion_sock_->write(QString("setup %1\n").arg(state_.username).toUtf8());
  companion_sock_->flush();
  AutoCancellableTimer timeout(this, 30 * 1000, [this]() {
    qWarning() << "SetupWithCompanion: timeout reading from companion";
    QApplication::exit(1);
  });
  while (true) {
    // waitForReadyRead() does not work as expected, somehow. Apparently Qt
    // doesn't know how to I/O.
    while (!companion_sock_->canReadLine()) QThread::msleep(250);
    const auto& ba = companion_sock_->readLine();
    if (ba.isEmpty()) {
      qWarning() << __FUNCTION__ << "read empty line, returning";
      return;
    }
    const auto& response = QString::fromUtf8(ba).trimmed();
    qDebug() << "companion response" << response;
    const auto& command = response.section(' ', 0, 1);
    const auto& payload = response.section(' ', 1);
    if (command == "message") {
      emit prolojs_->OnStatusMessage(payload);
    } else if (command == "error") {
      emit prolojs_->OnLoginError(payload);
      emit CompanionError(payload);
      return;
    } else if (command == "success") {
      emit prolojs_->OnLoginSuccess();
      emit CompanionSuccess();
      return;
    }
  }
}

void ProloGreet::OnCompanionSuccess() {
  if (state_.state != AuthState::WAITING_FOR_COMPANION) {
    qWarning() << "illegal state in" << __FUNCTION__;
    return;
  }
  qDebug() << __FUNCTION__;
  if (!lightdm_->startSessionSync(state_.session)) {
    SendCompanionCleanup();
    qWarning() << "LightDM startSession() returned false";
  }
  state_.username.clear();
  state_.password.clear();
  state_.session.clear();
  state_.state = AuthState::IDLE;
}

void ProloGreet::OnCompanionError(const QString& reason) {
  if (state_.state != AuthState::WAITING_FOR_COMPANION) {
    qWarning() << "illegal state in" << __FUNCTION__;
    return;
  }
  lightdm_->cancelAuthentication();
  state_.username.clear();
  state_.password.clear();
  state_.session.clear();
  state_.state = AuthState::IDLE;
}

void ProloGreet::OnWebviewLoadFinish(bool ok) {
  webview_load_success_ = ok;
  if (!ok) MaybeFallbackToInternalGreeter();
}

void ProloGreet::MaybeFallbackToInternalGreeter() {
  if (webview_load_success_) return;
  if (webview_uses_fallback_) {
    qWarning() << "could not load fallback internal greeter";
    return;
  }
  qWarning()
      << "could not load requested url; falling back to internal greeter";
  webview_->load(QUrl("qrc:/fallback/login.html"));
}

void ProloGreet::SendCompanionCleanup() {
  QString username = state_.username;
  if (username.isEmpty()) {
    qWarning() << "attempted to send companion cleanup on an empty username";
    return;
  }
  companion_sock_->write(QString("cleanup %1\n").arg(username).toUtf8());
  companion_sock_->flush();
}

void ProloGreet::SetLanguage(const QString& language) {
  lightdm_->setLanguage(language);
}

void ProloGreet::PowerOff() { lightdm_power_->shutdown(); }

void ProloGreet::Reboot() { lightdm_power_->restart(); }

QList<XSession> ProloGreet::AvailableSessions() const {
  auto* s = lightdm_sessions_;
  const int count = s->rowCount(QModelIndex());
  QList<XSession> sessions;
  sessions.reserve(count);
  for (int row = 0; row < count; row++) {
    const auto& index = s->index(row, 0);
    sessions.append({
        .id = s->data(index, QLightDM::SessionsModel::KeyRole).toString(),
        .name = s->data(index, Qt::DisplayRole).toString(),
        .description = s->data(index, Qt::ToolTipRole).toString(),
    });
  }
  return sessions;
}

void ProloGreet::OnCompanionSocketError() {
  qWarning() << __FUNCTION__ << companion_sock_->errorString();
}

void ProloGreet::OnCompanionSocketDisconnected() { qWarning() << __FUNCTION__; }

ProloJs::ProloJs(ProloGreet* prolo) : QObject(), prolo_(prolo) {}

QVariant ProloJs::AvailableSessions() {
  QVariantList list;
  for (const auto& s : prolo_->AvailableSessions()) {
    QVariantMap map;
    map.insert("id", s.id);
    map.insert("name", s.name);
    map.insert("description", s.description);
    list.append(map);
  }
  return QVariant::fromValue(list);
}

QVariant ProloJs::KeyboardLayouts() {
  QVariantList list;
  for (const auto* layout : prolo_->keyboard_->layouts()) {
    QVariantMap map;
    map.insert("short", layout->shortName());
    map.insert("long", layout->longName());
    list.append(map);
  }
  return QVariant::fromValue(list);
}

void ProloJs::SetKeyboardLayout(int id) { prolo_->keyboard_->setLayout(id); }

void ProloJs::Authenticate(const QString& username, const QString& password,
                           const QString& session) {
  prolo_->StartLightDmAuthentication(username, password, session);
}

void ProloJs::SetLanguage(const QString& language) {
  prolo_->SetLanguage(language);
}

void ProloJs::PowerOff() { prolo_->PowerOff(); }

void ProloJs::Reboot() { prolo_->Reboot(); }
