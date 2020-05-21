#pragma once

#include <QLabel>
#include <QLightDM/Greeter>
#include <QStackedLayout>
#include <QWidget>

#include "KeyboardModel.h"

class QWebEngineView;
class QWebChannel;
class GreetJS;

namespace QLightDM {
class PowerInterface;
class SessionsModel;
}  // namespace QLightDM

enum class AuthState {
  IDLE,
  WAITING_FOR_PROMPT,
  WAITING_FOR_AUTHENTICATION_COMPLETE,
};

struct State {
  AuthState state = AuthState::IDLE;
  QString username;
  QString password;
  QString session;
  QString language;
  bool got_business_logic_error = false;
};

constexpr char kFallbackUrl[] = "qrc:/fallback/login.html";

struct Options {
  QString url = kFallbackUrl;
  int fallback_delay = 2000;
  QColor background_color = Qt::black;
};

struct XSession {
  QString id, name, description;
};

class ProloGreet : public QWidget {
  Q_OBJECT

 public:
  explicit ProloGreet(Options options, QWidget* parent = nullptr);
  ~ProloGreet() override = default;

  bool Start() __attribute__((warn_unused_result));

 private slots:
  // Internal webview events.
  void OnWebviewLoadFinish(bool ok);
  void MaybeFallbackToInternalGreeter();

  // LightDM events.
  void OnLightDMMessage(const QString& message,
                        QLightDM::Greeter::MessageType type);
  void OnLightDMPrompt(const QString& prompt,
                       QLightDM::Greeter::PromptType type);
  void OnLightDMAuthenticationComplete();

  // For ProloJs (friend class).
  void StartLightDmAuthentication(const QString& username,
                                  const QString& password,
                                  const QString& session);
  void SetLanguage(const QString& language);
  void PowerOff();
  void Reboot();

 private:
  QList<XSession> AvailableSessions() const;

  State state_;
  Options options_;
  bool webview_load_success_ = false;
  bool webview_uses_fallback_ = false;

  // The UI elements.
  QStackedLayout* layout_;
  QLabel* status_info_;
  QWebEngineView* webview_;

  // The communication channel to JavaScript world.
  QWebChannel* channel_;
  GreetJS* js_;

  // The communication channel with LightDM, power and session APIs.
  QLightDM::Greeter* lightdm_;
  QLightDM::PowerInterface* lightdm_power_;
  QLightDM::SessionsModel* lightdm_sessions_;

  // The KeyboardModel to watch for capslock & numlock & layout changes, and
  // update layout.
  KeyboardModel* keyboard_;

  friend class GreetJS;
};

class GreetJS : public QObject {
  Q_OBJECT
 public:
  explicit GreetJS(ProloGreet* prolo);

 signals:
  // Signal sent to JS on LightDM (typically from PAM) messages.
  void OnStatusMessage(const QString& message);
  // Signal sent to JS when login was successful; LightDM will very soon start
  // the chosen session.
  void OnLoginSuccess();
  // Signal sent to JS when login failed somehow, eg. wrong credential or error
  // from PAM.
  void OnLoginError(const QString& reason);
  // Signal sent to JS when CapsLock state changes.
  void OnCapsLockChange(bool enabled);
  // Signal sent to JS when NumLock state changes.
  void OnNumLockChange(bool enabled);
  // Signal sent to JS when active keyboard layout changes.
  void OnKeyboardLayoutChange(int id);
  // Signal sent to JS when available keyboard layouts change. Retrieve layouts
  // with KeyboardLayouts().
  void OnKeyboardLayoutsChange();

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedGlobalDeclarationInspection"
 public slots:
  // Invoked through JS to retrieve available sessions (xsessions).
  // Returns a list of {id: "id", name: "Name", description: "..."}
  Q_INVOKABLE QVariant AvailableSessions();

  // Invoked through JS to retrieve available keyboard layouts.
  // Returns a list of {short: "short layout name", long: "long layout name"}
  Q_INVOKABLE QVariant KeyboardLayouts();

  // Invoked through JS to change the current layout.
  // Will emit OnKeyboardLayoutChange() if successful.
  Q_INVOKABLE void SetKeyboardLayout(int id);

  // Invoked through JS to start the authentication process.
  Q_INVOKABLE void Authenticate(const QString& username,
                                const QString& password,
                                const QString& session);

  // Invoked through JS to change the language.
  Q_INVOKABLE void SetLanguage(const QString& language);

  // Invoked through JS to power off.
  Q_INVOKABLE void PowerOff();

  // Invoked through JS to reboot.
  Q_INVOKABLE void Reboot();
#pragma clang diagnostic pop

 private:
  ProloGreet* prolo_{};  // Not owned.
};
