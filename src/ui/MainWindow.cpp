#include "ui/MainWindow.hpp"

#include <QAction>
#include <QAbstractItemView>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>

#include "core/DatabaseManager.hpp"
#include "core/VaultManager.hpp"
#include "core/model/Entities.hpp"
#include "core/repository/ConnectionRepository.hpp"
#include "core/repository/CredentialRepository.hpp"
#include "core/repository/GatewayRepository.hpp"
#include "core/repository/Repository.hpp"
#include "core/repository/SecretRepository.hpp"
#include "protocols/ISession.hpp"
#include "protocols/RdpSession.hpp"
#include "ui/FolderTreeView.hpp"
#include "ui/GatewayScope.hpp"
#include "ui/NewConnectionDialog.hpp"
#include "ui/NewGatewayDialog.hpp"
#include "ui/CredentialPromptDialog.hpp"
#include "ui/CredentialScope.hpp"
#include "ui/RootScope.hpp"
#include "ui/SessionRuntimeOptions.hpp"
#include "ui/SessionTabContent.hpp"
#include "ui/TreeItemRoles.hpp"
#include "ui/NewCredentialDialog.hpp"

namespace {
using vaultrdp::ui::kItemFolderIdRole;
using vaultrdp::ui::kItemIdRole;
using vaultrdp::ui::kItemTypeConnection;
using vaultrdp::ui::kItemTypeCredential;
using vaultrdp::ui::kItemTypeFolder;
using vaultrdp::ui::kItemTypeGateway;
using vaultrdp::ui::kItemTypeRole;
using vaultrdp::ui::kItemTypeVaultRoot;
using vaultrdp::ui::gatewayOptionsForFolder;
using vaultrdp::ui::credentialOptionsForFolder;
using vaultrdp::ui::makeSessionRuntimeOptionsJson;
using vaultrdp::ui::parseSessionRuntimeOptions;
using vaultrdp::ui::SessionRuntimeOptions;
}  // namespace

MainWindow::MainWindow(DatabaseManager* databaseManager, vaultrdp::core::VaultManager* vaultManager,
                       QWidget* parent)
    : QMainWindow(parent),
      databaseManager_(databaseManager),
      vaultManager_(vaultManager),
      repository_(std::make_unique<vaultrdp::core::repository::Repository>(databaseManager_)),
      connectionRepository_(std::make_unique<vaultrdp::core::repository::ConnectionRepository>(databaseManager_)),
      credentialRepository_(std::make_unique<vaultrdp::core::repository::CredentialRepository>(databaseManager_)),
      gatewayRepository_(std::make_unique<vaultrdp::core::repository::GatewayRepository>(databaseManager_)),
      secretRepository_(std::make_unique<vaultrdp::core::repository::SecretRepository>(databaseManager_)),
      folderTreeModel_(nullptr),
      folderTreeView_(nullptr),
      sessionTabWidget_(nullptr),
      newFolderAction_(nullptr),
      newConnectionAction_(nullptr),
      newGatewayAction_(nullptr),
      newCredentialAction_(nullptr),
      connectAction_(nullptr),
      disconnectAction_(nullptr),
      disconnectAllAction_(nullptr),
      lockVaultAction_(nullptr),
      vaultStatusLabel_(nullptr),
      debugModeLabel_(nullptr),
      unlockVaultButton_(nullptr),
      mainToolBar_(nullptr),
      mainSplitter_(nullptr),
      welcomeTab_(nullptr),
      sessionGenerationCounter_(0),
      suppressClipboardEvent_(false),
      ignoreClipboardEventsUntilMs_(0),
      lastClipboardWasRemoteFileUris_(false),
      isReloadingTree_(false) {
  setupUi();
  setupMenuBar();
  setupToolBar();
  reloadFolderTree();
  updateVaultStatus();
  maybeRunFirstStartupEncryptionWizard();
  maybePromptUnlockOnStartup();
}

MainWindow::~MainWindow() = default;

void MainWindow::refreshVaultUi() {
  updateVaultStatus();
}

void MainWindow::setupUi() {
  setWindowTitle("VaultRDP");
  resize(1280, 800);

  auto* centralWidget = new QWidget(this);
  auto* layout = new QHBoxLayout(centralWidget);
  layout->setContentsMargins(0, 0, 0, 0);

  mainSplitter_ = new QSplitter(Qt::Horizontal, centralWidget);
  auto* dragTreeView = new vaultrdp::ui::FolderTreeView(mainSplitter_);
  folderTreeView_ = dragTreeView;
  folderTreeView_->setHeaderHidden(true);
  folderTreeView_->header()->setStretchLastSection(true);
  folderTreeView_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  folderTreeView_->setEditTriggers(QAbstractItemView::EditKeyPressed);
  folderTreeView_->setContextMenuPolicy(Qt::CustomContextMenu);
  folderTreeView_->setSelectionMode(QAbstractItemView::SingleSelection);
  folderTreeView_->setDragEnabled(true);
  folderTreeView_->setAcceptDrops(true);
  folderTreeView_->setDropIndicatorShown(true);
  folderTreeView_->setDragDropMode(QAbstractItemView::InternalMove);
  folderTreeView_->setDefaultDropAction(Qt::MoveAction);
  connect(folderTreeView_, &QTreeView::customContextMenuRequested, this, &MainWindow::showTreeContextMenu);

  folderTreeModel_ = new QStandardItemModel(folderTreeView_);
  folderTreeModel_->setHorizontalHeaderLabels({"VaultRDP"});
  folderTreeView_->setModel(folderTreeModel_);
  connect(folderTreeModel_, &QStandardItemModel::itemChanged, this, &MainWindow::onTreeItemChanged);
  connect(folderTreeView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this](const QModelIndex&, const QModelIndex&) { updateCreateActionAvailability(); });
  dragTreeView->setDropCallback([this](const vaultrdp::ui::FolderTreeView::DragPayload& payload,
                                       const std::optional<QString>& destinationFolderId) {
    const auto scheduleReload = [this]() {
      QTimer::singleShot(0, this, [this]() { reloadFolderTree(); });
    };
    if (isReloadingTree_) {
      return;
    }
    if (payload.itemType != kItemTypeFolder && payload.itemType != kItemTypeConnection &&
        payload.itemType != kItemTypeGateway && payload.itemType != kItemTypeCredential) {
      return;
    }
    if (payload.sourceFolderId == destinationFolderId) {
      return;
    }

    QString moveValidationMessage;
    if (!validateMoveByScopeRules(payload.itemType, payload.itemId, destinationFolderId, &moveValidationMessage)) {
      QMessageBox::warning(this, "Move Blocked", moveValidationMessage);
      scheduleReload();
      return;
    }

    bool ok = false;
    QString failureTitle;
    if (payload.itemType == kItemTypeConnection) {
      ok = connectionRepository_->moveConnectionToFolder(payload.itemId, destinationFolderId);
      failureTitle = "Move Connection";
    } else if (payload.itemType == kItemTypeCredential) {
      ok = credentialRepository_->moveCredentialToFolder(payload.itemId, destinationFolderId);
      failureTitle = "Move Credential Set";
    } else if (payload.itemType == kItemTypeGateway) {
      ok = gatewayRepository_->moveGatewayToFolder(payload.itemId, destinationFolderId);
      failureTitle = "Move Gateway";
    } else if (payload.itemType == kItemTypeFolder) {
      ok = repository_->moveFolderToParent(payload.itemId, destinationFolderId);
      failureTitle = "Move Folder";
    }

    if (!ok) {
      QMessageBox::warning(this, failureTitle, "Move failed.");
    }
    scheduleReload();
  });

  sessionTabWidget_ = new QTabWidget(mainSplitter_);
  sessionTabWidget_->setTabsClosable(true);
  sessionTabWidget_->setMovable(true);
  sessionTabWidget_->setDocumentMode(true);

  welcomeTab_ = new QWidget(sessionTabWidget_);
  auto* emptyLayout = new QVBoxLayout(welcomeTab_);
  auto* emptyLabel = new QLabel("No active sessions. Select a connection and press Enter to connect.", welcomeTab_);
  emptyLabel->setWordWrap(true);
  emptyLayout->addWidget(emptyLabel);
  emptyLayout->addStretch();

  sessionTabWidget_->addTab(welcomeTab_, "Welcome");
  connect(sessionTabWidget_, &QTabWidget::tabCloseRequested, this, &MainWindow::handleTabCloseRequested);
  connect(sessionTabWidget_, &QTabWidget::currentChanged, this, [this](int) { syncClipboardToFocusedSession(); });
  connect(folderTreeView_, &QTreeView::doubleClicked, [this](const QModelIndex& index) {
    const int itemType = index.data(kItemTypeRole).toInt();
    if (itemType == kItemTypeConnection) {
      connectSelectedConnection();
    } else if (itemType == kItemTypeGateway) {
      editSelectedGateway();
    } else if (itemType == kItemTypeCredential) {
      editSelectedCredential();
    }
  });

  if (QGuiApplication::clipboard() != nullptr) {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
      try {
        if (suppressClipboardEvent_) {
          return;
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now < ignoreClipboardEventsUntilMs_) {
          return;
        }

        const auto connectionId = currentSessionConnectionId();
        if (!connectionId.has_value()) {
          return;
        }
        if (!sessionClipboardEnabledByConnection_.value(connectionId.value(), true)) {
          return;
        }

        auto* session = sessionsByConnection_.value(connectionId.value(), nullptr);
        if (session == nullptr) {
          return;
        }

        QString uriList;
        const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData(QClipboard::Clipboard);
        if (mimeData != nullptr && mimeData->hasUrls()) {
          const auto urls = mimeData->urls();
          bool hasFreerdpSyntheticPath = false;
          for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
              const QString localPath = url.toLocalFile();
              if (localPath.startsWith("/tmp/com.freerdp.client.cliprdr")) {
                hasFreerdpSyntheticPath = true;
              }
              uriList.append(url.toString(QUrl::FullyEncoded));
              uriList.append("\r\n");
            }
          }
          if (hasFreerdpSyntheticPath) {
            qInfo().noquote() << "[cliprdr-ui] ignoring synthetic FreeRDP temp paths in local->remote clipboard echo";
            return;
          }
        }
        if (lastClipboardWasRemoteFileUris_ && !lastRemoteClipboardUriList_.isEmpty() &&
            uriList == lastRemoteClipboardUriList_) {
          qInfo().noquote() << "[cliprdr-ui] ignoring repeated remote uri-list clipboard echo";
          return;
        }
        session->setLocalClipboardFileUris(uriList);

        const QString text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
        if (!lastRemoteClipboardUriList_.isEmpty() && uriList == lastRemoteClipboardUriList_ &&
            lastClipboardWasRemoteFileUris_) {
          return;
        }
        if (!lastRemoteClipboardText_.isEmpty() && text == lastRemoteClipboardText_ &&
            !lastClipboardWasRemoteFileUris_) {
          return;
        }
        qInfo().noquote() << "[cliprdr-ui] local clipboard changed chars=" << text.size()
                          << "connection=" << connectionId.value();
        session->setLocalClipboardText(text);
      } catch (...) {
      }
    });
  }

  mainSplitter_->setStretchFactor(0, 1);
  mainSplitter_->setStretchFactor(1, 3);
  mainSplitter_->setSizes({320, 960});

  layout->addWidget(mainSplitter_);
  setCentralWidget(centralWidget);

  auto* status = new QStatusBar(this);
  vaultStatusLabel_ = new QLabel(this);
  status->addPermanentWidget(vaultStatusLabel_);
  debugModeLabel_ = new QLabel(this);
  debugModeLabel_->setText("DEBUG MODE");
  const bool debugMode = QCoreApplication::instance()->property("debugMode").toBool();
  debugModeLabel_->setVisible(debugMode);
  status->addPermanentWidget(debugModeLabel_);

  unlockVaultButton_ = new QPushButton("Unlock Vault", this);
  connect(unlockVaultButton_, &QPushButton::clicked, [this]() {
    ensureVaultUnlocked();
  });
  status->addPermanentWidget(unlockVaultButton_);

  status->showMessage("DB: Ready");
  setStatusBar(status);
  restoreUiSettings();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  disconnectAllSessions();
  persistUiSettings();
  QMainWindow::closeEvent(event);
}

