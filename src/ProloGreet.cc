#include "ProloGreet.h"

#include <QApplication>
#include <QGuiApplication>
#include <QLightDM/Power>
#include <QLightDM/SessionsModel>
#include <QScreen>
#include <QStackedLayout>
#include <QTimer>
#include <QWebChannel>
#include <QWebEngineSettings>
#include <QWebEngineView>

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

  js_ = new GreetJS(this);

  // Establish a communication channel between us and the webview JS context.
  channel_ = new QWebChannel();
  channel_->registerObject("prologin", js_);
  webview_->page()->setWebChannel(channel_);

  // LightDM APIs.
  lightdm_ = new QLightDM::Greeter(this);
  // We want to die, not be reset. It's easier to not screw up state that way.
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
          [this](int id) { js_->OnKeyboardLayoutChange(id); });
  connect(keyboard_, &KeyboardModel::layoutsChanged,
          [this]() { js_->OnKeyboardLayoutsChange(); });
  connect(keyboard_, &KeyboardModel::capsLockStateChanged,
          [this](bool enabled) { js_->OnCapsLockChange(enabled); });
  connect(keyboard_, &KeyboardModel::numLockStateChanged,
          [this](bool enabled) { js_->OnNumLockChange(enabled); });
  keyboard_->initialize();
}

bool ProloGreet::Start() {
  // Connect to LightDM.
  status_info_->setText("Connecting to LightDM…");
  if (!lightdm_->connectSync()) {
    status_info_->setText("Could not connect to LightDM.");
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
    emit js_->OnLoginError("incorrect password");
    return;
  }

  qDebug() << "Authentication complete";
  emit js_->OnLoginSuccess();
  state_.username.clear();
  state_.password.clear();
  state_.session.clear();
  state_.state = AuthState::IDLE;

  if (!lightdm_->startSessionSync(state_.session)) {
    lightdm_->cancelAuthentication();
    qWarning() << "LightDM startSession() returned false";
  }
  qDebug() << "Authentication complete, starting session (I will die soon)";
  close();
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
  webview_->load(QUrl(kFallbackUrl));
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

GreetJS::GreetJS(ProloGreet* prolo) : QObject(), prolo_(prolo) {}

QVariant GreetJS::AvailableSessions() {
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

QVariant GreetJS::KeyboardLayouts() {
  QVariantList list;
  for (const auto* layout : prolo_->keyboard_->layouts()) {
    QVariantMap map;
    map.insert("short", layout->shortName());
    map.insert("long", layout->longName());
    list.append(map);
  }
  return QVariant::fromValue(list);
}

void GreetJS::SetKeyboardLayout(int id) { prolo_->keyboard_->setLayout(id); }

void GreetJS::Authenticate(const QString& username, const QString& password,
                           const QString& session) {
  prolo_->StartLightDmAuthentication(username, password, session);
}

void GreetJS::SetLanguage(const QString& language) {
  prolo_->SetLanguage(language);
}

void GreetJS::PowerOff() { prolo_->PowerOff(); }

void GreetJS::Reboot() { prolo_->Reboot(); }
