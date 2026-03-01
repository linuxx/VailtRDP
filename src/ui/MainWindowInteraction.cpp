#include "ui/MainWindow.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QSet>
#include <QSqlDatabase>
#include <QStandardItem>
#include <QTreeView>
#include <QTimer>

#include "core/DatabaseManager.hpp"
#include "core/VaultManager.hpp"
#include "core/repository/ConnectionRepository.hpp"
#include "core/repository/CredentialRepository.hpp"
#include "core/repository/GatewayRepository.hpp"
#include "core/repository/Repository.hpp"
#include "core/repository/SecretRepository.hpp"
#include "protocols/RdpSession.hpp"
#include "ui/SessionRuntimeOptions.hpp"
#include "ui/TreeItemRoles.hpp"

namespace {
using vaultrdp::ui::kItemFolderIdRole;
using vaultrdp::ui::kItemIdRole;
using vaultrdp::ui::kItemTypeConnection;
using vaultrdp::ui::kItemTypeCredential;
using vaultrdp::ui::kItemTypeFolder;
using vaultrdp::ui::kItemTypeGateway;
using vaultrdp::ui::kItemTypeRole;
using vaultrdp::ui::kItemTypeVaultRoot;
using vaultrdp::ui::parseSessionRuntimeOptions;
}  // namespace

std::optional<QString> MainWindow::selectedFolderId() const {
  const QModelIndex index = folderTreeView_->currentIndex();
  if (!index.isValid()) {
    return std::nullopt;
  }

  const int itemType = index.data(kItemTypeRole).toInt();
  if (itemType == kItemTypeFolder) {
    const QString id = index.data(kItemIdRole).toString();
    if (!id.isEmpty()) {
      return id;
    }
  }

  if (itemType == kItemTypeConnection) {
    const QString folderId = index.data(kItemFolderIdRole).toString();
    if (!folderId.isEmpty()) {
      return folderId;
    }
  }

  if (itemType == kItemTypeGateway) {
    const QString folderId = index.data(kItemFolderIdRole).toString();
    if (!folderId.isEmpty()) {
      return folderId;
    }
  }
  if (itemType == kItemTypeCredential) {
    const QString folderId = index.data(kItemFolderIdRole).toString();
    if (!folderId.isEmpty()) {
      return folderId;
    }
  }

  return std::nullopt;
}

std::optional<QString> MainWindow::selectedConnectionId() const {
  const QModelIndex index = folderTreeView_->currentIndex();
  if (!index.isValid()) {
    return std::nullopt;
  }

  if (index.data(kItemTypeRole).toInt() != kItemTypeConnection) {
    return std::nullopt;
  }

  const QString id = index.data(kItemIdRole).toString();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  return id;
}

std::optional<QString> MainWindow::selectedCredentialId() const {
  const QModelIndex index = folderTreeView_->currentIndex();
  if (!index.isValid()) {
    return std::nullopt;
  }
  if (index.data(kItemTypeRole).toInt() != kItemTypeCredential) {
    return std::nullopt;
  }
  const QString id = index.data(kItemIdRole).toString();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  return id;
}

std::optional<QString> MainWindow::selectedGatewayId() const {
  const QModelIndex index = folderTreeView_->currentIndex();
  if (!index.isValid()) {
    return std::nullopt;
  }
  if (index.data(kItemTypeRole).toInt() != kItemTypeGateway) {
    return std::nullopt;
  }
  const QString id = index.data(kItemIdRole).toString();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  return id;
}