void MainWindow::restoreUiSettings() {
  QSettings settings;

  const QByteArray geometry = settings.value("ui/main_window_geometry").toByteArray();
  if (!geometry.isEmpty()) {
    restoreGeometry(geometry);
  }

  const QList<QVariant> rawSplitterSizes = settings.value("ui/main_splitter_sizes").toList();
  if (mainSplitter_ != nullptr && rawSplitterSizes.size() >= 2) {
    QList<int> sizes;
    sizes.reserve(rawSplitterSizes.size());
    for (const QVariant& value : rawSplitterSizes) {
      sizes.push_back(value.toInt());
    }
    if (!sizes.isEmpty()) {
      mainSplitter_->setSizes(sizes);
    }
  }

  if (settings.value("ui/main_window_maximized", false).toBool()) {
    setWindowState(windowState() | Qt::WindowMaximized);
  }
}

void MainWindow::persistUiSettings() const {
  QSettings settings;
  const bool maximized = isMaximized();
  settings.setValue("ui/main_window_maximized", maximized);

  if (!maximized) {
    settings.setValue("ui/main_window_geometry", saveGeometry());
  }
  if (mainSplitter_ != nullptr) {
    settings.setValue("ui/main_splitter_sizes", QVariant::fromValue(mainSplitter_->sizes()));
  }
}

void MainWindow::updateCreateActionAvailability() {
  const QModelIndex index = folderTreeView_ != nullptr ? folderTreeView_->currentIndex() : QModelIndex();
  const int itemType = index.isValid() ? index.data(kItemTypeRole).toInt() : kItemTypeVaultRoot;
  const bool atVaultLevel = !index.isValid() || itemType == kItemTypeVaultRoot;

  if (newFolderAction_ != nullptr) {
    newFolderAction_->setText(atVaultLevel ? "New Root Folder" : "New Subfolder");
    newFolderAction_->setEnabled(true);
  }
  if (newConnectionAction_ != nullptr) {
    newConnectionAction_->setEnabled(!atVaultLevel);
  }
  if (newCredentialAction_ != nullptr) {
    newCredentialAction_->setEnabled(!atVaultLevel);
  }
  if (newGatewayAction_ != nullptr) {
    newGatewayAction_->setEnabled(!atVaultLevel);
  }
}

bool MainWindow::validateMoveByScopeRules(int itemType, const QString& itemId,
                                          const std::optional<QString>& destinationFolderId,
                                          QString* messageOut) const {
  auto fail = [&](const QString& message) {
    if (messageOut != nullptr) {
      *messageOut = message;
    }
    return false;
  };

  if (itemType != kItemTypeFolder && (!destinationFolderId.has_value() || destinationFolderId->trimmed().isEmpty())) {
    return fail(
        "This item must stay within a root folder. Select a destination root folder or subfolder.");
  }

  const auto folders = repository_->listFolders();
  const auto connections = connectionRepository_->listConnections();
  const auto gateways = gatewayRepository_->listGateways();
  const auto credentials = credentialRepository_->listCredentials();

  QHash<QString, vaultrdp::model::Gateway> gatewayById;
  for (const auto& gateway : gateways) {
    gatewayById.insert(gateway.id, gateway);
  }
  QHash<QString, vaultrdp::model::Credential> credentialById;
  for (const auto& credential : credentials) {
    credentialById.insert(credential.id, credential);
  }

  const auto folderRootMap = vaultrdp::ui::buildFolderRootMap(folders);

  QSet<QString> movedSubtree;
  std::optional<QString> movedFolderNewRoot;
  if (itemType == kItemTypeFolder) {
    movedSubtree.insert(itemId);
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& folder : folders) {
        if (folder.parentId.has_value() && movedSubtree.contains(folder.parentId.value()) &&
            !movedSubtree.contains(folder.id)) {
          movedSubtree.insert(folder.id);
          changed = true;
        }
      }
    }

    if (destinationFolderId.has_value() && !destinationFolderId->trimmed().isEmpty()) {
      movedFolderNewRoot = vaultrdp::ui::rootForFolder(destinationFolderId, folderRootMap);
    } else {
      movedFolderNewRoot = itemId;
    }
  }

  const auto destinationRoot = vaultrdp::ui::rootForFolder(destinationFolderId, folderRootMap);
  auto rootForFolderAfterMove = [&](const std::optional<QString>& folderId) -> std::optional<QString> {
    if (!folderId.has_value() || folderId->trimmed().isEmpty()) {
      return std::nullopt;
    }
    if (itemType == kItemTypeFolder && movedFolderNewRoot.has_value() && movedSubtree.contains(folderId.value())) {
      return movedFolderNewRoot;
    }
    return vaultrdp::ui::rootForFolder(folderId, folderRootMap);
  };

  auto rootForConnectionAfterMove = [&](const vaultrdp::model::Connection& connection) -> std::optional<QString> {
    if (itemType == kItemTypeConnection && connection.id == itemId) {
      return destinationRoot;
    }
    return rootForFolderAfterMove(std::optional<QString>(connection.folderId));
  };
  auto rootForGatewayAfterMove = [&](const vaultrdp::model::Gateway& gateway) -> std::optional<QString> {
    if (itemType == kItemTypeGateway && gateway.id == itemId) {
      return destinationRoot;
    }
    return rootForFolderAfterMove(gateway.folderId);
  };
  auto rootForCredentialAfterMove = [&](const vaultrdp::model::Credential& credential) -> std::optional<QString> {
    if (itemType == kItemTypeCredential && credential.id == itemId) {
      return destinationRoot;
    }
    return rootForFolderAfterMove(credential.folderId);
  };

  auto rootsMatchOrGlobal = [](const std::optional<QString>& leftRoot, const std::optional<QString>& rightRoot,
                               bool allowAnywhere) {
    return allowAnywhere || (leftRoot.has_value() && rightRoot.has_value() && leftRoot.value() == rightRoot.value());
  };

  for (const auto& connection : connections) {
    const auto connectionRoot = rootForConnectionAfterMove(connection);
    if (!connectionRoot.has_value()) {
      return fail("Move blocked because it would leave one or more connections outside a root folder.");
    }

    if (connection.gatewayId.has_value()) {
      const auto gatewayIt = gatewayById.find(connection.gatewayId.value());
      if (gatewayIt != gatewayById.end()) {
        const auto gatewayRoot = rootForGatewayAfterMove(gatewayIt.value());
        if (!rootsMatchOrGlobal(connectionRoot, gatewayRoot, gatewayIt->allowAnyFolder)) {
          return fail(
              "Move blocked. This would break folder-scope relationships between connections and referenced "
              "gateways/credential sets. Review linked items and either remove the relationship or enable "
              "\"can be used from any folder\".");
        }
      }
    }

    if (connection.credentialId.has_value()) {
      const auto credentialIt = credentialById.find(connection.credentialId.value());
      if (credentialIt != credentialById.end()) {
        const auto credentialRoot = rootForCredentialAfterMove(credentialIt.value());
        if (!rootsMatchOrGlobal(connectionRoot, credentialRoot, credentialIt->allowAnyFolder)) {
          return fail(
              "Move blocked. This would break folder-scope relationships between connections and referenced "
              "gateways/credential sets. Review linked items and either remove the relationship or enable "
              "\"can be used from any folder\".");
        }
      }
    }
  }

  for (const auto& gateway : gateways) {
    if (!gateway.credentialId.has_value()) {
      continue;
    }
    const auto credentialIt = credentialById.find(gateway.credentialId.value());
    if (credentialIt == credentialById.end()) {
      continue;
    }
    const auto gatewayRoot = rootForGatewayAfterMove(gateway);
    const auto credentialRoot = rootForCredentialAfterMove(credentialIt.value());
    if (!rootsMatchOrGlobal(gatewayRoot, credentialRoot, credentialIt->allowAnyFolder)) {
      return fail(
          "Move blocked. This would break folder-scope relationships between connections and referenced "
          "gateways/credential sets. Review linked items and either remove the relationship or enable "
          "\"can be used from any folder\".");
    }
  }

  return true;
}

