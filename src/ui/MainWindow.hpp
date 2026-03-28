#pragma once

#include <QMainWindow>

#include <QHash>
#include <QString>

#include <memory>
#include <optional>

class DatabaseManager;
class QModelIndex;
class QPoint;
class QStandardItem;
class QStandardItemModel;
class QTabWidget;
class QTreeView;
class QAction;
class QCloseEvent;
class QResizeEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QToolBar;
class QToolButton;
class QWidget;
class QShortcut;

namespace vaultrdp::core::repository {
class ConnectionRepository;
struct ConnectionLaunchInfo;
class CredentialRepository;
class GatewayRepository;
class Repository;
class SecretRepository;
}  // namespace vaultrdp::core::repository

namespace vaultrdp::core {
class VaultManager;
}
namespace vaultrdp::model {
struct Connection;
}
namespace vaultrdp::protocols {
class RdpSession;
enum class SessionState : int;
}
namespace vaultrdp::ui {
class SessionTabContent;
class SessionController;
class SessionWorkspace;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(DatabaseManager* databaseManager, vaultrdp::core::VaultManager* vaultManager,
                      QWidget* parent = nullptr);
  ~MainWindow() override;
  void refreshVaultUi();

 protected:
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  enum class ThemeMode : int {
    System = 0,
    Dark = 1,
    Light = 2,
  };
  enum class FullscreenMode : int {
    Windowed = 0,
    Entering = 1,
    Active = 2,
    Exiting = 3,
  };

  void restoreUiSettings();
  void persistUiSettings() const;
  void updateCreateActionAvailability();
  QString treeItemKey(const QModelIndex& index) const;
  void captureTreeState(QStringList* expandedKeys, QString* selectedKey) const;
  void restoreTreeState(const QStringList& expandedKeys, const QString& selectedKey);
  void setupUi();
  void setupMenuBar();
  void setupToolBar();
  void scheduleTreeReload();
  void applyTreeFilter(const QString& filterText);
  bool applyTreeFilterRecursive(const QModelIndex& index, const QString& filterLower);
  void applyTheme(ThemeMode mode);
  ThemeMode savedThemeMode() const;
  void setThemeMode(ThemeMode mode);
  void reloadFolderTree();
  void createFolder();
  void createConnection();
  void createCredential();
  void createGateway();
  void editSelectedItem();
  void editSelectedConnection();
  void editSelectedCredential();
  void editSelectedGateway();
  void showVaultSettingsDialog();
  bool ensureVaultUnlocked();
  void updateVaultStatus();
  void applyVaultUiState();
  void maybeRunFirstStartupEncryptionWizard();
  void maybePromptUnlockOnStartup();
  std::optional<QString> selectedFolderId() const;
  std::optional<QString> selectedConnectionId() const;
  std::optional<QString> selectedCredentialId() const;
  std::optional<QString> selectedGatewayId() const;
  void connectSelectedConnection();
  void connectOrDisconnectSelectedConnection();
  bool promptForCredentials(const std::optional<QString>& suggestedUsername,
                            const std::optional<QString>& suggestedDomain,
                            std::optional<QString>* usernameOut, std::optional<QString>* domainOut,
                            std::optional<QString>* passwordOut, bool forGateway = false);
  bool isAuthenticationFailureMessage(const QString& message) const;
  bool isGatewayAuthenticationFailureMessage(const QString& message) const;
  void disconnectCurrentSession();
  void disconnectSelectedConnection();
  void disconnectAllSessions();
  void logoffCurrentSession();
  void openSelectedConnectionFullscreen();
  void toggleCurrentSessionFullscreen();
  void exitSessionFullscreen();
  bool hasActiveSessionForConnection(const QString& connectionId) const;
  bool closeSessionForConnection(const QString& connectionId);
  QString formatSessionErrorForDisplay(const QString& message) const;
  void enterSessionFullscreenForConnection(const QString& connectionId);
  void showTreeContextMenu(const QPoint& pos);
  void renameSelectedItem();
  void duplicateSelectedConnection();
  void duplicateSelectedCredential();
  void duplicateSelectedGateway();
  void deleteSelectedItem();
  void copySelectedHostname();
  void copySelectedUsername();
  void onTreeItemChanged(class QStandardItem* item);
  void expandSubtree(const QModelIndex& rootIndex);
  void collapseSubtree(const QModelIndex& rootIndex);
  void handleTabCloseRequested(int index);
  void addSessionTab(const vaultrdp::core::repository::ConnectionLaunchInfo& launchInfo);
  void updateSessionTabState(const QString& connectionId, vaultrdp::protocols::SessionState state);
  QString sessionStateLabel(vaultrdp::protocols::SessionState state) const;
  void ensureWelcomeTab();
  std::optional<QString> currentSessionConnectionId() const;
  void syncClipboardToFocusedSession();
  bool isSessionFullscreenActive() const;
  bool isSessionGenerationCurrent(const QString& connectionId, quint64 generation,
                                  const char* eventName) const;
  void resetSessionControllerStateForManualConnect(const QString& connectionId);
  void closeSessionTabForConnection(const QString& connectionId);
  bool validateMoveByScopeRules(int itemType, const QString& itemId, const std::optional<QString>& destinationFolderId,
                                QString* messageOut) const;