void MainWindow::connectSelectedConnection() {
  qInfo() << "[ui] connectSelectedConnection requested";
  const auto selectedId = selectedConnectionId();
  if (!selectedId.has_value()) {
    return;
  }

  if (sessionTabsByConnection_.contains(selectedId.value())) {
    QWidget* tab = sessionTabsByConnection_.value(selectedId.value());
    const int existingIndex = sessionTabWidget_->indexOf(tab);
    if (existingIndex >= 0) {
      sessionTabWidget_->setCurrentIndex(existingIndex);
      return;
    }
    sessionTabsByConnection_.remove(selectedId.value());
  }

  const auto maybeConnection = connectionRepository_->findConnectionById(selectedId.value());
  if (!maybeConnection.has_value()) {
    QMessageBox::warning(this, "Connect", "Connection was not found.");
    return;
  }

  if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && maybeConnection->credentialId.has_value()) {
    if (!ensureVaultUnlocked()) {
      return;
    }
  }

  const auto maybeLaunchInfo = connectionRepository_->resolveLaunchInfo(selectedId.value(), vaultManager_);
  if (!maybeLaunchInfo.has_value()) {
    QMessageBox::warning(this, "Connect", "Failed to resolve connection launch settings.");
    return;
  }

  auto launchInfo = maybeLaunchInfo.value();
  if (launchInfo.connection.gatewayId.has_value() &&
      (!launchInfo.gatewayHost.has_value() || launchInfo.gatewayHost->trimmed().isEmpty())) {
    QMessageBox::warning(this, "Connect", "Gateway is configured but missing host/port details.");
    return;
  }
  const auto sessionOptions = parseSessionRuntimeOptions(launchInfo.connection.optionsJson);
  if ((!launchInfo.username.has_value() || launchInfo.username->trimmed().isEmpty()) &&
      !sessionOptions.lastSuccessfulUsername.isEmpty()) {
    launchInfo.username = sessionOptions.lastSuccessfulUsername;
  }

  blockAutoReconnectByConnection_[selectedId.value()] = false;
  autoReconnectArmedByConnection_[selectedId.value()] = false;
  authFailurePromptCountByConnection_[selectedId.value()] = 0;
  lastAuthPromptMsByConnection_[selectedId.value()] = 0;

  const bool needsPrimaryCredentialPrompt = !launchInfo.password.has_value() || launchInfo.password->isEmpty();
  const bool needsGatewayCredentialPrompt =
      launchInfo.gatewayHost.has_value() &&
      (launchInfo.gatewayPromptEachTime || !launchInfo.gatewayPassword.has_value() ||
       launchInfo.gatewayPassword->isEmpty());
  if (needsPrimaryCredentialPrompt || needsGatewayCredentialPrompt) {
    std::optional<QString> enteredUsername;
    std::optional<QString> enteredDomain;
    std::optional<QString> enteredPassword;
    std::optional<QString> suggestedUsername = launchInfo.username;
    std::optional<QString> suggestedDomain = launchInfo.domain;
    if ((!suggestedUsername.has_value() || suggestedUsername->trimmed().isEmpty()) &&
        launchInfo.gatewayUsername.has_value() && !launchInfo.gatewayUsername->trimmed().isEmpty()) {
      suggestedUsername = launchInfo.gatewayUsername;
    }
    if ((!suggestedDomain.has_value() || suggestedDomain->trimmed().isEmpty()) &&
        launchInfo.gatewayDomain.has_value() && !launchInfo.gatewayDomain->trimmed().isEmpty()) {
      suggestedDomain = launchInfo.gatewayDomain;
    }
    if (!promptForCredentials(suggestedUsername, suggestedDomain, &enteredUsername, &enteredDomain,
                              &enteredPassword,
                              needsGatewayCredentialPrompt && !needsPrimaryCredentialPrompt)) {
      return;
    }
    if (needsPrimaryCredentialPrompt) {
      launchInfo.username = enteredUsername;
      launchInfo.domain = enteredDomain;
      launchInfo.password = enteredPassword;
    }
    if (needsGatewayCredentialPrompt) {
      launchInfo.gatewayUsername = enteredUsername;
      launchInfo.gatewayDomain = enteredDomain;
      launchInfo.gatewayPassword = enteredPassword;
    }
  }

  addSessionTab(launchInfo);
}

void MainWindow::disconnectCurrentSession() {
  qInfo() << "[ui] disconnectCurrentSession requested";
  const int index = sessionTabWidget_->currentIndex();
  if (index < 0) {
    return;
  }
  handleTabCloseRequested(index);
}

void MainWindow::disconnectAllSessions() {
  qInfo() << "[ui] disconnectAllSessions requested";
  while (sessionTabWidget_->count() > 0) {
    bool removedAny = false;
    for (int i = sessionTabWidget_->count() - 1; i >= 0; --i) {
      if (sessionTabWidget_->widget(i) == welcomeTab_) {
        continue;
      }
      handleTabCloseRequested(i);
      removedAny = true;
    }
    if (!removedAny) {
      break;
    }
  }
}