void MainWindow::setupMenuBar() {
  auto* fileMenu = menuBar()->addMenu("&File");
  newFolderAction_ = fileMenu->addAction("New Root Folder", this, &MainWindow::createFolder);
  newConnectionAction_ = fileMenu->addAction("New Connection", this, &MainWindow::createConnection);
  newCredentialAction_ = fileMenu->addAction("New Credential Set", this, &MainWindow::createCredential);
  newGatewayAction_ = fileMenu->addAction("New Gateway", this, &MainWindow::createGateway);
  fileMenu->addSeparator();
  fileMenu->addAction("Exit", this, &QWidget::close);

  auto* editMenu = menuBar()->addMenu("&Edit");
  editMenu->addAction("Edit Selected", this, &MainWindow::editSelectedItem);
  editMenu->addAction("Duplicate");
  editMenu->addAction("Delete");
  editMenu->addAction("Rename");

  auto* viewMenu = menuBar()->addMenu("&View");
  viewMenu->addAction("Expand All", folderTreeView_, &QTreeView::expandAll);
  viewMenu->addAction("Collapse All", folderTreeView_, &QTreeView::collapseAll);

  auto* sessionMenu = menuBar()->addMenu("&Session");
  connectAction_ = sessionMenu->addAction("Connect", this, &MainWindow::connectSelectedConnection);
  disconnectAction_ = sessionMenu->addAction("Disconnect", this, &MainWindow::disconnectCurrentSession);
  disconnectAllAction_ = sessionMenu->addAction("Disconnect All", this, &MainWindow::disconnectAllSessions);

  auto* vaultMenu = menuBar()->addMenu("&Vault");
  lockVaultAction_ = vaultMenu->addAction("Lock Vault", [this]() {
    vaultManager_->lock();
    updateVaultStatus();
  });
  vaultMenu->addAction("Vault Settings...", this, &MainWindow::showVaultSettingsDialog);

  auto* helpMenu = menuBar()->addMenu("&Help");
  helpMenu->addAction("About VaultRDP");
}

void MainWindow::setupToolBar() {
  mainToolBar_ = addToolBar("Main");
  mainToolBar_->setMovable(false);

  mainToolBar_->addAction(newFolderAction_);
  mainToolBar_->addAction(newConnectionAction_);
  mainToolBar_->addAction(newCredentialAction_);
  mainToolBar_->addSeparator();
  mainToolBar_->addAction(connectAction_);
  mainToolBar_->addAction(disconnectAction_);
  mainToolBar_->addSeparator();
  mainToolBar_->addAction(lockVaultAction_);
}

void MainWindow::reloadFolderTree() {
  isReloadingTree_ = true;
  folderTreeModel_->removeRows(0, folderTreeModel_->rowCount());

  auto* vaultRoot = new QStandardItem("Vault");
  vaultRoot->setEditable(false);
  vaultRoot->setDragEnabled(false);
  vaultRoot->setDropEnabled(true);
  vaultRoot->setData(kItemTypeVaultRoot, kItemTypeRole);
  vaultRoot->setData(QString(), kItemIdRole);
  vaultRoot->setData(QString(), kItemFolderIdRole);
  folderTreeModel_->appendRow(vaultRoot);

  const auto folders = repository_->listFolders();
  const auto connections = connectionRepository_->listConnections();
  const auto credentials = credentialRepository_->listCredentials();
  const auto gateways = gatewayRepository_->listGateways();

  QHash<QString, QStandardItem*> folderItemById;
  for (const auto& folder : folders) {
    auto* folderItem = new QStandardItem(folder.name);
    folderItem->setEditable(true);
    folderItem->setDragEnabled(true);
    folderItem->setDropEnabled(true);
    folderItem->setData(kItemTypeFolder, kItemTypeRole);
    folderItem->setData(folder.id, kItemIdRole);
    folderItem->setData(folder.id, kItemFolderIdRole);
    folderItemById.insert(folder.id, folderItem);
  }

  for (const auto& folder : folders) {
    QStandardItem* item = folderItemById.value(folder.id, nullptr);
    if (item == nullptr) {
      continue;
    }

    if (folder.parentId.has_value()) {
      QStandardItem* parent = folderItemById.value(folder.parentId.value(), nullptr);
      if (parent != nullptr) {
        parent->appendRow(item);
        continue;
      }
    }
    vaultRoot->appendRow(item);
  }

  for (const auto& connection : connections) {
    auto* item = new QStandardItem(connection.name);
    item->setEditable(true);
    item->setDragEnabled(true);
    item->setDropEnabled(false);
    item->setData(kItemTypeConnection, kItemTypeRole);
    item->setData(connection.id, kItemIdRole);
    item->setData(connection.folderId, kItemFolderIdRole);

    QStandardItem* folderParent = folderItemById.value(connection.folderId, nullptr);
    if (folderParent != nullptr) {
      folderParent->appendRow(item);
    } else {
      vaultRoot->appendRow(item);
    }
  }

  for (const auto& credential : credentials) {
    auto* item = new QStandardItem(credential.name);
    item->setEditable(true);
    item->setDragEnabled(true);
    item->setDropEnabled(false);
    item->setData(kItemTypeCredential, kItemTypeRole);
    item->setData(credential.id, kItemIdRole);
    item->setData(credential.folderId.has_value() ? credential.folderId.value() : QString(), kItemFolderIdRole);
    QStandardItem* folderParent =
        credential.folderId.has_value() ? folderItemById.value(credential.folderId.value(), nullptr) : nullptr;
    if (folderParent != nullptr) {
      folderParent->appendRow(item);
    } else {
      vaultRoot->appendRow(item);
    }
  }

  for (const auto& gateway : gateways) {
    auto* item = new QStandardItem(gateway.name);
    item->setEditable(true);
    item->setDragEnabled(true);
    item->setDropEnabled(false);
    item->setData(kItemTypeGateway, kItemTypeRole);
    item->setData(gateway.id, kItemIdRole);
    item->setData(gateway.folderId.has_value() ? gateway.folderId.value() : QString(), kItemFolderIdRole);
    QStandardItem* folderParent =
        gateway.folderId.has_value() ? folderItemById.value(gateway.folderId.value(), nullptr) : nullptr;
    if (folderParent != nullptr) {
      folderParent->appendRow(item);
    } else {
      vaultRoot->appendRow(item);
    }
  }

  folderTreeView_->expandAll();
  if (folderTreeView_->selectionModel() != nullptr) {
    folderTreeView_->setCurrentIndex(folderTreeModel_->index(0, 0));
  }
  updateCreateActionAvailability();
  isReloadingTree_ = false;
}

void MainWindow::createFolder() {
  qInfo() << "[ui] createFolder requested";
  const QString name = QInputDialog::getText(this, "New Folder", "Folder name:");
  if (name.trimmed().isEmpty()) {
    return;
  }

  const auto created = repository_->createFolder(name, selectedFolderId());
  if (!created.has_value()) {
    QMessageBox::critical(this, "Create Folder", "Failed to create folder.");
    return;
  }

  reloadFolderTree();
}

void MainWindow::createConnection() {
  qInfo() << "[ui] createConnection requested";
  if (!selectedFolderId().has_value()) {
    QMessageBox::information(this, "New Connection", "Select a root folder or subfolder first.");
    return;
  }
  NewConnectionDialog dialog(this);
  const auto folders = repository_->listFolders();
  const auto gateways = gatewayRepository_->listGateways();
  const auto credentials = credentialRepository_->listCredentials();
  const auto credentialOptions = credentialOptionsForFolder(credentials, selectedFolderId(), folders);
  const auto gatewayOptions = gatewayOptionsForFolder(gateways, selectedFolderId(), folders);
  dialog.setCredentialOptions(credentialOptions);
  dialog.setGatewayOptions(gatewayOptions);
  dialog.setInitialValues(QString(), QString(), 3389, QString(), QString(), QString(), true, true, true,
                          std::nullopt);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  if (dialog.connectionName().isEmpty() || dialog.host().isEmpty()) {
    QMessageBox::warning(this, "New Connection", "Connection name and host are required.");
    return;
  }

  std::optional<QString> credentialId;
  const bool hasCredentialFields = !dialog.username().isEmpty() && !dialog.password().isEmpty();
  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    QMessageBox::critical(this, "New Connection", "Failed to start database transaction.");
    return;
  }

  if (dialog.useSavedCredentialSet()) {
    if (!dialog.selectedCredentialSetId().has_value()) {
      db.rollback();
      QMessageBox::warning(this, "New Connection", "Select a saved credential set.");
      return;
    }
    credentialId = dialog.selectedCredentialSetId();
  } else if (dialog.saveCredential() && hasCredentialFields) {
    if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && !ensureVaultUnlocked()) {
      db.rollback();
      QMessageBox::information(this, "Vault", "Vault must be unlocked to save credentials.");
      return;
    }

    const auto maybeSecretId = secretRepository_->createPasswordSecret(dialog.password(), vaultManager_);
    if (!maybeSecretId.has_value()) {
      db.rollback();
      QMessageBox::critical(this, "New Connection", "Failed to store encrypted password.");
      return;
    }

    std::optional<QString> domain;
    if (!dialog.domain().isEmpty()) {
      domain = dialog.domain();
    }

    const auto maybeCredential = credentialRepository_->createCredential(
        dialog.connectionName() + " Credential", dialog.username(), domain, maybeSecretId.value());
    if (!maybeCredential.has_value()) {
      db.rollback();
      QMessageBox::critical(this, "New Connection", "Failed to create credential record.");
      return;
    }

    credentialId = maybeCredential->id;
  }

  const auto created = connectionRepository_->createConnection(
      dialog.connectionName(), dialog.host(), dialog.port(), selectedFolderId(), credentialId,
      dialog.selectedGatewayId(),
      makeSessionRuntimeOptionsJson(dialog.enableClipboard(), dialog.mapHomeDrive()));
  if (!created.has_value()) {
    db.rollback();
    QMessageBox::critical(this, "New Connection", "Failed to create connection.");
    return;
  }
  if (!db.commit()) {
    db.rollback();
    QMessageBox::critical(this, "New Connection", "Failed to commit database transaction.");
    return;
  }

  reloadFolderTree();
}

