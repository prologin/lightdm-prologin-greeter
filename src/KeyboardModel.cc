#include "KeyboardModel.h"

#define explicit explicit_is_keyword_in_cpp
#include <xcb/xkb.h>
#undef explicit

#include <QDebug>

namespace {

// Retrieves xkb human-friendly atom name for 'cookie'.
QString atomName(xcb_connection_t* xcb, xcb_get_atom_name_cookie_t cookie) {
  // Get atom name
  xcb_generic_error_t* error = nullptr;
  xcb_get_atom_name_reply_t* reply =
      xcb_get_atom_name_reply(xcb, cookie, &error);
  QString res;
  if (reply) {
    QByteArray replyText(xcb_get_atom_name_name(reply),
                         xcb_get_atom_name_name_length(reply));
    res = QString::fromLocal8Bit(replyText);
    free(reply);
  } else {
    qWarning() << "Failed to get atom name: " << error->error_code;
  }
  return res;
}

// Retrieves the xkb indicator mask for bit 'i'.
uint8_t indicatorMask(xcb_connection_t* xcb, uint8_t i) {
  auto cookie =
      xcb_xkb_get_indicator_map(xcb, XCB_XKB_ID_USE_CORE_KBD, 1u << i);
  xcb_generic_error_t* error = nullptr;
  auto* reply = xcb_xkb_get_indicator_map_reply(xcb, cookie, &error);
  uint8_t mask = 0;
  if (reply) {
    auto* map = xcb_xkb_get_indicator_map_maps(reply);
    mask = map->mods;
    free(reply);
  } else {
    qWarning() << "Can't get indicator mask " << error->error_code;
  }
  return mask;
}

// Parse layout short name.
QStringList parseShortNames(const QString& text) {
  QRegExp re(R"(\+([a-z]+))");
  re.setCaseSensitivity(Qt::CaseInsensitive);
  QStringList res;
  int pos = 0;
  while ((pos = re.indexIn(text, pos)) != -1) {
    pos += re.matchedLength();
    if (re.cap(1) == "inet" || re.cap(1) == "group") continue;
    res << re.cap(1);
  }
  return res;
}

}  // namespace

KeyboardModel::KeyboardModel(QObject* parent) : QObject(parent) {}

void KeyboardModel::initialize() {
  xcb_ = xcb_connect(nullptr, nullptr);
  if (xcb_ == nullptr) {
    qCritical() << "xcb_connect failed";
    return;
  }

  // Initialize xkb extension.
  {
    auto cookie = xcb_xkb_use_extension(xcb_, XCB_XKB_MAJOR_VERSION,
                                        XCB_XKB_MINOR_VERSION);
    xcb_generic_error_t* error = nullptr;
    xcb_xkb_use_extension_reply(xcb_, cookie, &error);
    if (error) {
      qCritical() << "xcb_xkb_use_extension failed:" << error->error_code;
      return;
    }
  }

  // Get masks for caps lock and num lock.
  {
    auto cookie = xcb_xkb_get_names(xcb_, XCB_XKB_ID_USE_CORE_KBD,
                                    XCB_XKB_NAME_DETAIL_INDICATOR_NAMES);
    xcb_generic_error_t* error = nullptr;
    auto* reply = xcb_xkb_get_names_reply(xcb_, cookie, &error);
    if (error) {
      qCritical() << "can't init indicator map:" << error->error_code;
      return;
    }
    xcb_xkb_get_names_value_list_t list;
    const void* buffer = xcb_xkb_get_names_value_list(reply);
    xcb_xkb_get_names_value_list_unpack(
        buffer, reply->nTypes, reply->indicators, reply->virtualMods,
        reply->groupNames, reply->nKeys, reply->nKeyAliases,
        reply->nRadioGroups, reply->which, &list);
    int ind_cnt =
        xcb_xkb_get_names_value_list_indicator_names_length(reply, &list);

    QList<xcb_get_atom_name_cookie_t> cookies;
    for (int i = 0; i < ind_cnt; i++) {
      cookies << xcb_get_atom_name(xcb_, list.indicatorNames[i]);
    }

    for (int i = 0; i < ind_cnt; i++) {
      QString name = atomName(xcb_, cookies[i]);
      if (name == QLatin1String("Num Lock")) {
        numlock_.mask = indicatorMask(xcb_, i);
      } else if (name == QLatin1String("Caps Lock")) {
        capslock_.mask = indicatorMask(xcb_, i);
      }
    }
    free(reply);
  }

  if (!getKeyboardLayouts()) {
    return;
  }

  // Get indicator state.
  {
    auto cookie = xcb_xkb_get_state(xcb_, XCB_XKB_ID_USE_CORE_KBD);
    xcb_generic_error_t* error = nullptr;
    auto* reply = xcb_xkb_get_state_reply(xcb_, cookie, &error);
    if (error) {
      qCritical() << "cannot load indicator state:" << error->error_code;
      return;
    }
    capslock_.enabled = reply->lockedMods & capslock_.mask;
    numlock_.enabled = reply->lockedMods & numlock_.mask;
    current_layout_ = reply->group;
    emit capsLockStateChanged(capslock_.enabled);
    emit numLockStateChanged(numlock_.enabled);
    emit currentLayoutChanged(current_layout_);
    free(reply);
  }

  // Watch changes.
  {
    xcb_xkb_select_events_details_t unused;
    auto cookie =
        xcb_xkb_select_events(xcb_, XCB_XKB_ID_USE_CORE_KBD,
                              XCB_XKB_EVENT_TYPE_STATE_NOTIFY |
                                  XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY,
                              0,
                              XCB_XKB_EVENT_TYPE_STATE_NOTIFY |
                                  XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY,
                              0, 0, &unused);
    auto* error = xcb_request_check(xcb_, cookie);
    if (error) {
      qCritical() << "cannot select xck-xkb events:" << error->error_code;
      return;
    }
    xcb_flush(xcb_);
    int fd = xcb_get_file_descriptor(xcb_);
    socket_ = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(socket_, &QSocketNotifier::activated, this,
            &KeyboardModel::onXcbEvent);
  }

  // All is well, mark as working.
  working_ = true;
}

