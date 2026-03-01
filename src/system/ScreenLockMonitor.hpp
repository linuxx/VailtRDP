#pragma once

#include <QObject>

class QDBusMessage;

namespace vaultrdp::system {

class ScreenLockMonitor : public QObject {
  Q_OBJECT

 public:
  explicit ScreenLockMonitor(QObject* parent = nullptr);

  bool start();

 Q_SIGNALS:
  void screenLockChanged(bool locked);

 private Q_SLOTS:
  void onActiveChanged(bool active);

 private:
  bool queryCurrentState();
};

}  // namespace vaultrdp::system