void MainWindow::createCredential() {
  qInfo() << "[ui] createCredential requested";
  if (!selectedFolderId().has_value()) {
    QMessageBox::information(this, "New Credential Set", "Select a root folder or subfolder first.");
    return;
  }
  NewCredentialDialog dialog(this);
  dialog.setInitialValues(QString(), QString(), QString(), QString(), false);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  if (dialog.credentialName().isEmpty() || dialog.username().isEmpty() || dialog.password().isEmpty()) {
    QMessageBox::warning(this, "New Credential Set", "Name, username, and password are required.");
    return;
  }

  if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && !ensureVaultUnlocked()) {
    return;
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    QMessageBox::critical(this, "New Credential Set", "Failed to start database transaction.");
    return;
  }

  const auto maybeSecretId = secretRepository_->createPasswordSecret(dialog.password(), vaultManager_);
  if (!maybeSecretId.has_value()) {
    db.rollback();
    QMessageBox::critical(this, "New Credential Set", "Failed to store password.");
    return;
  }

  std::optional<QString> domain;
  if (!dialog.domain().isEmpty()) {
    domain = dialog.domain();
  }

  const auto created =
      credentialRepository_->createCredential(dialog.credentialName(), dialog.username(), domain, maybeSecretId.value(),
                                             selectedFolderId(), dialog.allowAnyFolder());
  if (!created.has_value()) {
    db.rollback();
    QMessageBox::critical(this, "New Credential Set", "Failed to create credential set.");
    return;
  }

  if (!db.commit()) {
    db.rollback();
    QMessageBox::critical(this, "New Credential Set", "Failed to commit transaction.");
    return;
  }

  reloadFolderTree();
}

void MainWindow::createGateway() {
  qInfo() << "[ui] createGateway requested";
  if (!selectedFolderId().has_value()) {
    QMessageBox::information(this, "New Gateway", "Select a root folder or subfolder first.");
    return;
  }
  NewGatewayDialog dialog(this);
  const auto folders = repository_->listFolders();
  const auto credentials = credentialRepository_->listCredentials();
  dialog.setCredentialOptions(credentialOptionsForFolder(credentials, selectedFolderId(), folders));
  dialog.setInitialValues(QString(), QString(), 443, vaultrdp::model::GatewayCredentialMode::SameAsConnection,
                          QString(), QString(), QString(), false, std::nullopt);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  if (dialog.gatewayName().isEmpty() || dialog.host().isEmpty()) {
    QMessageBox::warning(this, "New Gateway", "Gateway name and host are required.");
    return;
  }

  const auto mode = dialog.credentialMode();
  std::optional<QString> credentialId;

  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    QMessageBox::critical(this, "New Gateway", "Failed to start database transaction.");
    return;
  }

  if (mode == vaultrdp::model::GatewayCredentialMode::SeparateSaved) {
    if (dialog.useSavedCredentialSet()) {
      if (!dialog.selectedCredentialSetId().has_value()) {
        db.rollback();
        QMessageBox::warning(this, "New Gateway", "Select a saved credential set.");
        return;
      }
      credentialId = dialog.selectedCredentialSetId();
    } else if (dialog.username().isEmpty() || dialog.password().isEmpty()) {
      db.rollback();
      QMessageBox::warning(this, "New Gateway",
                           "Username and password are required for Saved Credentials mode.");
      return;
    } else {
      if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && !ensureVaultUnlocked()) {
        db.rollback();
        return;
      }
      const auto maybeSecretId = secretRepository_->createPasswordSecret(dialog.password(), vaultManager_);
      if (!maybeSecretId.has_value()) {
        db.rollback();
        QMessageBox::critical(this, "New Gateway", "Failed to store gateway password.");
        return;
      }
      std::optional<QString> domain;
      if (!dialog.domain().isEmpty()) {
        domain = dialog.domain();
      }
      const auto maybeCredential =
          credentialRepository_->createCredential(dialog.gatewayName() + " Gateway Credential",
                                                  dialog.username(), domain, maybeSecretId.value());
      if (!maybeCredential.has_value()) {
        db.rollback();
        QMessageBox::critical(this, "New Gateway", "Failed to create gateway credential.");
        return;
      }
      credentialId = maybeCredential->id;
    }
  }

  const auto created = gatewayRepository_->createGateway(dialog.gatewayName(), dialog.host(), dialog.port(), mode,
                                                         credentialId, selectedFolderId(),
                                                         dialog.allowAnyFolder());
  if (!created.has_value()) {
    db.rollback();
    QMessageBox::critical(this, "New Gateway", "Failed to create gateway.");
    return;
  }
  if (!db.commit()) {
    db.rollback();
    QMessageBox::critical(this, "New Gateway", "Failed to commit transaction.");
    return;
  }

  reloadFolderTree();
}

void MainWindow::editSelectedConnection() {
  qInfo() << "[ui] editSelectedConnection requested";
  const auto connectionId = selectedConnectionId();
  if (!connectionId.has_value()) {
    return;
  }

  const auto maybeConnection = connectionRepository_->findConnectionById(connectionId.value());
  if (!maybeConnection.has_value()) {
    QMessageBox::warning(this, "Edit Connection", "Connection was not found.");
    return;
  }
  const auto& connection = maybeConnection.value();

  std::optional<vaultrdp::core::repository::ConnectionLaunchInfo> launchInfo =
      connectionRepository_->resolveLaunchInfo(connection.id, vaultManager_);
  const SessionRuntimeOptions sessionOptions = parseSessionRuntimeOptions(connection.optionsJson);

  NewConnectionDialog dialog(this);
  const auto folders = repository_->listFolders();
  const auto gateways = gatewayRepository_->listGateways();
  const auto credentials = credentialRepository_->listCredentials();
  const std::optional<QString> editFolderId =
      connection.folderId.trimmed().isEmpty() ? std::nullopt : std::optional<QString>(connection.folderId);
  const auto credentialOptions = credentialOptionsForFolder(credentials, editFolderId, folders, connection.credentialId);
  const auto gatewayOptions = gatewayOptionsForFolder(gateways, editFolderId, folders, connection.gatewayId);
  dialog.setCredentialOptions(credentialOptions);
  dialog.setGatewayOptions(gatewayOptions);
  dialog.setDialogTitle("Edit Connection");
  dialog.setInitialValues(connection.name, connection.host, connection.port,
                          launchInfo.has_value() && launchInfo->username.has_value() ? launchInfo->username.value()
                                                                                      : QString(),
                          launchInfo.has_value() && launchInfo->domain.has_value() ? launchInfo->domain.value()
                                                                                    : QString(),
                          launchInfo.has_value() && launchInfo->password.has_value() ? launchInfo->password.value()
                                                                                      : QString(),
                          connection.credentialId.has_value(),
                          sessionOptions.enableClipboard, sessionOptions.mapHomeDrive,
                          connection.gatewayId, connection.credentialId);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  if (dialog.connectionName().isEmpty() || dialog.host().isEmpty()) {
    QMessageBox::warning(this, "Edit Connection", "Connection name and host are required.");
    return;
  }

  std::optional<QString> credentialId = connection.credentialId;
  std::optional<vaultrdp::model::Credential> oldCredential;
  if (connection.credentialId.has_value()) {
    oldCredential = credentialRepository_->findCredentialById(connection.credentialId.value());
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    QMessageBox::critical(this, "Edit Connection", "Failed to start database transaction.");
    return;
  }

  const bool hasCredentialFields = !dialog.username().isEmpty() && !dialog.password().isEmpty();
  if (dialog.useSavedCredentialSet()) {
    if (!dialog.selectedCredentialSetId().has_value()) {
      db.rollback();
      QMessageBox::warning(this, "Edit Connection", "Select a saved credential set.");
      return;
    }
    credentialId = dialog.selectedCredentialSetId();
  } else if (!dialog.saveCredential()) {
    credentialId = std::nullopt;
  } else if (hasCredentialFields) {
    if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && !ensureVaultUnlocked()) {
      db.rollback();
      QMessageBox::information(this, "Vault", "Vault must be unlocked to save credentials.");
      return;
    }

    std::optional<QString> domain;
    if (!dialog.domain().isEmpty()) {
      domain = dialog.domain();
    }
    if (oldCredential.has_value()) {
      if (!secretRepository_->updatePasswordSecret(oldCredential->secretId, dialog.password(), vaultManager_)) {
        db.rollback();
        QMessageBox::critical(this, "Edit Connection", "Failed to update encrypted password.");
        return;
      }
      if (!credentialRepository_->updateCredential(oldCredential->id, dialog.connectionName() + " Credential",
                                                   dialog.username(), domain)) {
        db.rollback();
        QMessageBox::critical(this, "Edit Connection", "Failed to update credential record.");
        return;
      }
      credentialId = oldCredential->id;
    } else {
      const auto maybeSecretId = secretRepository_->createPasswordSecret(dialog.password(), vaultManager_);
      if (!maybeSecretId.has_value()) {
        db.rollback();
        QMessageBox::critical(this, "Edit Connection", "Failed to store encrypted password.");
        return;
      }

      const auto maybeCredential = credentialRepository_->createCredential(
          dialog.connectionName() + " Credential", dialog.username(), domain, maybeSecretId.value());
      if (!maybeCredential.has_value()) {
        db.rollback();
        QMessageBox::critical(this, "Edit Connection", "Failed to create credential record.");
        return;
      }
      credentialId = maybeCredential->id;
    }
  }

  if (!connectionRepository_->updateConnection(connection.id, dialog.connectionName(), dialog.host(),
                                               dialog.port(), credentialId, dialog.selectedGatewayId(),
                                               makeSessionRuntimeOptionsJson(dialog.enableClipboard(),
                                                                             dialog.mapHomeDrive(),
                                                                             sessionOptions.lastSuccessfulUsername))) {
    db.rollback();
    QMessageBox::warning(this, "Edit Connection", "Failed to update connection.");
    return;
  }

  if (oldCredential.has_value() &&
      (!credentialId.has_value() || credentialId.value() != oldCredential->id)) {
    const int references = credentialRepository_->countCredentialReferences(oldCredential->id);
    if (references == 0) {
      if (!credentialRepository_->deleteCredential(oldCredential->id)) {
        db.rollback();
        QMessageBox::warning(this, "Edit Connection", "Failed to delete old credential.");
        return;
      }
      if (!secretRepository_->deleteSecret(oldCredential->secretId)) {
        db.rollback();
        QMessageBox::warning(this, "Edit Connection", "Failed to delete old secret.");
        return;
      }
    }
  }

  if (!db.commit()) {
    db.rollback();
    QMessageBox::warning(this, "Edit Connection", "Failed to commit database transaction.");
    return;
  }

  reloadFolderTree();
}

void MainWindow::editSelectedItem() {
  if (selectedConnectionId().has_value()) {
    editSelectedConnection();
    return;
  }
  if (selectedCredentialId().has_value()) {
    editSelectedCredential();
    return;
  }
  if (selectedGatewayId().has_value()) {
    editSelectedGateway();
  }
}

