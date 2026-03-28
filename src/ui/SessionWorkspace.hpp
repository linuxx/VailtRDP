#pragma once

#include <QHash>
#include <QString>

#include <memory>
#include <optional>

#include "core/repository/ConnectionRepository.hpp"
#include "ui/SessionController.hpp"

class QObject;
class QTabWidget;
class QWidget;

namespace vaultrdp::protocols {
class RdpSession;
enum class SessionState : int;
}

namespace vaultrdp::ui {
class SessionTabContent;

struct SessionHandle {
  QString connectionId;
  quint64 generation = 0;
  SessionTabContent* content = nullptr;
  vaultrdp::protocols::RdpSession* session = nullptr;
};

class SessionWorkspace {
 public:
  SessionWorkspace(QTabWidget* sessionTabWidget, QWidget* welcomeTab, QObject* sessionParent);

  SessionController* controller();
  const SessionController* controller() const;

  std::optional<SessionHandle> createSessionTab(
      const vaultrdp::core::repository::ConnectionLaunchInfo& launchInfo);
  bool hasActiveSessionForConnection(const QString& connectionId) const;
  QWidget* tabForConnection(const QString& connectionId) const;
  SessionTabContent* contentForConnection(const QString& connectionId) const;
  vaultrdp::protocols::RdpSession* sessionForConnection(const QString& connectionId) const;
  std::optional<vaultrdp::core::repository::ConnectionLaunchInfo> launchInfoForConnection(
      const QString& connectionId) const;
  vaultrdp::core::repository::ConnectionLaunchInfo* mutableLaunchInfoForConnection(const QString& connectionId);
  bool isClipboardEnabledForConnection(const QString& connectionId) const;

  std::optional<QString> currentSessionConnectionId() const;
  void ensureWelcomeTab();
  void updateSessionTabState(const QString& connectionId, vaultrdp::protocols::SessionState state);
  void syncClipboardToFocusedSession(const QString& clipboardText);

  bool closeSessionForConnection(const QString& connectionId);
  std::optional<QString> closeSessionAt(int index);

  bool isSessionGenerationCurrent(const QString& connectionId, quint64 generation, const char* eventName) const;

 private:
  QTabWidget* sessionTabWidget_;
  QWidget* welcomeTab_;
  QObject* sessionParent_;
  std::unique_ptr<SessionController> controller_;
  QHash<QString, QWidget*> sessionTabsByConnection_;
  QHash<QString, vaultrdp::protocols::RdpSession*> sessionsByConnection_;
  QHash<QString, bool> sessionClipboardEnabledByConnection_;
  QHash<QString, vaultrdp::core::repository::ConnectionLaunchInfo> launchInfoByConnection_;
  QHash<QString, quint64> sessionGenerationByConnection_;
  quint64 sessionGenerationCounter_ = 0;
};

}  // namespace vaultrdp::ui
