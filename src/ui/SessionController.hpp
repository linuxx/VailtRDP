#pragma once

#include <QHash>
#include <QString>

namespace vaultrdp::ui {

class SessionController {
 public:
  void onSessionCreated(const QString& connectionId);
  void onSessionConnected(const QString& connectionId);
  void onSessionClosed(const QString& connectionId);

  void setAutoReconnectBlocked(const QString& connectionId, bool blocked);
  bool isAutoReconnectBlocked(const QString& connectionId) const;

  void setAutoReconnectArmed(const QString& connectionId, bool armed);
  bool isAutoReconnectArmed(const QString& connectionId) const;

  void setAuthPromptActive(const QString& connectionId, bool active);
  bool isAuthPromptActive(const QString& connectionId) const;

  int incrementAuthFailurePromptCount(const QString& connectionId);
  int authFailurePromptCount(const QString& connectionId) const;
  void setAuthFailurePromptCount(const QString& connectionId, int count);

  void setLastAuthFailureWasGateway(const QString& connectionId, bool gatewayFailure);
  bool lastAuthFailureWasGateway(const QString& connectionId) const;

  void setLastAuthPromptMs(const QString& connectionId, qint64 valueMs);
  qint64 lastAuthPromptMs(const QString& connectionId) const;

  void setReconnectAttempts(const QString& connectionId, int attempts);
  int reconnectAttempts(const QString& connectionId) const;

  bool hasEverConnected(const QString& connectionId) const;

 private:
  QHash<QString, bool> authPromptActiveByConnection_;
  QHash<QString, bool> blockAutoReconnectByConnection_;
  QHash<QString, bool> autoReconnectArmedByConnection_;
  QHash<QString, int> authFailurePromptCountByConnection_;
  QHash<QString, bool> lastAuthFailureWasGatewayByConnection_;
  QHash<QString, qint64> lastAuthPromptMsByConnection_;
  QHash<QString, int> reconnectAttemptsByConnection_;
  QHash<QString, bool> hasEverConnectedByConnection_;
};

}  // namespace vaultrdp::ui