void MainWindow::handleTabCloseRequested(int index) {
  qInfo() << "[ui] handleTabCloseRequested index=" << index;
  if (index < 0 || index >= sessionTabWidget_->count()) {
    return;
  }

  QWidget* tab = sessionTabWidget_->widget(index);
  if (tab == nullptr || tab == welcomeTab_) {
    return;
  }

  const QString connectionId = tab->property("connection_id").toString();
  if (!connectionId.isEmpty()) {
    sessionGenerationByConnection_[connectionId] = ++sessionGenerationCounter_;
    qInfo().noquote() << "[session conn=" + connectionId + "] close requested generation advanced to"
                      << sessionGenerationByConnection_.value(connectionId);
    if (sessionsByConnection_.contains(connectionId)) {
      auto* session = sessionsByConnection_.take(connectionId);
      delete session;
    }
    reconnectAttemptsByConnection_.remove(connectionId);
    hasEverConnectedByConnection_.remove(connectionId);
    sessionClipboardEnabledByConnection_.remove(connectionId);
    launchInfoByConnection_.remove(connectionId);
    authPromptActiveByConnection_.remove(connectionId);
    blockAutoReconnectByConnection_.remove(connectionId);
    autoReconnectArmedByConnection_.remove(connectionId);
    authFailurePromptCountByConnection_.remove(connectionId);
    lastAuthFailureWasGatewayByConnection_.remove(connectionId);
    lastAuthPromptMsByConnection_.remove(connectionId);
    sessionTabsByConnection_.remove(connectionId);
  }

  sessionTabWidget_->removeTab(index);
  tab->deleteLater();
  ensureWelcomeTab();
}

void MainWindow::showTreeContextMenu(const QPoint& pos) {
  const QModelIndex index = folderTreeView_->indexAt(pos);
  if (index.isValid()) {
    folderTreeView_->setCurrentIndex(index);
  }

  const int itemType = index.isValid() ? index.data(kItemTypeRole).toInt() : kItemTypeVaultRoot;
  QMenu menu(this);

  if (itemType == kItemTypeConnection) {
    menu.addAction("Connect", this, &MainWindow::connectSelectedConnection);
    menu.addAction("Edit...", this, &MainWindow::editSelectedConnection);
    menu.addAction("Duplicate", this, &MainWindow::duplicateSelectedConnection);
    menu.addAction("Rename", this, &MainWindow::renameSelectedItem);
    menu.addAction("Delete", this, &MainWindow::deleteSelectedItem);
    menu.addSeparator();
    menu.addAction("Copy Hostname", this, &MainWindow::copySelectedHostname);
    menu.addAction("Copy Username", this, &MainWindow::copySelectedUsername);
  } else if (itemType == kItemTypeCredential) {
    menu.addAction("Edit...", this, &MainWindow::editSelectedCredential);
    menu.addAction("Duplicate", this, &MainWindow::duplicateSelectedCredential);
    menu.addAction("Rename", this, &MainWindow::renameSelectedItem);
    menu.addAction("Delete", this, &MainWindow::deleteSelectedItem);
    menu.addSeparator();
    menu.addAction("Copy Username", this, &MainWindow::copySelectedUsername);
  } else if (itemType == kItemTypeGateway) {
    menu.addAction("Edit...", this, &MainWindow::editSelectedGateway);
    menu.addAction("Duplicate", this, &MainWindow::duplicateSelectedGateway);
    menu.addAction("Rename", this, &MainWindow::renameSelectedItem);
    menu.addAction("Delete", this, &MainWindow::deleteSelectedItem);
    menu.addSeparator();
    menu.addAction("Copy Hostname", this, &MainWindow::copySelectedHostname);
    menu.addAction("Copy Username", this, &MainWindow::copySelectedUsername);
  } else {
    if (itemType == kItemTypeVaultRoot) {
      menu.addAction("New Root Folder", this, &MainWindow::createFolder);
      menu.addSeparator();
      menu.addAction("Expand All", folderTreeView_, &QTreeView::expandAll);
      menu.addAction("Collapse All", folderTreeView_, &QTreeView::collapseAll);
    } else if (itemType == kItemTypeFolder) {
      menu.addAction("New Subfolder", this, &MainWindow::createFolder);
      menu.addAction("New Connection...", this, &MainWindow::createConnection);
      menu.addAction("New Credential Set...", this, &MainWindow::createCredential);
      menu.addAction("New Gateway...", this, &MainWindow::createGateway);
      menu.addSeparator();
      menu.addAction("Rename", this, &MainWindow::renameSelectedItem);
      menu.addAction("Delete", this, &MainWindow::deleteSelectedItem);
      menu.addSeparator();
      menu.addAction("Expand All", [this, index]() {
        expandSubtree(index);
      });
      menu.addAction("Collapse All", [this, index]() {
        collapseSubtree(index);
      });
    }
  }

  menu.exec(folderTreeView_->viewport()->mapToGlobal(pos));
}

void MainWindow::renameSelectedItem() {
  const QModelIndex index = folderTreeView_->currentIndex();
  if (!index.isValid()) {
    return;
  }

  const int itemType = index.data(kItemTypeRole).toInt();
  if (itemType != kItemTypeFolder && itemType != kItemTypeConnection && itemType != kItemTypeCredential &&
      itemType != kItemTypeGateway) {
    return;
  }

  folderTreeView_->edit(index);
}