  DatabaseManager* databaseManager_;
  vaultrdp::core::VaultManager* vaultManager_;
  std::unique_ptr<vaultrdp::core::repository::Repository> repository_;
  std::unique_ptr<vaultrdp::core::repository::ConnectionRepository> connectionRepository_;
  std::unique_ptr<vaultrdp::core::repository::CredentialRepository> credentialRepository_;
  std::unique_ptr<vaultrdp::core::repository::GatewayRepository> gatewayRepository_;
  std::unique_ptr<vaultrdp::core::repository::SecretRepository> secretRepository_;
  QStandardItemModel* folderTreeModel_;
  QTreeView* folderTreeView_;
  QLineEdit* treeSearchEdit_;
  QTabWidget* sessionTabWidget_;
  QAction* newFolderAction_;
  QAction* newConnectionAction_;
  QAction* newGatewayAction_;
  QAction* newCredentialAction_;
  QAction* connectAction_;
  QAction* logoffAction_;
  QAction* exitFullscreenAction_;
  QAction* disconnectAction_;
  QAction* disconnectAllAction_;
  QAction* fullscreenSessionAction_;
  QAction* lockVaultAction_;
  QAction* themeSystemAction_;
  QAction* themeDarkAction_;
  QAction* themeLightAction_;
  QLabel* vaultStatusLabel_;
  QLabel* debugModeLabel_;
  QPushButton* unlockVaultButton_;
  QToolButton* treeConnectButton_;
  QToolButton* treeNewConnectionButton_;
  QToolButton* treeNewFolderButton_;
  QToolButton* treeNewCredentialButton_;
  QToolButton* treeNewGatewayButton_;
  QToolBar* mainToolBar_;
  QSplitter* mainSplitter_;
  QWidget* treePaneWidget_;
  QWidget* welcomeTab_;
  QShortcut* fullscreenShortcut_;
  QShortcut* exitFullscreenShortcut_;
  FullscreenMode fullscreenMode_;
  QString sessionFullscreenConnectionId_;
  QList<int> splitterSizesBeforeSessionFullscreen_;
  Qt::WindowStates windowStateBeforeSessionFullscreen_;
  QSet<QString> pendingFullscreenByConnection_;
  std::unique_ptr<vaultrdp::ui::SessionWorkspace> sessionWorkspace_;
  bool suppressClipboardEvent_;
  qint64 ignoreClipboardEventsUntilMs_;
  QString lastRemoteClipboardText_;
  QString lastRemoteClipboardUriList_;
  bool lastClipboardWasRemoteFileUris_;
  bool isReloadingTree_;
  bool isApplyingTreeFilter_;
  bool treeMutationGuard_;
  bool treeReloadScheduled_;
};
