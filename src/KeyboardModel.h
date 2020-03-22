#pragma once

#define explicit explicit_is_keyword_in_cpp
#include <xcb/xcb.h>
#undef explicit

#include <QObject>
#include <QSocketNotifier>

struct KeyboardLayout : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString shortName READ shortName CONSTANT);
  Q_PROPERTY(QString longName READ longName CONSTANT);

 public:
  explicit KeyboardLayout(QObject* parent = nullptr) : QObject(parent) {}
  QString shortName() const { return short_; }
  QString longName() const { return long_; }

 private:
  KeyboardLayout(QString short_name, QString long_name);
  const QString short_, long_;

  friend class KeyboardModel;
};

class KeyboardModel : public QObject {
  Q_OBJECT
 public:
  explicit KeyboardModel(QObject* parent = nullptr);
  QList<KeyboardLayout*> layouts() const;

 signals:
  void numLockStateChanged(bool);
  void capsLockStateChanged(bool);
  void layoutsChanged();
  void currentLayoutChanged(int id);

 public slots:
  void initialize();
  void disconnect();
  void setLayout(int id);

 private slots:
  void onXcbEvent();
  bool getKeyboardLayouts();

 private:
  bool working_ = false;

  xcb_connection_t* xcb_ = nullptr;
  // Socket to watch for xkb changes.
  QSocketNotifier* socket_ = nullptr;

  struct Indicator {
    bool enabled = false;
    uint8_t mask = 0;
  } numlock_, capslock_;
  int current_layout_ = 0;
  QList<KeyboardLayout*> layouts_;
};