void MainWindow::duplicateSelectedConnection() {
  const auto connectionId = selectedConnectionId();
  if (!connectionId.has_value()) {
    return;
  }

  const auto duplicated = connectionRepository_->duplicateConnection(connectionId.value());
  if (!duplicated.has_value()) {
    QMessageBox::warning(this, "Duplicate Connection", "Failed to duplicate connection.");
    return;
  }
  reloadFolderTree();
}

void MainWindow::duplicateSelectedGateway() {
  const auto gatewayId = selectedGatewayId();
  if (!gatewayId.has_value()) {
    return;
  }

  const auto duplicated = gatewayRepository_->duplicateGateway(gatewayId.value());
  if (!duplicated.has_value()) {
    QMessageBox::warning(this, "Duplicate Gateway", "Failed to duplicate gateway.");
    return;
  }
  reloadFolderTree();
}

void MainWindow::duplicateSelectedCredential() {
  const auto credentialId = selectedCredentialId();
  if (!credentialId.has_value()) {
    return;
  }

  const auto duplicated = credentialRepository_->duplicateCredential(credentialId.value());
  if (!duplicated.has_value()) {
    QMessageBox::warning(this, "Duplicate Credential Set", "Failed to duplicate credential set.");
    return;
  }
  reloadFolderTree();
}

void MainWindow::deleteSelectedItem() {
  const QModelIndex index = folderTreeView_->currentIndex();
  if (!index.isValid()) {
    return;
  }

  const int itemType = index.data(kItemTypeRole).toInt();
  const QString itemId = index.data(kItemIdRole).toString();

  if (itemType == kItemTypeConnection) {
    if (sessionTabsByConnection_.contains(itemId)) {
      QMessageBox::information(this, "Delete Connection",
                               "Disconnect the active session before deleting this connection.");
      return;
    }

    if (QMessageBox::question(this, "Delete Connection", "Delete selected connection?") != QMessageBox::Yes) {
      return;
    }

    if (!connectionRepository_->deleteConnection(itemId)) {
      QMessageBox::warning(this, "Delete Connection", "Failed to delete connection.");
      return;
    }

    reloadFolderTree();
    return;
  }

  if (itemType == kItemTypeGateway) {
    if (gatewayRepository_->isGatewayInUse(itemId)) {
      QMessageBox::information(this, "Delete Gateway",
                               "Gateway is used by one or more connections and cannot be deleted.");
      return;
    }

    if (QMessageBox::question(this, "Delete Gateway", "Delete selected gateway?") != QMessageBox::Yes) {
      return;
    }
    if (!gatewayRepository_->deleteGateway(itemId)) {
      QMessageBox::warning(this, "Delete Gateway", "Failed to delete gateway.");
      return;
    }
    reloadFolderTree();
    return;
  }

  if (itemType == kItemTypeCredential) {
    const int references = credentialRepository_->countCredentialReferences(itemId);
    if (references > 0) {
      QMessageBox::information(this, "Delete Credential Set",
                               "Credential set is used by one or more connections or gateways.");
      return;
    }
    if (QMessageBox::question(this, "Delete Credential Set", "Delete selected credential set?") !=
        QMessageBox::Yes) {
      return;
    }
    const auto maybeCredential = credentialRepository_->findCredentialById(itemId);
    if (!maybeCredential.has_value()) {
      QMessageBox::warning(this, "Delete Credential Set", "Credential set not found.");
      return;
    }
    QSqlDatabase db = databaseManager_->database();
    if (!db.transaction()) {
      QMessageBox::warning(this, "Delete Credential Set", "Failed to start transaction.");
      return;
    }
    if (!credentialRepository_->deleteCredential(itemId) ||
        !secretRepository_->deleteSecret(maybeCredential->secretId)) {
      db.rollback();
      QMessageBox::warning(this, "Delete Credential Set", "Failed to delete credential set.");
      return;
    }
    if (!db.commit()) {
      db.rollback();
      QMessageBox::warning(this, "Delete Credential Set", "Failed to commit transaction.");
      return;
    }
    reloadFolderTree();
    return;
  }

  if (itemType == kItemTypeFolder) {
    const auto allFolders = repository_->listFolders();
    const auto allConnections = connectionRepository_->listConnections();

    QSet<QString> subtree;
    subtree.insert(itemId);
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& folder : allFolders) {
        if (folder.parentId.has_value() && subtree.contains(folder.parentId.value()) && !subtree.contains(folder.id)) {
          subtree.insert(folder.id);
          changed = true;
        }
      }
    }

    int connectionCount = 0;
    for (const auto& conn : allConnections) {
      if (subtree.contains(conn.folderId)) {
        connectionCount++;
      }
    }

    QString prompt = "Delete selected folder?";
    if (subtree.size() > 1 || connectionCount > 0) {
      prompt = QString("Delete folder recursively?\n\nSubfolders: %1\nConnections: %2")
                   .arg(subtree.size() - 1)
                   .arg(connectionCount);
    }

    if (QMessageBox::question(this, "Delete Folder", prompt) != QMessageBox::Yes) {
      return;
    }

    if (!repository_->deleteFolderRecursive(itemId)) {
      QMessageBox::warning(this, "Delete Folder", "Failed to delete folder.");
      return;
    }

    reloadFolderTree();
  }
}

