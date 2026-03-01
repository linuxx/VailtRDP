#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>

#include <optional>

#include "protocols/ISession.hpp"

namespace vaultrdp::protocols {

class RdpSessionWorker;

class RdpSession : public ISession {
  Q_OBJECT

 public:
  RdpSession(QString host, int port, std::optional<QString> username, std::optional<QString> domain,
             std::optional<QString> password, std::optional<QString> gatewayHost, int gatewayPort,
             std::optional<QString> gatewayUsername, std::optional<QString> gatewayDomain,
             std::optional<QString> gatewayPassword, bool gatewayUseSameCredentials,
             int initialWidth, int initialHeight,
             bool enableClipboard, bool mapHomeDrive, QObject* parent = nullptr);
  ~RdpSession() override;

  void connectSession() override;
  void disconnectSession() override;
  void resizeSession(int width, int height) override;
  bool isConnected() const override;
  SessionState state() const override;

  void sendKeyInput(int qtKey, quint32 nativeScanCode, bool pressed);
  void sendMouseMove(int x, int y);
  void sendMouseButton(Qt::MouseButton button, bool pressed, int x, int y);
  void sendWheel(Qt::Orientation orientation, int delta, int x, int y);
  void setLocalClipboardText(const QString& text);
  void setLocalClipboardFileUris(const QString& uriList);

Q_SIGNALS:
  void frameUpdated(const QImage& frame);
  void remoteClipboardText(const QString& text);
  void remoteClipboardFileUris(const QString& uriList);
  void remoteLogoff();

 private:
  void setState(SessionState newState);
  void ensureWorkerThread();
  void shutdownWorkerThread();

  QString host_;
  int port_;
  std::optional<QString> username_;
  std::optional<QString> domain_;
  std::optional<QString> password_;
  std::optional<QString> gatewayHost_;
  int gatewayPort_;
  std::optional<QString> gatewayUsername_;
  std::optional<QString> gatewayDomain_;
  std::optional<QString> gatewayPassword_;
  bool gatewayUseSameCredentials_;
  int initialWidth_;
  int initialHeight_;
  bool enableClipboard_;
  bool mapHomeDrive_;
  QString sessionTag_;
  SessionState state_;
  bool shutdownStarted_;
  bool stopIssued_;

  QThread workerThread_;
  QPointer<RdpSessionWorker> worker_;
};

}  // namespace vaultrdp::protocols
