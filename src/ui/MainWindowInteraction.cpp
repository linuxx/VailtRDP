#include "ui/MainWindow.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QSet>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QStandardItem>
#include <QTreeView>
#include <QTimer>
#include <QScreen>
#include <QSplitter>
#include <QWindow>

#include "core/DatabaseManager.hpp"
#include "core/VaultManager.hpp"
#include "core/repository/ConnectionRepository.hpp"
#include "core/repository/CredentialRepository.hpp"
#include "core/repository/GatewayRepository.hpp"
#include "core/repository/Repository.hpp"
#include "core/repository/SecretRepository.hpp"
#include "protocols/RdpSession.hpp"
#include "ui/IconTheme.hpp"
#include "ui/SessionController.hpp"
#include "ui/SessionTabContent.hpp"
#include "ui/SessionRuntimeOptions.hpp"
#include "ui/TreeItemRoles.hpp"

namespace {
using vaultrdp::ui::kItemFolderIdRole;
using vaultrdp::ui::kItemIdRole;
using vaultrdp::ui::kItemOriginalNameRole;
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

  if (hasActiveSessionForConnection(selectedId.value())) {
    QWidget* tab = sessionTabsByConnection_.value(selectedId.value());
    const int existingIndex = sessionTabWidget_->indexOf(tab);
    if (existingIndex >= 0) {
      sessionTabWidget_->setCurrentIndex(existingIndex);
    }
    updateCreateActionAvailability();
    return;
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

  resetSessionControllerStateForManualConnect(selectedId.value());

  const bool needsPrimaryCredentialPrompt =
      sessionOptions.promptEveryTime || !launchInfo.password.has_value() || launchInfo.password->isEmpty();
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
  updateCreateActionAvailability();
}

void MainWindow::connectOrDisconnectSelectedConnection() {
  const auto selectedId = selectedConnectionId();
  if (!selectedId.has_value()) {
    return;
  }
  if (hasActiveSessionForConnection(selectedId.value())) {
    disconnectSelectedConnection();
    return;
  }
  connectSelectedConnection();
}

void MainWindow::disconnectCurrentSession() {
  qInfo() << "[ui] disconnectCurrentSession requested";
  const int index = sessionTabWidget_->currentIndex();
  if (index < 0) {
    return;
  }
  handleTabCloseRequested(index);
}

void MainWindow::disconnectSelectedConnection() {
  qInfo() << "[ui] disconnectSelectedConnection requested";
  const auto selectedId = selectedConnectionId();
  if (!selectedId.has_value()) {
    return;
  }
  closeSessionForConnection(selectedId.value());
}

bool MainWindow::hasActiveSessionForConnection(const QString& connectionId) const {
  QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
  if (tab == nullptr || sessionTabWidget_ == nullptr) {
    return false;
  }
  return sessionTabWidget_->indexOf(tab) >= 0;
}

bool MainWindow::closeSessionForConnection(const QString& connectionId) {
  QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
  if (tab == nullptr || sessionTabWidget_ == nullptr) {
    sessionTabsByConnection_.remove(connectionId);
    return false;
  }
  const int index = sessionTabWidget_->indexOf(tab);
  if (index < 0) {
    sessionTabsByConnection_.remove(connectionId);
    return false;
  }
  handleTabCloseRequested(index);
  return true;
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

void MainWindow::logoffCurrentSession() {
  qInfo() << "[ui] logoffCurrentSession requested";
  const auto connectionId = currentSessionConnectionId();
  if (!connectionId.has_value()) {
    return;
  }
  auto* session = sessionsByConnection_.value(connectionId.value(), nullptr);
  if (session == nullptr || session->state() != vaultrdp::protocols::SessionState::Connected) {
    return;
  }

  auto releaseModifiers = [session]() {
    session->sendKeyInput(Qt::Key_Meta, 0, false);
    session->sendKeyInput(Qt::Key_Super_L, 0, false);
    session->sendKeyInput(Qt::Key_Super_R, 0, false);
    session->sendKeyInput(Qt::Key_Control, 0, false);
    session->sendKeyInput(Qt::Key_Alt, 0, false);
    session->sendKeyInput(Qt::Key_Shift, 0, false);
  };
  auto sendTap = [session](int qtKey) {
    session->sendKeyInput(qtKey, 0, true);
    session->sendKeyInput(qtKey, 0, false);
  };
  auto sendLetter = [session](QChar ch) {
    const ushort uc = ch.toLower().unicode();
    if (uc < 'a' || uc > 'z') {
      return;
    }
    const int qtKey = Qt::Key_A + int(uc - 'a');
    session->sendKeyInput(qtKey, 0, true);
    session->sendKeyInput(qtKey, 0, false);
  };

  // Defensive reset first to avoid stuck modifier state across sessions/focus changes.
  releaseModifiers();

  // Win+R opens the Windows Run dialog, then we type "logoff" and press Enter.
  session->sendKeyInput(Qt::Key_Meta, 0, true);
  session->sendKeyInput(Qt::Key_R, 0, true);
  session->sendKeyInput(Qt::Key_R, 0, false);
  session->sendKeyInput(Qt::Key_Meta, 0, false);

  QTimer::singleShot(120, this, [this, connectionId, sendLetter, sendTap, releaseModifiers]() {
    auto* active = sessionsByConnection_.value(connectionId.value(), nullptr);
    if (active == nullptr || active->state() != vaultrdp::protocols::SessionState::Connected) {
      return;
    }
    const QString command = "logoff";
    for (const QChar ch : command) {
      sendLetter(ch);
    }
    sendTap(Qt::Key_Return);
    releaseModifiers();
  });
}

void MainWindow::openSelectedConnectionFullscreen() {
  const auto selectedId = selectedConnectionId();
  if (!selectedId.has_value()) {
    return;
  }
  const QString connectionId = selectedId.value();
  if (hasActiveSessionForConnection(connectionId)) {
    enterSessionFullscreenForConnection(connectionId);
    return;
  }
  pendingFullscreenByConnection_.insert(connectionId);
  connectSelectedConnection();
}

void MainWindow::toggleCurrentSessionFullscreen() {
  if (isSessionFullscreenActive()) {
    exitSessionFullscreen();
    return;
  }
  const auto currentId = currentSessionConnectionId();
  if (!currentId.has_value()) {
    openSelectedConnectionFullscreen();
    return;
  }
  enterSessionFullscreenForConnection(currentId.value());
}

bool MainWindow::isSessionFullscreenActive() const {
  return fullscreenMode_ == FullscreenMode::Entering || fullscreenMode_ == FullscreenMode::Active;
}

void MainWindow::enterSessionFullscreenForConnection(const QString& connectionId) {
  if (!hasActiveSessionForConnection(connectionId)) {
    return;
  }
  if (!isSessionFullscreenActive()) {
    fullscreenMode_ = FullscreenMode::Entering;
    splitterSizesBeforeSessionFullscreen_ = mainSplitter_ != nullptr ? mainSplitter_->sizes() : QList<int>{};
    windowStateBeforeSessionFullscreen_ = windowState();
  }
  sessionFullscreenConnectionId_ = connectionId;
  QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
  if (tab != nullptr && sessionTabWidget_ != nullptr) {
    const int index = sessionTabWidget_->indexOf(tab);
    if (index >= 0) {
      sessionTabWidget_->setCurrentIndex(index);
    }
  }
  if (mainSplitter_ != nullptr) {
    if (treePaneWidget_ != nullptr) {
      treePaneWidget_->setMinimumWidth(0);
      treePaneWidget_->setMaximumWidth(0);
      treePaneWidget_->setVisible(false);
    }
    mainSplitter_->setSizes({0, 1});
  }
  showFullScreen();
  fullscreenMode_ = FullscreenMode::Active;
  updateCreateActionAvailability();
}

void MainWindow::exitSessionFullscreen() {
  if (!isSessionFullscreenActive()) {
    return;
  }
  fullscreenMode_ = FullscreenMode::Exiting;
  sessionFullscreenConnectionId_.clear();
  if ((windowStateBeforeSessionFullscreen_ & Qt::WindowMaximized) == Qt::WindowMaximized) {
    showMaximized();
  } else {
    showNormal();
  }
  if (mainSplitter_ != nullptr && splitterSizesBeforeSessionFullscreen_.size() >= 2) {
    if (treePaneWidget_ != nullptr) {
      treePaneWidget_->setMaximumWidth(QWIDGETSIZE_MAX);
      treePaneWidget_->setMinimumWidth(0);
      treePaneWidget_->setVisible(true);
    }
    mainSplitter_->setSizes(splitterSizesBeforeSessionFullscreen_);
  }
  fullscreenMode_ = FullscreenMode::Windowed;
  updateCreateActionAvailability();
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
  const bool closingFullscreenSession =
      isSessionFullscreenActive() && !sessionFullscreenConnectionId_.isEmpty() &&
      sessionFullscreenConnectionId_ == connectionId;
  if (!connectionId.isEmpty()) {
    pendingFullscreenByConnection_.remove(connectionId);
    sessionGenerationByConnection_[connectionId] = ++sessionGenerationCounter_;
    qInfo().noquote() << "[session conn=" + connectionId + "] close requested generation advanced to"
                      << sessionGenerationByConnection_.value(connectionId);
    if (sessionsByConnection_.contains(connectionId)) {
      auto* session = sessionsByConnection_.take(connectionId);
      if (session != nullptr) {
        session->disconnectSession();
        session->deleteLater();
      }
    }
    clearSessionTrackingForConnection(connectionId);
  }

  sessionTabWidget_->removeTab(index);
  tab->deleteLater();
  ensureWelcomeTab();
  if (closingFullscreenSession) {
    exitSessionFullscreen();
  } else if (isSessionFullscreenActive()) {
    const auto currentId = currentSessionConnectionId();
    if (currentId.has_value()) {
      sessionFullscreenConnectionId_ = currentId.value();
    } else {
      exitSessionFullscreen();
    }
  }
  updateCreateActionAvailability();
}

void MainWindow::clearSessionTrackingForConnection(const QString& connectionId) {
  sessionController_->onSessionClosed(connectionId);
  sessionClipboardEnabledByConnection_.remove(connectionId);
  launchInfoByConnection_.remove(connectionId);
  sessionTabsByConnection_.remove(connectionId);
}

void MainWindow::showTreeContextMenu(const QPoint& pos) {
  const QModelIndex index = folderTreeView_->indexAt(pos);
  if (index.isValid()) {
    folderTreeView_->setCurrentIndex(index);
  }

  const int itemType = index.isValid() ? index.data(kItemTypeRole).toInt() : kItemTypeVaultRoot;
  QMenu menu(this);

  if (itemType == kItemTypeConnection) {
    const QString connectionId = index.data(kItemIdRole).toString();
    const bool active = hasActiveSessionForConnection(connectionId);
    const bool vaultUsable = vaultManager_ != nullptr &&
                             vaultManager_->state() != vaultrdp::core::VaultState::Locked;
    auto* connectAction = menu.addAction("Connect", this, &MainWindow::connectSelectedConnection);
    connectAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connect, this));
    connectAction->setEnabled(vaultUsable);
    auto* disconnectAction = menu.addAction("Disconnect", this, &MainWindow::disconnectSelectedConnection);
    disconnectAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Disconnect, this));
    disconnectAction->setEnabled(active);
    auto* logoffAction = menu.addAction("Logoff", this, [this, connectionId]() {
      QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
      if (tab != nullptr && sessionTabWidget_ != nullptr) {
        const int index = sessionTabWidget_->indexOf(tab);
        if (index >= 0) {
          sessionTabWidget_->setCurrentIndex(index);
        }
      }
      logoffCurrentSession();
    });
    logoffAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Logoff, this));
    logoffAction->setEnabled(active);
    auto* fullscreenAction =
        menu.addAction("Open Full Screen", this, &MainWindow::openSelectedConnectionFullscreen);
    fullscreenAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connect, this));
    fullscreenAction->setEnabled(active || vaultUsable);
    menu.addSeparator();
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
  if (item == nullptr || isReloadingTree_ || treeMutationGuard_) {
    return;
  }

  const int itemType = item->data(kItemTypeRole).toInt();
  const QString itemId = item->data(kItemIdRole).toString();
  if (itemId.trimmed().isEmpty()) {
    return;
  }
  if (itemType != kItemTypeFolder && itemType != kItemTypeConnection &&
      itemType != kItemTypeCredential && itemType != kItemTypeGateway) {
    return;
  }
  const QString originalName = item->data(kItemOriginalNameRole).toString().trimmed();
  const QString newName = item->text().trimmed();
  if (newName.isEmpty()) {
    QSignalBlocker blocker(folderTreeModel_);
    item->setText(originalName);
    return;
  }
  if (!originalName.isEmpty() && newName == originalName) {
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
    QSignalBlocker blocker(folderTreeModel_);
    item->setText(originalName);
    return;
  }
  item->setData(newName, kItemOriginalNameRole);
  if (itemType == kItemTypeFolder) {
    QSignalBlocker blocker(folderTreeModel_);
    if (auto* parent = item->parent(); parent != nullptr) {
      parent->sortChildren(0, Qt::AscendingOrder);
    }
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