void MainWindow::editSelectedCredential() {
  qInfo() << "[ui] editSelectedCredential requested";
  const auto credentialId = selectedCredentialId();
  if (!credentialId.has_value()) {
    return;
  }

  const auto maybeCredential = credentialRepository_->findCredentialById(credentialId.value());
  if (!maybeCredential.has_value()) {
    QMessageBox::warning(this, "Edit Credential Set", "Credential set was not found.");
    return;
  }
  const auto& credential = maybeCredential.value();

  if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && !ensureVaultUnlocked()) {
    return;
  }
  const auto maybePassword = secretRepository_->decryptPasswordSecret(credential.secretId, vaultManager_);
  const QString existingPassword = maybePassword.has_value() ? maybePassword.value() : QString();

  NewCredentialDialog dialog(this);
  dialog.setDialogTitle("Edit Credential Set");
  dialog.setInitialValues(credential.name, credential.username, credential.domain.value_or(QString()),
                          existingPassword, credential.allowAnyFolder);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  if (dialog.credentialName().isEmpty() || dialog.username().isEmpty() || dialog.password().isEmpty()) {
    QMessageBox::warning(this, "Edit Credential Set", "Name, username, and password are required.");
    return;
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    QMessageBox::critical(this, "Edit Credential Set", "Failed to start database transaction.");
    return;
  }

  if (!secretRepository_->updatePasswordSecret(credential.secretId, dialog.password(), vaultManager_)) {
    db.rollback();
    QMessageBox::warning(this, "Edit Credential Set", "Failed to update password.");
    return;
  }

  std::optional<QString> domain;
  if (!dialog.domain().isEmpty()) {
    domain = dialog.domain();
  }
  if (!credentialRepository_->updateCredential(credential.id, dialog.credentialName(), dialog.username(), domain,
                                               credential.folderId, dialog.allowAnyFolder())) {
    db.rollback();
    QMessageBox::warning(this, "Edit Credential Set", "Failed to update credential set.");
    return;
  }

  if (!db.commit()) {
    db.rollback();
    QMessageBox::warning(this, "Edit Credential Set", "Failed to commit transaction.");
    return;
  }

  reloadFolderTree();
}

void MainWindow::editSelectedGateway() {
  qInfo() << "[ui] editSelectedGateway requested";
  const auto gatewayId = selectedGatewayId();
  if (!gatewayId.has_value()) {
    return;
  }

  const auto maybeGateway = gatewayRepository_->findGatewayById(gatewayId.value());
  if (!maybeGateway.has_value()) {
    QMessageBox::warning(this, "Edit Gateway", "Gateway was not found.");
    return;
  }

  std::optional<vaultrdp::model::Credential> oldCredential;
  QString existingUsername;
  QString existingDomain;
  QString existingPassword;
  if (maybeGateway->credentialId.has_value()) {
    oldCredential = credentialRepository_->findCredentialById(maybeGateway->credentialId.value());
    if (oldCredential.has_value()) {
      existingUsername = oldCredential->username;
      existingDomain = oldCredential->domain.has_value() ? oldCredential->domain.value() : QString();
      if (vaultManager_->state() == vaultrdp::core::VaultState::Locked) {
        if (!ensureVaultUnlocked()) {
          return;
        }
      }
      const auto maybePassword = secretRepository_->decryptPasswordSecret(oldCredential->secretId, vaultManager_);
      if (maybePassword.has_value()) {
        existingPassword = maybePassword.value();
      }
    }
  }

  NewGatewayDialog dialog(this);
  const auto credentials = credentialRepository_->listCredentials();
  const auto folders = repository_->listFolders();
  dialog.setCredentialOptions(
      credentialOptionsForFolder(credentials, maybeGateway->folderId, folders, maybeGateway->credentialId));
  dialog.setDialogTitle("Edit Gateway");
  dialog.setInitialValues(maybeGateway->name, maybeGateway->host, maybeGateway->port,
                          maybeGateway->credentialMode, existingUsername, existingDomain, existingPassword,
                          maybeGateway->allowAnyFolder, maybeGateway->credentialId);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  if (dialog.gatewayName().isEmpty() || dialog.host().isEmpty()) {
    QMessageBox::warning(this, "Edit Gateway", "Gateway name and host are required.");
    return;
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    QMessageBox::critical(this, "Edit Gateway", "Failed to start database transaction.");
    return;
  }

  const auto newMode = dialog.credentialMode();
  std::optional<QString> newCredentialId = maybeGateway->credentialId;
  if (newMode == vaultrdp::model::GatewayCredentialMode::SeparateSaved) {
    if (dialog.useSavedCredentialSet()) {
      if (!dialog.selectedCredentialSetId().has_value()) {
        db.rollback();
        QMessageBox::warning(this, "Edit Gateway", "Select a saved credential set.");
        return;
      }
      newCredentialId = dialog.selectedCredentialSetId();
    } else if (dialog.username().isEmpty() || dialog.password().isEmpty()) {
      db.rollback();
      QMessageBox::warning(this, "Edit Gateway",
                           "Username and password are required for Saved Credentials mode.");
      return;
    } else {
      if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && !ensureVaultUnlocked()) {
        db.rollback();
        return;
      }
      std::optional<QString> domain;
      if (!dialog.domain().isEmpty()) {
        domain = dialog.domain();
      }
      if (oldCredential.has_value()) {
        if (!secretRepository_->updatePasswordSecret(oldCredential->secretId, dialog.password(), vaultManager_)) {
          db.rollback();
          QMessageBox::warning(this, "Edit Gateway", "Failed to update saved gateway password.");
          return;
        }
        if (!credentialRepository_->updateCredential(oldCredential->id, dialog.gatewayName() + " Gateway Credential",
                                                     dialog.username(), domain)) {
          db.rollback();
          QMessageBox::warning(this, "Edit Gateway", "Failed to update gateway credential.");
          return;
        }
        newCredentialId = oldCredential->id;
      } else {
        const auto maybeSecretId = secretRepository_->createPasswordSecret(dialog.password(), vaultManager_);
        if (!maybeSecretId.has_value()) {
          db.rollback();
          QMessageBox::warning(this, "Edit Gateway", "Failed to store gateway password.");
          return;
        }
        const auto maybeCredential =
            credentialRepository_->createCredential(dialog.gatewayName() + " Gateway Credential",
                                                    dialog.username(), domain, maybeSecretId.value());
        if (!maybeCredential.has_value()) {
          db.rollback();
          QMessageBox::warning(this, "Edit Gateway", "Failed to create gateway credential.");
          return;
        }
        newCredentialId = maybeCredential->id;
      }
    }
  } else {
    newCredentialId = std::nullopt;
  }

  if (!gatewayRepository_->updateGateway(maybeGateway->id, dialog.gatewayName(), dialog.host(), dialog.port(),
                                         newMode, newCredentialId, maybeGateway->folderId,
                                         dialog.allowAnyFolder())) {
    db.rollback();
    QMessageBox::warning(this, "Edit Gateway", "Failed to update gateway.");
    return;
  }

  if (oldCredential.has_value() &&
      (!newCredentialId.has_value() || newCredentialId.value() != oldCredential->id)) {
    const int refs = credentialRepository_->countCredentialReferences(oldCredential->id);
    if (refs == 0) {
      if (!credentialRepository_->deleteCredential(oldCredential->id) ||
          !secretRepository_->deleteSecret(oldCredential->secretId)) {
        db.rollback();
        QMessageBox::warning(this, "Edit Gateway", "Failed to clean up old gateway credential.");
        return;
      }
    }
  }

  if (!db.commit()) {
    db.rollback();
    QMessageBox::warning(this, "Edit Gateway", "Failed to commit transaction.");
    return;
  }

  reloadFolderTree();
}

