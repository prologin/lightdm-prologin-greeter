#include "ProloGreet.h"

#include <QApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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

QColor InverseColor(const QColor& color) {
  qreal r, g, b;
  color.getRgbF(&r, &g, &b, nullptr);
  return QColor::fromRgbF(1 - r, 1 - g, 1 - b);
}

}  // namespace

ProloGreet::ProloGreet(Options options, QWidget* parent)
    : state_(), options_(std::move(options)), QWidget(parent) {
  {
    auto pal = palette();
    pal.setColor(QPalette::Window, options.background_color);
    pal.setColor(QPalette::WindowText, InverseColor(options.background_color));
    setAutoFillBackground(true);
    setPalette(pal);
  }

  webview_ = new QWebEngineView(this);
  SetWebviewOptions(webview_);
  {
    auto pal = webview_->palette();
    pal.setColor(QPalette::Window, options.background_color);
    webview_->setAutoFillBackground(true);
    webview_->setPalette(pal);
    webview_->page()->setBackgroundColor(options.background_color);
  }
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
  QTimer::singleShot(options_.fallback_delay,
                     [this]() { MaybeFallbackToInternalGreeter(); });
  return true;
}

void ProloGreet::StartLightDmAuthentication(const QString& username,
                                            const QString& password,
                                            const QString& session) {
  qDebug() << "starting LightDM authentication flow";
  if (state_.state != AuthState::IDLE) {
    qWarning() << "illegal state in" << __FUNCTION__ << (int)state_.state;
    return;
  }
  state_.username = username;
  state_.password = password;
  state_.session = session;
  state_.got_business_logic_error = false;
  state_.state = AuthState::WAITING_FOR_PROMPT;
  lightdm_->authenticate(username);
}

void ProloGreet::OnLightDMMessage(const QString& payload,
                                  QLightDM::Greeter::MessageType type) {
  // We only support JSON-encoded messages (not generic PAM errors that are too
  // noisy & inscrutable).
  qDebug() << "received LightDM message, type" << type << ":" << payload;
  if (type != QLightDM::Greeter::MessageTypeInfo) return;
  QJsonParseError error;
  const auto json = QJsonDocument::fromJson(payload.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError) return;
  if (!json.isObject()) return;
  const auto message = json.object().value("message").toString();
  if (message.isNull()) return;
  const auto isError = json.object().value("isError").toBool();
  state_.got_business_logic_error |= isError;
  js_->OnStatusMessage(message, isError);
}

void ProloGreet::OnLightDMPrompt(const QString& prompt,
                                 QLightDM::Greeter::PromptType type) {
  qDebug() << "received prompt from LightDM, type" << type << ":" << prompt;
  if (state_.state != AuthState::WAITING_FOR_PROMPT) {
    qWarning() << "illegal state in" << __FUNCTION__ << (int)state_.state;
    return;
  }
  if (type != QLightDM::Greeter::PromptType::PromptTypeSecret) {
    qWarning() << "unexpected prompt; we only support SECRET, for the password";
    return;
  }
  qDebug() << "replying to LightDM 'secret' prompt with user password";
  state_.state = AuthState::WAITING_FOR_AUTHENTICATION_COMPLETE;
  lightdm_->respond(state_.password);
}

void ProloGreet::OnLightDMAuthenticationComplete() {
  qDebug() << "LightDM authentication complete";
  if (state_.state != AuthState::WAITING_FOR_AUTHENTICATION_COMPLETE) {
    qWarning() << "illegal state in" << __FUNCTION__ << (int)state_.state;
    return;
  }

  if (lightdm_->authenticationUser() != state_.username) {
    qWarning() << "LightDM user is not the user we're authenticating";
    return;
  }

  if (!lightdm_->isAuthenticated()) {
    qDebug() << __FUNCTION__ << "but not authenticated; an error happened";
    lightdm_->cancelAuthentication();
    state_.state = AuthState::IDLE;
    // LightDM doesn't give us a way to distinguish between PAM failures. A bad
    // password would typically be a PAM 'auth' stage failure, but if some
    // 'account' stage returns non-zero, it appears the same to us:
    // isAuthenticated() = false. We don't want to say "incorrect password" for
    // "business logic" errors (the JSON-encoded ones in OnLightDMMessage). So
    // when we receive such a JSON "business logic" error, we set
    // got_business_logic_error to true. If it's false when we check it here,
    // this must be an early stage PAM error, so we guess that's a bad password.
    // Otherwise, we don't send anything so the last "business logic" error
    // remains the one displayed to the user.
    if (state_.got_business_logic_error) {
      qDebug() << "sending unknown login error";
      emit js_->OnLoginError("");
    } else {
      qDebug() << "sending 'incorrect password' since we don't know better";
      emit js_->OnLoginError("incorrect password");
    }
    return;
  }

  qDebug() << "authentication successful, starting session" << state_.session;
  emit js_->OnLoginSuccess();

  if (!lightdm_->startSessionSync(state_.session)) {
    lightdm_->cancelAuthentication();
    qWarning() << "LightDM startSession() returned false; I don't know how to "
                  "handle that and will die now.";
  } else {
    // This code and below will likely not have time to execute, since LightDM
    // kills us right after startSessionSync().
    qDebug() << "LightDM is now taking over. Bye!";
  }

  // Reset everything just in case.
  state_.username.clear();
  state_.password.clear();
  state_.session.clear();
  state_.state = AuthState::IDLE;
  // Self-quit.
  close();
}

void ProloGreet::OnWebviewLoadFinish(bool ok) {
  webview_load_success_ = ok;
  if (!ok) {
    MaybeFallbackToInternalGreeter();
  } else {
    // Finally reveal the webview. Prevents flashes of default background color.
    layout_->setCurrentWidget(webview_);
  }
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
