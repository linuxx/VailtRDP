#include "system/ScreenLockMonitor.hpp"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDebug>

namespace {
constexpr const char* kService = "org.gnome.ScreenSaver";
constexpr const char* kPath = "/org/gnome/ScreenSaver";
constexpr const char* kInterface = "org.gnome.ScreenSaver";
}  // namespace

namespace vaultrdp::system {

ScreenLockMonitor::ScreenLockMonitor(QObject* parent) : QObject(parent) {}

bool ScreenLockMonitor::start() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  const bool connected = bus.connect(kService, kPath, kInterface, "ActiveChanged", this, SLOT(onActiveChanged(bool)));

  if (!connected) {
    qWarning() << "Failed to connect to GNOME screen lock signal";
    return false;
  }

  queryCurrentState();
  return true;
}

void ScreenLockMonitor::onActiveChanged(bool active) {
  Q_EMIT screenLockChanged(active);
}

bool ScreenLockMonitor::queryCurrentState() {
  QDBusInterface iface(kService, kPath, kInterface, QDBusConnection::sessionBus());
  if (!iface.isValid()) {
    qWarning() << "GNOME screensaver DBus interface not available";
    return false;
  }

  QDBusPendingReply<bool> reply = iface.asyncCall("GetActive");
  reply.waitForFinished();
  if (reply.isError()) {
    qWarning() << "Failed to query screen lock state:" << reply.error().message();
    return false;
  }

  Q_EMIT screenLockChanged(reply.value());
  return true;
}

}  // namespace vaultrdp::system
