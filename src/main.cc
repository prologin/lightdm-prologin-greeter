#include <QApplication>
#include <QFile>
#include <QNetworkProxy>
#include <QSettings>
#include <QTimer>
#include <iostream>

#include "ProloGreet.h"

namespace {

constexpr char kDefaultConfigLocation[] =
    "/etc/lightdm/lightdm-prologin-greeter.conf";

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setQuitOnLastWindowClosed(true);

  // Load INI conf.
  QString conf_path = kDefaultConfigLocation;
  if (QApplication::arguments().length() >= 2) {
    conf_path = QApplication::arguments().at(1);
  }

  Options options;
  QSettings conf(conf_path, QSettings::IniFormat);
  conf.beginGroup("greeter");
  const QString url = conf.value("url").toString();
  if (!url.isEmpty()) options.url = url;
  const QColor bg_color(conf.value("background_color").toString());
  if (bg_color.isValid()) options.background_color = bg_color;
  bool ok;
  const int delay = conf.value("fallback_delay").toInt(&ok);
  if (ok) options.fallback_delay = delay;
  const auto& proxy_spec = conf.value("http_proxy").toString();
  conf.endGroup();
  if (conf.status() != QSettings::NoError) {
    std::cerr << "could not parse config at '" << conf_path.toStdString()
              << "'\n";
    return 1;
  }

  if (!proxy_spec.isEmpty()) {
    QRegExp spec(R"(^([\da-f:\.]+):(\d+)(\+dns)?$)", Qt::CaseInsensitive);
    spec.setMinimal(true);
    if (!spec.exactMatch(proxy_spec)) {
      std::cerr << "invalid proxy syntax: " << proxy_spec.toStdString() << "\n";
    } else {
      std::cerr << "proxy is " << proxy_spec.toStdString() << "\n";
      QNetworkProxy p(QNetworkProxy::Socks5Proxy, spec.cap(1),
                      spec.cap(2).toInt());
      if (!spec.cap(3).isEmpty())
        p.setCapabilities(QNetworkProxy::HostNameLookupCapability);
      QNetworkProxy::setApplicationProxy(p);
    }
  }

  // Initialize and show the greeter.
  ProloGreet greeter(options);
  greeter.show();
  QApplication::processEvents(QEventLoop::AllEvents);
  if (!greeter.Start()) {
    QTimer::singleShot(6000, []() { QApplication::exit(42); });
  }

  int ret = QApplication::exec();
  std::cerr << "exited gracefully with code " << ret << "\n";
  return ret;
}