void MainWindow::showVaultSettingsDialog() {
  QDialog dialog(this);
  dialog.setWindowTitle("Vault Settings");
  dialog.resize(560, 260);

  auto* layout = new QVBoxLayout(&dialog);

  auto* encryptionStatus = new QLabel(&dialog);
  auto* secretsCountLabel = new QLabel(&dialog);
  auto* credentialsCountLabel = new QLabel(&dialog);
  auto* connectionsCountLabel = new QLabel(&dialog);
  auto* dbPathLabel = new QLabel(&dialog);
  dbPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

  const QString dbPath = databaseManager_->databasePath();
  dbPathLabel->setText(dbPath);

  auto refreshStats = [this, encryptionStatus, secretsCountLabel, credentialsCountLabel, connectionsCountLabel]() {
    const char* statusText = "Unknown";
    switch (vaultManager_->state()) {
      case vaultrdp::core::VaultState::Disabled:
        statusText = "Disabled (passwords stored unencrypted)";
        break;
      case vaultrdp::core::VaultState::Locked:
        statusText = "Enabled (locked)";
        break;
      case vaultrdp::core::VaultState::Unlocked:
        statusText = "Enabled (unlocked)";
        break;
    }
    encryptionStatus->setText(QString("Encryption: %1").arg(statusText));

    QSqlDatabase db = databaseManager_->database();
    auto countTable = [&db](const char* table) -> int {
      QSqlQuery q(db);
      if (!q.exec(QString("SELECT COUNT(1) FROM %1").arg(table)) || !q.next()) {
        return 0;
      }
      return q.value(0).toInt();
    };
    secretsCountLabel->setText(QString("Secrets: %1").arg(countTable("secrets")));
    credentialsCountLabel->setText(QString("Credentials: %1").arg(countTable("credentials")));
    connectionsCountLabel->setText(QString("Connections: %1").arg(countTable("connections")));
  };

  refreshStats();

  auto* copyPathButton = new QPushButton("Copy Vault Location", &dialog);
  connect(copyPathButton, &QPushButton::clicked, &dialog, [dbPath]() {
    if (QGuiApplication::clipboard() != nullptr) {
      QGuiApplication::clipboard()->setText(dbPath);
    }
  });

  auto* enableEncryptionButton = new QPushButton("Enable Encryption", &dialog);
  auto* disableEncryptionButton = new QPushButton("Disable Encryption", &dialog);
  auto* changePasswordButton = new QPushButton("Change Password", &dialog);

  auto updateButtons = [this, enableEncryptionButton, disableEncryptionButton, changePasswordButton]() {
    const bool isDisabled = vaultManager_->state() == vaultrdp::core::VaultState::Disabled;
    enableEncryptionButton->setEnabled(isDisabled);
    disableEncryptionButton->setEnabled(!isDisabled);
    changePasswordButton->setEnabled(!isDisabled);
  };
  updateButtons();

  connect(enableEncryptionButton, &QPushButton::clicked, &dialog, [this, &dialog, refreshStats, updateButtons]() {
    bool ok = false;
    const QString passphrase = QInputDialog::getText(&dialog, "",
                                                     "Set vault password (encrypts database secrets):",
                                                     QLineEdit::Password, QString(), &ok);
    if (!ok || passphrase.isEmpty()) {
      return;
    }
    const QString confirm = QInputDialog::getText(&dialog, "", "Confirm vault password:",
                                                  QLineEdit::Password, QString(), &ok);
    if (!ok || confirm.isEmpty()) {
      return;
    }
    if (passphrase != confirm) {
      QMessageBox::warning(&dialog, "Enable Encryption", "Passwords do not match.");
      return;
    }
    if (!vaultManager_->enable(passphrase)) {
      QMessageBox::warning(&dialog, "Enable Encryption",
                           "Failed to enable encryption. Password must satisfy policy.");
      return;
    }
    updateVaultStatus();
    refreshStats();
    updateButtons();
  });

  connect(disableEncryptionButton, &QPushButton::clicked, &dialog, [this, &dialog, refreshStats, updateButtons]() {
    if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && !ensureVaultUnlocked()) {
      return;
    }
    if (QMessageBox::warning(&dialog, "Disable Encryption",
                             "Disabling encryption will store passwords in the database unencrypted.\n\n"
                             "This is not recommended.\n\nContinue?",
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
      return;
    }
    if (!vaultManager_->disable()) {
      QMessageBox::warning(&dialog, "Disable Encryption", "Failed to disable encryption.");
      return;
    }
    updateVaultStatus();
    refreshStats();
    updateButtons();
  });

  connect(changePasswordButton, &QPushButton::clicked, &dialog, [this, &dialog, refreshStats, updateButtons]() {
    if (vaultManager_->state() == vaultrdp::core::VaultState::Disabled) {
      return;
    }
    if (vaultManager_->state() == vaultrdp::core::VaultState::Locked && !ensureVaultUnlocked()) {
      return;
    }

    while (true) {
      bool ok = false;
      const QString oldPass = QInputDialog::getText(&dialog, "", "Current password:", QLineEdit::Password,
                                                    QString(), &ok);
      if (!ok || oldPass.isEmpty()) {
        return;
      }
      const QString newPass = QInputDialog::getText(&dialog, "", "New password:", QLineEdit::Password, QString(),
                                                    &ok);
      if (!ok || newPass.isEmpty()) {
        return;
      }
      const QString confirm = QInputDialog::getText(&dialog, "", "Confirm new password:", QLineEdit::Password,
                                                    QString(), &ok);
      if (!ok || confirm.isEmpty()) {
        return;
      }
      if (newPass != confirm) {
        QMessageBox::warning(&dialog, "Change Password", "New passwords do not match.");
        continue;
      }
      if (!vaultManager_->rotatePassphrase(oldPass, newPass)) {
        QMessageBox::warning(&dialog, "Change Password",
                             "Failed to change password. Check current password and policy requirements.");
        return;
      }
      QMessageBox::information(&dialog, "Change Password", "Vault password changed.");
      updateVaultStatus();
      refreshStats();
      updateButtons();
      return;
    }
  });

  layout->addWidget(encryptionStatus);
  layout->addWidget(connectionsCountLabel);
  layout->addWidget(credentialsCountLabel);
  layout->addWidget(secretsCountLabel);
  layout->addSpacing(8);
  layout->addWidget(new QLabel("Vault Location:", &dialog));
  layout->addWidget(dbPathLabel);
  layout->addWidget(copyPathButton);
  layout->addSpacing(8);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->addWidget(enableEncryptionButton);
  buttonRow->addWidget(disableEncryptionButton);
  buttonRow->addWidget(changePasswordButton);
  buttonRow->addStretch();
  layout->addLayout(buttonRow);

  auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(closeButtons);

  dialog.exec();
}

bool MainWindow::ensureVaultUnlocked() {
  if (vaultManager_->state() == vaultrdp::core::VaultState::Unlocked) {
    return true;
  }

  if (vaultManager_->state() == vaultrdp::core::VaultState::Disabled) {
    bool ok = false;
    const QString passphrase = QInputDialog::getText(this, "", "Set master passphrase:",
                                                     QLineEdit::Password, QString(), &ok);
    if (!ok || passphrase.isEmpty()) {
      return false;
    }

    const QString confirm = QInputDialog::getText(this, "", "Confirm master passphrase:",
                                                  QLineEdit::Password, QString(), &ok);
    if (!ok || confirm.isEmpty()) {
      return false;
    }

    if (passphrase != confirm) {
      QMessageBox::warning(this, "Enable Vault", "Passphrases do not match.");
      return false;
    }

    if (!vaultManager_->enable(passphrase)) {
      QMessageBox::warning(this, "Enable Vault",
                           "Failed to enable vault. Passphrase must include uppercase, lowercase, and number.");
      updateVaultStatus();
      return false;
    }

    updateVaultStatus();
    return true;
  }

  while (true) {
    bool ok = false;
    const QString passphrase =
        QInputDialog::getText(this, "", "Master passphrase:", QLineEdit::Password, QString(), &ok);
    if (!ok || passphrase.isEmpty()) {
      return false;
    }

    if (vaultManager_->unlock(passphrase)) {
      updateVaultStatus();
      return true;
    }

    QMessageBox::warning(this, "Unlock Vault", "Incorrect passphrase.");
    updateVaultStatus();
  }
}

void MainWindow::updateVaultStatus() {
  switch (vaultManager_->state()) {
    case vaultrdp::core::VaultState::Disabled:
      vaultStatusLabel_->setText("Vault: Disabled");
      break;
    case vaultrdp::core::VaultState::Locked:
      vaultStatusLabel_->setText("Vault: Locked");
      break;
    case vaultrdp::core::VaultState::Unlocked:
      vaultStatusLabel_->setText("Vault: Unlocked");
      break;
  }

  const bool encryptionEnabled = vaultManager_->isEnabled();
  const bool vaultUnlocked = vaultManager_->state() == vaultrdp::core::VaultState::Unlocked;
  lockVaultAction_->setVisible(true);

  if (!encryptionEnabled) {
    lockVaultAction_->setEnabled(false);
  } else {
    lockVaultAction_->setEnabled(vaultUnlocked);
  }
  connectAction_->setEnabled(vaultManager_->state() == vaultrdp::core::VaultState::Unlocked);
  disconnectAction_->setEnabled(vaultManager_->state() == vaultrdp::core::VaultState::Unlocked);
  disconnectAllAction_->setEnabled(vaultManager_->state() == vaultrdp::core::VaultState::Unlocked);
  applyVaultUiState();
}

void MainWindow::applyVaultUiState() {
  const bool locked = vaultManager_->state() == vaultrdp::core::VaultState::Locked;

  if (centralWidget() != nullptr) {
    centralWidget()->setEnabled(!locked);
  }
  if (menuBar() != nullptr) {
    menuBar()->setEnabled(!locked);
  }
  if (mainToolBar_ != nullptr) {
    mainToolBar_->setEnabled(!locked);
  }
  if (unlockVaultButton_ != nullptr) {
    unlockVaultButton_->setVisible(locked);
    unlockVaultButton_->setEnabled(locked);
  }
}

void MainWindow::maybeRunFirstStartupEncryptionWizard() {
  QSettings settings;
  const QFileInfo dbInfo(databaseManager_->databasePath());
  const QString dbInstanceKey =
      QString("vault/first_startup_encryption_wizard_seen/%1/%2")
          .arg(dbInfo.absoluteFilePath(),
               QString::number(dbInfo.lastModified().toSecsSinceEpoch()));
  if (settings.value(dbInstanceKey, false).toBool()) {
    return;
  }

  settings.setValue(dbInstanceKey, true);

  if (vaultManager_->state() != vaultrdp::core::VaultState::Disabled) {
    return;
  }

  QTimer::singleShot(0, this, [this]() {
    while (true) {
      QMessageBox wizard(this);
      wizard.setWindowTitle("Encryption Setup");
      wizard.setIcon(QMessageBox::Information);
      wizard.setText("Protect saved passwords with vault encryption.");
      wizard.setInformativeText(
          "Vault encryption uses a master password to encrypt secrets in the database.\n\n"
          "If you skip this step, saved passwords will be stored unencrypted (not recommended).\n\n"
          "You can change this later from Vault -> Vault Settings.");
      QPushButton* enableButton = wizard.addButton("Enable Encryption (Recommended)", QMessageBox::AcceptRole);
      QPushButton* skipButton = wizard.addButton("Skip for Now", QMessageBox::RejectRole);
      wizard.setDefaultButton(enableButton);
      wizard.exec();

      if (wizard.clickedButton() != enableButton) {
        Q_UNUSED(skipButton);
        updateVaultStatus();
        return;
      }

      bool ok = false;
      const QString passphrase = QInputDialog::getText(this, "", "Set vault password:", QLineEdit::Password,
                                                       QString(), &ok);
      if (!ok || passphrase.isEmpty()) {
        // Back to Enable/Skip prompt.
        continue;
      }

      const QString confirm = QInputDialog::getText(this, "", "Confirm vault password:", QLineEdit::Password,
                                                    QString(), &ok);
      if (!ok || confirm.isEmpty()) {
        // Back to Enable/Skip prompt.
        continue;
      }

      if (passphrase != confirm) {
        QMessageBox::warning(this, "Enable Encryption", "Passwords do not match.");
        // Back to Enable/Skip prompt.
        continue;
      }

      if (!vaultManager_->enable(passphrase)) {
        QMessageBox::warning(this, "Enable Encryption",
                             "Failed to enable encryption. Password must satisfy policy.");
      }
      updateVaultStatus();
      return;
    }
  });
}

void MainWindow::maybePromptUnlockOnStartup() {
  if (vaultManager_->state() != vaultrdp::core::VaultState::Locked) {
    return;
  }

  QTimer::singleShot(0, this, [this]() {
    ensureVaultUnlocked();
  });
}


