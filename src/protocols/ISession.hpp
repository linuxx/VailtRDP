#pragma once

#include <QObject>
#include <QString>

namespace vaultrdp::protocols {

enum class SessionState {
  Initialized = 0,
  Connecting = 1,
  Connected = 2,
  Disconnected = 3,
  Error = 4,
};

class ISession : public QObject {
  Q_OBJECT

 public:
  explicit ISession(QObject* parent = nullptr) : QObject(parent) {}
  ~ISession() override = default;

  virtual void connectSession() = 0;
  virtual void disconnectSession() = 0;
  virtual void resizeSession(int width, int height) = 0;
  virtual bool isConnected() const = 0;
  virtual SessionState state() const = 0;

 Q_SIGNALS:
  void stateChanged(vaultrdp::protocols::SessionState state);
  void errorOccurred(const QString& message);
};

}  // namespace vaultrdp::protocols