void KeyboardModel::disconnect() {
  if (socket_) socket_->deleteLater();
  if (xcb_) xcb_disconnect(xcb_);
  socket_ = nullptr;
  xcb_ = nullptr;
}

void KeyboardModel::onXcbEvent() {
  while (xcb_generic_event_t* event = xcb_poll_for_event(xcb_)) {
    if (event->response_type != 0 && event->pad0 == XCB_XKB_STATE_NOTIFY) {
      auto e = reinterpret_cast<xcb_xkb_state_notify_event_t*>(event);
      capslock_.enabled = e->lockedMods & capslock_.mask;
      numlock_.enabled = e->lockedMods & numlock_.mask;
      current_layout_ = e->group;
      emit capsLockStateChanged(capslock_.enabled);
      emit numLockStateChanged(numlock_.enabled);
      emit currentLayoutChanged(current_layout_);
    } else if (event->response_type != 0 &&
               event->pad0 == XCB_XKB_NEW_KEYBOARD_NOTIFY) {
      // Reset keyboards. Ignores errors.
      // Sends the layoutChanged signal on success, so don't emit it here.
      getKeyboardLayouts();
    }
    free(event);
  }
}

bool KeyboardModel::getKeyboardLayouts() {
  if (!working_) return false;

  // Get atoms for short and long names
  auto cookie = xcb_xkb_get_names(
      xcb_, XCB_XKB_ID_USE_CORE_KBD,
      XCB_XKB_NAME_DETAIL_GROUP_NAMES | XCB_XKB_NAME_DETAIL_SYMBOLS);
  xcb_generic_error_t* error = nullptr;
  auto* reply = xcb_xkb_get_names_reply(xcb_, cookie, &error);
  if (error) {
    qCritical() << "Can't init layouts: " << error->error_code;
    return false;
  }

  const void* buffer = xcb_xkb_get_names_value_list(reply);
  xcb_xkb_get_names_value_list_t res_list;
  xcb_xkb_get_names_value_list_unpack(
      buffer, reply->nTypes, reply->indicators, reply->virtualMods,
      reply->groupNames, reply->nKeys, reply->nKeyAliases, reply->nRadioGroups,
      reply->which, &res_list);

  QList<QString> short_names = parseShortNames(
      atomName(xcb_, xcb_get_atom_name(xcb_, res_list.symbolsName)));

  // Get long names from short names and fill-in layout list.
  layouts_.clear();
  int groups_cnt = xcb_xkb_get_names_value_list_groups_length(reply, &res_list);

  QList<xcb_get_atom_name_cookie_t> cookies;
  for (int i = 0; i < groups_cnt; i++) {
    cookies << xcb_get_atom_name(xcb_, res_list.groups[i]);
  }

  for (int i = 0; i < groups_cnt; i++) {
    QString short_name, long_name = atomName(xcb_, cookies[i]);
    if (i < short_names.length()) short_name = short_names[i];
    layouts_ << new KeyboardLayout(short_name, long_name);
  }
  free(reply);
  emit layoutsChanged();
  return true;
}

void KeyboardModel::setLayout(int id) {
  if (!working_) return;
  current_layout_ = id;
  auto cookie = xcb_xkb_latch_lock_state(xcb_, XCB_XKB_ID_USE_CORE_KBD,
                                         /* affected mask (empty) */ 0,
                                         /* new value (ignored) */ 0,
                                         /* lock group */ 1, current_layout_,
                                         /* latch stuff (ignored) */ 0, 0, 0);
  xcb_generic_error_t* error = xcb_request_check(xcb_, cookie);
  if (error) {
    qWarning() << "Can't update state: " << error->error_code;
  }
}

QList<KeyboardLayout*> KeyboardModel::layouts() const { return layouts_; }

KeyboardLayout::KeyboardLayout(QString short_name, QString long_name)
    : QObject(), short_(std::move(short_name)), long_(std::move(long_name)) {}