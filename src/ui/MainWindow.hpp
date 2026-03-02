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
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QToolBar;
class QToolButton;
class QWidget;

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

 private:
  enum class ThemeMode : int {
    System = 0,
    Dark = 1,
    Light = 2,
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
  bool promptForCredentials(const std::optional<QString>& suggestedUsername,
                            const std::optional<QString>& suggestedDomain,
                            std::optional<QString>* usernameOut, std::optional<QString>* domainOut,
                            std::optional<QString>* passwordOut, bool forGateway = false);
  bool isAuthenticationFailureMessage(const QString& message) const;
  bool isGatewayAuthenticationFailureMessage(const QString& message) const;
  void disconnectCurrentSession();
  void disconnectAllSessions();
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
  QAction* disconnectAction_;
  QAction* disconnectAllAction_;
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
  QWidget* welcomeTab_;
  QHash<QString, QWidget*> sessionTabsByConnection_;
  QHash<QString, vaultrdp::protocols::RdpSession*> sessionsByConnection_;
  QHash<QString, bool> sessionClipboardEnabledByConnection_;
  QHash<QString, vaultrdp::core::repository::ConnectionLaunchInfo> launchInfoByConnection_;
  QHash<QString, bool> authPromptActiveByConnection_;
  QHash<QString, bool> blockAutoReconnectByConnection_;
  QHash<QString, bool> autoReconnectArmedByConnection_;
  QHash<QString, int> authFailurePromptCountByConnection_;
  QHash<QString, bool> lastAuthFailureWasGatewayByConnection_;
  QHash<QString, qint64> lastAuthPromptMsByConnection_;
  QHash<QString, int> reconnectAttemptsByConnection_;
  QHash<QString, bool> hasEverConnectedByConnection_;
  QHash<QString, quint64> sessionGenerationByConnection_;
  quint64 sessionGenerationCounter_;
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