void MainWindow::copySelectedHostname() {
  if (const auto connectionId = selectedConnectionId(); connectionId.has_value()) {
    const auto connection = connectionRepository_->findConnectionById(connectionId.value());
    if (connection.has_value() && QGuiApplication::clipboard() != nullptr) {
      QGuiApplication::clipboard()->setText(connection->host);
    }
    return;
  }

  if (const auto gatewayId = selectedGatewayId(); gatewayId.has_value()) {
    const auto gateway = gatewayRepository_->findGatewayById(gatewayId.value());
    if (gateway.has_value() && QGuiApplication::clipboard() != nullptr) {
      QGuiApplication::clipboard()->setText(gateway->host);
    }
  }
}

void MainWindow::copySelectedUsername() {
  std::optional<QString> username;
  if (const auto connectionId = selectedConnectionId(); connectionId.has_value()) {
    username = connectionRepository_->findUsernameByConnectionId(connectionId.value());
  } else if (const auto credentialId = selectedCredentialId(); credentialId.has_value()) {
    const auto credential = credentialRepository_->findCredentialById(credentialId.value());
    if (credential.has_value()) {
      username = credential->username;
    }
  } else if (const auto gatewayId = selectedGatewayId(); gatewayId.has_value()) {
    username = gatewayRepository_->findUsernameByGatewayId(gatewayId.value());
  }

  if (!username.has_value()) {
    QMessageBox::information(this, "Copy Username", "No saved username for the selected item.");
    return;
  }

  if (QGuiApplication::clipboard() != nullptr) {
    QGuiApplication::clipboard()->setText(username.value());
  }
}

void MainWindow::onTreeItemChanged(QStandardItem* item) {
  if (item == nullptr || isReloadingTree_) {
    return;
  }

  const int itemType = item->data(kItemTypeRole).toInt();
  const QString itemId = item->data(kItemIdRole).toString();
  const QString newName = item->text().trimmed();
  if (newName.isEmpty()) {
    reloadFolderTree();
    return;
  }

  bool ok = false;
  if (itemType == kItemTypeFolder) {
    ok = repository_->renameFolder(itemId, newName);
  } else if (itemType == kItemTypeConnection) {
    ok = connectionRepository_->renameConnection(itemId, newName);
  } else if (itemType == kItemTypeCredential) {
    ok = credentialRepository_->renameCredential(itemId, newName);
  } else if (itemType == kItemTypeGateway) {
    ok = gatewayRepository_->renameGateway(itemId, newName);
  }

  if (!ok) {
    QMessageBox::warning(this, "Rename", "Failed to rename item.");
    reloadFolderTree();
    return;
  }

  if (itemType == kItemTypeFolder) {
    // Defer rebuild to avoid mutating the model during itemChanged processing.
    QTimer::singleShot(0, this, [this]() { reloadFolderTree(); });
  }
}

void MainWindow::expandSubtree(const QModelIndex& rootIndex) {
  if (!rootIndex.isValid()) {
    return;
  }

  folderTreeView_->setExpanded(rootIndex, true);
  const int rowCount = folderTreeModel_->rowCount(rootIndex);
  for (int row = 0; row < rowCount; ++row) {
    expandSubtree(folderTreeModel_->index(row, 0, rootIndex));
  }
}

void MainWindow::collapseSubtree(const QModelIndex& rootIndex) {
  if (!rootIndex.isValid()) {
    return;
  }

  const int rowCount = folderTreeModel_->rowCount(rootIndex);
  for (int row = 0; row < rowCount; ++row) {
    collapseSubtree(folderTreeModel_->index(row, 0, rootIndex));
  }
  folderTreeView_->setExpanded(rootIndex, false);
}