void MainWindow::addSessionTab(const vaultrdp::core::repository::ConnectionLaunchInfo& launchInfo) {
  const auto& connection = launchInfo.connection;
  const quint64 sessionGeneration = ++sessionGenerationCounter_;
  sessionGenerationByConnection_[connection.id] = sessionGeneration;
  qInfo().noquote() << "[session conn=" + connection.id + " gen=" + QString::number(sessionGeneration) +
                           "] addSessionTab name="
                    << connection.name;

  auto* sessionWidget = new vaultrdp::ui::SessionTabContent(connection.id, sessionTabWidget_);
  sessionWidget->setProperty("connection_id", connection.id);

  const int tabIndex = sessionTabWidget_->addTab(sessionWidget, connection.name);
  sessionTabWidget_->setCurrentIndex(tabIndex);
  sessionTabsByConnection_.insert(connection.id, sessionWidget);
  launchInfoByConnection_.insert(connection.id, launchInfo);
  reconnectAttemptsByConnection_.insert(connection.id, 0);
  hasEverConnectedByConnection_.insert(connection.id, false);
  authPromptActiveByConnection_.insert(connection.id, false);
  lastAuthFailureWasGatewayByConnection_.insert(connection.id, false);
  autoReconnectArmedByConnection_.insert(connection.id, false);
  const SessionRuntimeOptions sessionOptions = parseSessionRuntimeOptions(connection.optionsJson);
  sessionClipboardEnabledByConnection_.insert(connection.id, sessionOptions.enableClipboard);

  const QSize viewport = sessionWidget->viewportSize();
  auto* session = new vaultrdp::protocols::RdpSession(connection.host, connection.port, launchInfo.username,
                                                      launchInfo.domain, launchInfo.password,
                                                      launchInfo.gatewayHost, launchInfo.gatewayPort,
                                                      launchInfo.gatewayUsername, launchInfo.gatewayDomain,
                                                      launchInfo.gatewayPassword,
                                                      launchInfo.gatewayCredentialMode ==
                                                          vaultrdp::model::GatewayCredentialMode::SameAsConnection,
                                                      viewport.width(),
                                                      viewport.height(), sessionOptions.enableClipboard,
                                                      sessionOptions.mapHomeDrive,
                                                      this);
  sessionsByConnection_.insert(connection.id, session);

  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::keyInput, this,
          [session](int qtKey, quint32 nativeScanCode, bool pressed) {
            session->sendKeyInput(qtKey, nativeScanCode, pressed);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::mouseMoveInput, this,
          [session](int x, int y) { session->sendMouseMove(x, y); });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::mouseButtonInput, this,
          [session](Qt::MouseButton button, bool pressed, int x, int y) {
            session->sendMouseButton(button, pressed, x, y);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::wheelInput, this,
          [session](Qt::Orientation orientation, int delta, int x, int y) {
            session->sendWheel(orientation, delta, x, y);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::viewportResizeRequested, this,
          [session](int width, int height) { session->resizeSession(width, height); });

  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::reconnectRequested, this,
          [this, sessionGeneration](const QString& connectionId) {
            auto* reconnectSession = sessionsByConnection_.value(connectionId, nullptr);
            if (reconnectSession == nullptr) {
              return;
            }
            if (sessionGenerationByConnection_.value(connectionId, 0) != sessionGeneration) {
              qInfo().noquote() << "[session conn=" + connectionId + " gen=" + QString::number(sessionGeneration) +
                                       "] stale reconnect request ignored";
              return;
            }
            blockAutoReconnectByConnection_[connectionId] = false;
            authFailurePromptCountByConnection_[connectionId] = 0;
            lastAuthFailureWasGatewayByConnection_[connectionId] = false;
            lastAuthPromptMsByConnection_[connectionId] = 0;
            reconnectAttemptsByConnection_[connectionId] = 0;
            autoReconnectArmedByConnection_[connectionId] = false;
            reconnectSession->connectSession();
          });

  connect(session, &vaultrdp::protocols::ISession::stateChanged, this,
          [this, connectionId = connection.id, sessionGeneration](vaultrdp::protocols::SessionState state) {
            if (sessionGenerationByConnection_.value(connectionId, 0) != sessionGeneration) {
              qInfo().noquote() << "[session conn=" + connectionId + " gen=" + QString::number(sessionGeneration) +
                                       "] ignoring stale stateChanged";
              return;
            }
            updateSessionTabState(connectionId, state);

            if (state == vaultrdp::protocols::SessionState::Connected) {
              blockAutoReconnectByConnection_[connectionId] = false;
              authFailurePromptCountByConnection_[connectionId] = 0;
              lastAuthFailureWasGatewayByConnection_[connectionId] = false;
              lastAuthPromptMsByConnection_[connectionId] = 0;
              reconnectAttemptsByConnection_[connectionId] = 0;
              hasEverConnectedByConnection_[connectionId] = true;
              autoReconnectArmedByConnection_[connectionId] = false;

              const auto infoIt = launchInfoByConnection_.find(connectionId);
              if (infoIt != launchInfoByConnection_.end() && infoIt->username.has_value() &&
                  !infoIt->username->trimmed().isEmpty()) {
                SessionRuntimeOptions options =
                    parseSessionRuntimeOptions(infoIt->connection.optionsJson);
                options.lastSuccessfulUsername = infoIt->username.value().trimmed();
                const QString newOptionsJson = makeSessionRuntimeOptionsJson(
                    options.enableClipboard, options.mapHomeDrive, options.lastSuccessfulUsername);
                if (newOptionsJson != infoIt->connection.optionsJson &&
                    connectionRepository_->updateConnectionOptionsJson(connectionId, newOptionsJson)) {
                  infoIt->connection.optionsJson = newOptionsJson;
                }
              }
              return;
            }

            if (state == vaultrdp::protocols::SessionState::Disconnected &&
                hasEverConnectedByConnection_.value(connectionId, false)) {
              if (blockAutoReconnectByConnection_.value(connectionId, false)) {
                return;
              }
              if (!autoReconnectArmedByConnection_.value(connectionId, false)) {
                return;
              }
              const int attempts = reconnectAttemptsByConnection_.value(connectionId, 0);
              if (attempts >= 3) {
                return;
              }

              reconnectAttemptsByConnection_[connectionId] = attempts + 1;
              QTimer::singleShot(1500, this, [this, connectionId, sessionGeneration]() {
                if (sessionGenerationByConnection_.value(connectionId, 0) != sessionGeneration) {
                  qInfo().noquote() << "[session conn=" + connectionId + " gen=" +
                                           QString::number(sessionGeneration) +
                                           "] stale reconnect timer skipped";
                  return;
                }
                auto* reconnectSession = sessionsByConnection_.value(connectionId, nullptr);
                if (reconnectSession == nullptr) {
                  return;
                }

                const auto currentState = reconnectSession->state();
                if (currentState == vaultrdp::protocols::SessionState::Connected ||
                    currentState == vaultrdp::protocols::SessionState::Connecting) {
                  return;
                }
                autoReconnectArmedByConnection_[connectionId] = false;
                reconnectSession->connectSession();
              });
            }
          });
  connect(session, &vaultrdp::protocols::ISession::errorOccurred, this,
          [this, connectionId = connection.id, sessionGeneration](const QString& message) {
            if (sessionGenerationByConnection_.value(connectionId, 0) != sessionGeneration) {
              qInfo().noquote() << "[session conn=" + connectionId + " gen=" + QString::number(sessionGeneration) +
                                       "] ignoring stale error event";
              return;
            }
            QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
            auto* content = qobject_cast<vaultrdp::ui::SessionTabContent*>(tab);
            if (content != nullptr) {
              content->setErrorText(message);
            }

            if (!isAuthenticationFailureMessage(message)) {
              const QString lowered = message.toLower();
              const bool networkLikeFailure =
                  lowered.contains("transport failed") || lowered.contains("network") ||
                  lowered.contains("proxy_ts_connectfailed") || lowered.contains("proxy connect failed") ||
                  lowered.contains("dns") || lowered.contains("tls");
              if (networkLikeFailure) {
                autoReconnectArmedByConnection_[connectionId] = true;
              }
              return;
            }
            const bool gatewayAuthFailure = isGatewayAuthenticationFailureMessage(message);
            lastAuthFailureWasGatewayByConnection_[connectionId] = gatewayAuthFailure;
            blockAutoReconnectByConnection_[connectionId] = true;
            autoReconnectArmedByConnection_[connectionId] = false;

            const int promptCount = authFailurePromptCountByConnection_.value(connectionId, 0) + 1;
            authFailurePromptCountByConnection_[connectionId] = promptCount;
            if (promptCount > 3) {
              QWidget* retryTab = sessionTabsByConnection_.value(connectionId, nullptr);
              if (retryTab != nullptr) {
                const int index = sessionTabWidget_->indexOf(retryTab);
                if (index >= 0) {
                  handleTabCloseRequested(index);
                }
              }
              return;
            }
            if (authPromptActiveByConnection_.value(connectionId, false)) {
              return;
            }

            authPromptActiveByConnection_[connectionId] = true;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const int baseDelayMs = qMin(5000, 1000 * promptCount);
            const qint64 earliest = lastAuthPromptMsByConnection_.value(connectionId, 0) + baseDelayMs;
            const int delayMs = static_cast<int>(qMax<qint64>(0, earliest - now));
            QTimer::singleShot(delayMs, this, [this, connectionId, sessionGeneration]() {
              if (sessionGenerationByConnection_.value(connectionId, 0) != sessionGeneration) {
                qInfo().noquote() << "[session conn=" + connectionId + " gen=" +
                                         QString::number(sessionGeneration) +
                                         "] stale auth prompt timer skipped";
                authPromptActiveByConnection_[connectionId] = false;
                return;
              }
              const auto infoIt = launchInfoByConnection_.find(connectionId);
              if (infoIt == launchInfoByConnection_.end()) {
                authPromptActiveByConnection_[connectionId] = false;
                return;
              }

              auto retryInfo = infoIt.value();
              const bool gatewayCredsIndependent =
                  retryInfo.gatewayHost.has_value() &&
                  retryInfo.gatewayCredentialMode !=
                      vaultrdp::model::GatewayCredentialMode::SameAsConnection;
              const bool gatewayAuthFailureNow =
                  lastAuthFailureWasGatewayByConnection_.value(connectionId, false);
              const bool promptGatewayCreds = gatewayAuthFailureNow && retryInfo.gatewayHost.has_value();
              std::optional<QString> enteredUsername;
              std::optional<QString> enteredDomain;
              std::optional<QString> enteredPassword;
              lastAuthPromptMsByConnection_[connectionId] = QDateTime::currentMSecsSinceEpoch();
              const std::optional<QString> suggestedUsername =
                  promptGatewayCreds ? retryInfo.gatewayUsername : retryInfo.username;
              const std::optional<QString> suggestedDomain =
                  promptGatewayCreds ? retryInfo.gatewayDomain : retryInfo.domain;
              const bool accepted =
                  promptForCredentials(suggestedUsername, suggestedDomain, &enteredUsername, &enteredDomain,
                                       &enteredPassword, promptGatewayCreds);
              authPromptActiveByConnection_[connectionId] = false;
              if (!accepted) {
                QWidget* retryTab = sessionTabsByConnection_.value(connectionId, nullptr);
                if (retryTab != nullptr) {
                  const int index = sessionTabWidget_->indexOf(retryTab);
                  if (index >= 0) {
                    handleTabCloseRequested(index);
                  }
                }
                return;
              }

              const int promptCountSnapshot = authFailurePromptCountByConnection_.value(connectionId, 0);
              const bool blockSnapshot = blockAutoReconnectByConnection_.value(connectionId, false);
              const qint64 promptMsSnapshot = lastAuthPromptMsByConnection_.value(connectionId, 0);
              if (promptGatewayCreds && gatewayCredsIndependent) {
                retryInfo.gatewayUsername = enteredUsername;
                retryInfo.gatewayDomain = enteredDomain;
                retryInfo.gatewayPassword = enteredPassword;
              } else if (promptGatewayCreds) {
                retryInfo.gatewayUsername = enteredUsername;
                retryInfo.gatewayDomain = enteredDomain;
                retryInfo.gatewayPassword = enteredPassword;
                retryInfo.username = enteredUsername;
                retryInfo.domain = enteredDomain;
                retryInfo.password = enteredPassword;
              } else {
                retryInfo.username = enteredUsername;
                retryInfo.domain = enteredDomain;
                retryInfo.password = enteredPassword;
              }

              QWidget* retryTab = sessionTabsByConnection_.value(connectionId, nullptr);
              if (retryTab != nullptr) {
                const int index = sessionTabWidget_->indexOf(retryTab);
                if (index >= 0) {
                  handleTabCloseRequested(index);
                }
              }
              addSessionTab(retryInfo);
              authFailurePromptCountByConnection_[connectionId] = promptCountSnapshot;
              blockAutoReconnectByConnection_[connectionId] = blockSnapshot;
              lastAuthPromptMsByConnection_[connectionId] = promptMsSnapshot;
            });
          });
  connect(session, &vaultrdp::protocols::RdpSession::frameUpdated, this,
          [this, connectionId = connection.id](const QImage& frame) {
            QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
            auto* content = qobject_cast<vaultrdp::ui::SessionTabContent*>(tab);
            if (content != nullptr) {
              content->setFrame(frame);
            }
          });
  connect(session, &vaultrdp::protocols::RdpSession::remoteClipboardText, this,
          [this, connectionId = connection.id](const QString& text) {
            try {
              const auto currentId = currentSessionConnectionId();
              if (!currentId.has_value() || currentId.value() != connectionId ||
                  QGuiApplication::clipboard() == nullptr) {
                return;
              }
              if (!sessionClipboardEnabledByConnection_.value(connectionId, true)) {
                return;
              }

              qInfo().noquote() << "[cliprdr-ui] applying remote clipboard chars=" << text.size()
                                << "connection=" << connectionId;
              suppressClipboardEvent_ = true;
              lastRemoteClipboardText_ = text;
              lastRemoteClipboardUriList_.clear();
              lastClipboardWasRemoteFileUris_ = false;
              ignoreClipboardEventsUntilMs_ = QDateTime::currentMSecsSinceEpoch() + 1500;
              QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
              suppressClipboardEvent_ = false;
            } catch (...) {
              suppressClipboardEvent_ = false;
            }
          });
  connect(session, &vaultrdp::protocols::RdpSession::remoteClipboardFileUris, this,
          [this, connectionId = connection.id](const QString& uriList) {
            try {
              const auto currentId = currentSessionConnectionId();
              if (!currentId.has_value() || currentId.value() != connectionId ||
                  QGuiApplication::clipboard() == nullptr) {
                return;
              }
              if (!sessionClipboardEnabledByConnection_.value(connectionId, true)) {
                return;
              }

              QList<QUrl> urls;
              const QStringList lines = uriList.split(QRegularExpression("[\\r\\n]+"),
                                                      Qt::SkipEmptyParts);
              for (const QString& line : lines) {
                const QString trimmed = line.trimmed();
                if (trimmed.isEmpty() || trimmed.startsWith("#")) {
                  continue;
                }
                const QUrl url(trimmed);
                if (url.isValid()) {
                  urls.push_back(url);
                }
              }
              if (urls.isEmpty()) {
                return;
              }

              auto* mime = new QMimeData();
              mime->setUrls(urls);
              lastRemoteClipboardUriList_ = uriList;
              lastRemoteClipboardText_.clear();
              lastClipboardWasRemoteFileUris_ = true;
              ignoreClipboardEventsUntilMs_ = QDateTime::currentMSecsSinceEpoch() + 1500;
              suppressClipboardEvent_ = true;
              QGuiApplication::clipboard()->setMimeData(mime, QClipboard::Clipboard);
              suppressClipboardEvent_ = false;
              qInfo().noquote() << "[cliprdr-ui] applied remote file uri-list urls=" << urls.size()
                                << "connection=" << connectionId;
            } catch (...) {
              suppressClipboardEvent_ = false;
            }
          });
  connect(session, &vaultrdp::protocols::RdpSession::remoteLogoff, this,
          [this, connectionId = connection.id, sessionGeneration]() {
            if (sessionGenerationByConnection_.value(connectionId, 0) != sessionGeneration) {
              return;
            }
            blockAutoReconnectByConnection_[connectionId] = true;
            autoReconnectArmedByConnection_[connectionId] = false;
            QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
            if (tab == nullptr) {
              return;
            }
            const int index = sessionTabWidget_->indexOf(tab);
            if (index >= 0) {
              handleTabCloseRequested(index);
            }
          });

  session->connectSession();
  syncClipboardToFocusedSession();
}

