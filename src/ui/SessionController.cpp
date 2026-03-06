#include "ui/SessionController.hpp"

#include <QtGlobal>

namespace vaultrdp::ui {

void SessionController::onSessionCreated(const QString& connectionId) {
  reconnectAttemptsByConnection_.insert(connectionId, 0);
  hasEverConnectedByConnection_.insert(connectionId, false);
  authPromptActiveByConnection_.insert(connectionId, false);
  lastAuthFailureWasGatewayByConnection_.insert(connectionId, false);
  autoReconnectArmedByConnection_.insert(connectionId, false);
  blockAutoReconnectByConnection_.insert(connectionId, false);
  authFailurePromptCountByConnection_.insert(connectionId, 0);
  lastAuthPromptMsByConnection_.insert(connectionId, 0);
}

void SessionController::onSessionConnected(const QString& connectionId) {
  blockAutoReconnectByConnection_[connectionId] = false;
  authFailurePromptCountByConnection_[connectionId] = 0;
  lastAuthFailureWasGatewayByConnection_[connectionId] = false;
  lastAuthPromptMsByConnection_[connectionId] = 0;
  reconnectAttemptsByConnection_[connectionId] = 0;
  hasEverConnectedByConnection_[connectionId] = true;
  autoReconnectArmedByConnection_[connectionId] = false;
  authPromptActiveByConnection_[connectionId] = false;
}

void SessionController::onSessionClosed(const QString& connectionId) {
  reconnectAttemptsByConnection_.remove(connectionId);
  hasEverConnectedByConnection_.remove(connectionId);
  authPromptActiveByConnection_.remove(connectionId);
  blockAutoReconnectByConnection_.remove(connectionId);
  autoReconnectArmedByConnection_.remove(connectionId);
  authFailurePromptCountByConnection_.remove(connectionId);
  lastAuthFailureWasGatewayByConnection_.remove(connectionId);
  lastAuthPromptMsByConnection_.remove(connectionId);
}

void SessionController::setAutoReconnectBlocked(const QString& connectionId, bool blocked) {
  blockAutoReconnectByConnection_[connectionId] = blocked;
}

bool SessionController::isAutoReconnectBlocked(const QString& connectionId) const {
  return blockAutoReconnectByConnection_.value(connectionId, false);
}

void SessionController::setAutoReconnectArmed(const QString& connectionId, bool armed) {
  autoReconnectArmedByConnection_[connectionId] = armed;
}

bool SessionController::isAutoReconnectArmed(const QString& connectionId) const {
  return autoReconnectArmedByConnection_.value(connectionId, false);
}

void SessionController::setAuthPromptActive(const QString& connectionId, bool active) {
  authPromptActiveByConnection_[connectionId] = active;
}

bool SessionController::isAuthPromptActive(const QString& connectionId) const {
  return authPromptActiveByConnection_.value(connectionId, false);
}

int SessionController::incrementAuthFailurePromptCount(const QString& connectionId) {
  const int next = authFailurePromptCountByConnection_.value(connectionId, 0) + 1;
  authFailurePromptCountByConnection_[connectionId] = next;
  return next;
}

int SessionController::authFailurePromptCount(const QString& connectionId) const {
  return authFailurePromptCountByConnection_.value(connectionId, 0);
}

void SessionController::setAuthFailurePromptCount(const QString& connectionId, int count) {
  authFailurePromptCountByConnection_[connectionId] = qMax(0, count);
}

void SessionController::setLastAuthFailureWasGateway(const QString& connectionId, bool gatewayFailure) {
  lastAuthFailureWasGatewayByConnection_[connectionId] = gatewayFailure;
}

bool SessionController::lastAuthFailureWasGateway(const QString& connectionId) const {
  return lastAuthFailureWasGatewayByConnection_.value(connectionId, false);
}

void SessionController::setLastAuthPromptMs(const QString& connectionId, qint64 valueMs) {
  lastAuthPromptMsByConnection_[connectionId] = valueMs;
}

qint64 SessionController::lastAuthPromptMs(const QString& connectionId) const {
  return lastAuthPromptMsByConnection_.value(connectionId, 0);
}

void SessionController::setReconnectAttempts(const QString& connectionId, int attempts) {
  reconnectAttemptsByConnection_[connectionId] = qMax(0, attempts);
}

int SessionController::reconnectAttempts(const QString& connectionId) const {
  return reconnectAttemptsByConnection_.value(connectionId, 0);
}

bool SessionController::hasEverConnected(const QString& connectionId) const {
  return hasEverConnectedByConnection_.value(connectionId, false);
}

}  // namespace vaultrdp::ui

