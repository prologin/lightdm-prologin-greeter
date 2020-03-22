#pragma once

#include <QLabel>
#include <QLightDM/Greeter>
#include <QLocalSocket>
#include <QStackedLayout>
#include <QTextStream>
#include <QWebChannelAbstractTransport>
#include <QWidget>

#include "KeyboardModel.h"

class QWebEngineView;
class QWebChannel;
class ProloJs;

namespace QLightDM {
class PowerInterface;
class SessionsModel;
}  // namespace QLightDM

enum class AuthState {
  IDLE,
  WAITING_FOR_PROMPT,
  WAITING_FOR_AUTHENTICATION_COMPLETE,
  WAITING_FOR_PROLOGIND
};

struct State {
  AuthState state = AuthState::IDLE;
  QString username;
  QString password;
  QString session;
  QString language;
};

struct Options {
  QString url;
  int fallback_delay = 4000;
  QString prologind_socket = "/run/prologind";
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

 signals:
  void PrologindSuccess();
  void PrologindError(const QString& reason);

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

  // prologind events.
  void OnPrologindSuccess();
  void OnPrologindError(const QString& reason);
  void OnPrologindSocketError(QLocalSocket::LocalSocketError);
  void OnPrologindSocketDisconnected();

  // For ProloJs (friend class).
  void StartLightDmAuthentication(const QString& username,
                                  const QString& password,
                                  const QString& session);
  void SetLanguage(const QString& language);
  void PowerOff();
  void Reboot();

 private:
  QList<XSession> AvailableSessions() const;

  // Does blocking I/O. Run this in a thread.
  void SetupWithPrologind();
  void SendPrologindCleanup();

  State state_;
  Options options_;
  bool webview_load_success_ = false;
  bool webview_uses_fallback_ = false;

  // The communication channel with prologind.
  QLocalSocket* prolo_sock_;

  // The UI elements.
  QStackedLayout* layout_;
  QLabel* status_info_;
  QWebEngineView* webview_;

  // The communication channel to JavaScript world.
  QWebChannel* channel_;
  ProloJs* prolojs_;

  // The communication channel with LightDM, power and session APIs.
  QLightDM::Greeter* lightdm_;
  QLightDM::PowerInterface* lightdm_power_;
  QLightDM::SessionsModel* lightdm_sessions_;

  // The KeyboardModel to watch for capslock & numlock & layout changes, and
  // update layout.
  KeyboardModel* keyboard_;

  friend class ProloJs;
};

class ProloJs : public QObject {
  Q_OBJECT
 public:
  explicit ProloJs(ProloGreet* prolo);

 signals:
  // Signal sent to JS on messages from prologind.
  void OnStatusMessage(const QString& message);
  // Signal sent to JS when login was successful; LightDM will very soon start
  // the chosen session.
  void OnLoginSuccess();
  // Signal sent to JS when login failed somehow, eg. wrong credential or error
  // from prologind.
  void OnLoginError(const QString& reason);
  // Signal sent to JS when CapsLock state changes.
  void OnCapsLockChange(bool enabled);
  // Signal sent to JS when NumLock state changes.
  void OnNumLockChange(bool enabled);
  // Signal sent to JS when active keyboard layout changes.
  void OnKeyboardLayoutChange(int id);
  // Signal sent to JS when available keyboard layouts change. Retrieve layouts
  // with keyboardLayouts().
  void OnKeyboardLayoutsChange();

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

 private:
  ProloGreet* prolo_{};  // Not owned.
};