void MainWindow::updateSessionTabState(const QString& connectionId, vaultrdp::protocols::SessionState state) {
  QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
  auto* content = qobject_cast<vaultrdp::ui::SessionTabContent*>(tab);
  if (content == nullptr) {
    return;
  }
  content->setSessionState(state);
}

QString MainWindow::sessionStateLabel(vaultrdp::protocols::SessionState state) const {
  switch (state) {
    case vaultrdp::protocols::SessionState::Initialized:
      return "Initialized";
    case vaultrdp::protocols::SessionState::Connecting:
      return "Connecting";
    case vaultrdp::protocols::SessionState::Connected:
      return "Connected";
    case vaultrdp::protocols::SessionState::Disconnected:
      return "Disconnected";
    case vaultrdp::protocols::SessionState::Error:
      return "Error";
  }
  return "Unknown";
}

void MainWindow::ensureWelcomeTab() {
  if (sessionTabWidget_->count() > 0) {
    for (int i = 0; i < sessionTabWidget_->count(); ++i) {
      if (sessionTabWidget_->widget(i) != welcomeTab_) {
        return;
      }
    }
  }

  if (welcomeTab_ == nullptr) {
    return;
  }

  if (sessionTabWidget_->indexOf(welcomeTab_) < 0) {
    sessionTabWidget_->addTab(welcomeTab_, "Welcome");
  }
  sessionTabWidget_->setCurrentWidget(welcomeTab_);
}

std::optional<QString> MainWindow::currentSessionConnectionId() const {
  if (sessionTabWidget_ == nullptr) {
    return std::nullopt;
  }

  QWidget* current = sessionTabWidget_->currentWidget();
  if (current == nullptr || current == welcomeTab_) {
    return std::nullopt;
  }

  const QString connectionId = current->property("connection_id").toString();
  if (connectionId.isEmpty()) {
    return std::nullopt;
  }
  return connectionId;
}

void MainWindow::syncClipboardToFocusedSession() {
  if (QGuiApplication::clipboard() == nullptr) {
    return;
  }

  const auto connectionId = currentSessionConnectionId();
  if (!connectionId.has_value()) {
    return;
  }
  if (!sessionClipboardEnabledByConnection_.value(connectionId.value(), true)) {
    return;
  }

  auto* session = sessionsByConnection_.value(connectionId.value(), nullptr);
  if (session == nullptr) {
    return;
  }

  session->setLocalClipboardText(QGuiApplication::clipboard()->text(QClipboard::Clipboard));
}

bool MainWindow::promptForCredentials(const std::optional<QString>& suggestedUsername,
                                      const std::optional<QString>& suggestedDomain,
                                      std::optional<QString>* usernameOut, std::optional<QString>* domainOut,
                                      std::optional<QString>* passwordOut, bool forGateway) {
  if (usernameOut == nullptr || domainOut == nullptr || passwordOut == nullptr) {
    return false;
  }

  while (true) {
    vaultrdp::ui::CredentialPromptResult result;
    if (!vaultrdp::ui::promptForCredentials(this, suggestedUsername, suggestedDomain, forGateway, &result)) {
      return false;
    }
    if (!result.username.has_value() || result.username->trimmed().isEmpty() ||
        !result.password.has_value() || result.password->isEmpty()) {
      QMessageBox::warning(this, "Credentials Required", "Username and password are required.");
      continue;
    }
    *usernameOut = result.username;
    *domainOut = result.domain;
    *passwordOut = result.password;
    return true;
  }
}

bool MainWindow::isAuthenticationFailureMessage(const QString& message) const {
  if (message.startsWith("Authentication failed", Qt::CaseInsensitive)) {
    return true;
  }
  if (message.startsWith("Gateway authentication failed", Qt::CaseInsensitive)) {
    return true;
  }
  return message.contains("ACCESS_DENIED", Qt::CaseInsensitive) ||
         message.contains("WRONG_PASSWORD", Qt::CaseInsensitive) ||
         message.contains("LOGON_FAILURE", Qt::CaseInsensitive);
}

bool MainWindow::isGatewayAuthenticationFailureMessage(const QString& message) const {
  if (message.startsWith("Gateway authentication failed", Qt::CaseInsensitive)) {
    return true;
  }
  return message.contains("RDG_", Qt::CaseInsensitive) ||
         message.contains("proxy_ts_connectfailed", Qt::CaseInsensitive) ||
         message.contains("gateway", Qt::CaseInsensitive);
}
