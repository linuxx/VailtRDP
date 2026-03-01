#include "ui/MainWindow.hpp"

#include <QAction>
#include <QAbstractItemView>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
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

#include <functional>

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
#include "ui/NewConnectionDialog.hpp"
#include "ui/NewGatewayDialog.hpp"
#include "ui/SessionTabContent.hpp"

namespace {
constexpr int kItemTypeRole = Qt::UserRole + 100;
constexpr int kItemIdRole = Qt::UserRole + 101;
constexpr int kItemFolderIdRole = Qt::UserRole + 102;

constexpr int kItemTypeFolder = 1;
constexpr int kItemTypeConnection = 2;
constexpr int kItemTypeVaultRoot = 3;
constexpr int kItemTypeGateway = 4;

struct SessionRuntimeOptions {
  bool enableClipboard = true;
  bool mapHomeDrive = true;
  QString lastSuccessfulUsername;
};

SessionRuntimeOptions parseSessionRuntimeOptions(const QString& optionsJson) {
  SessionRuntimeOptions options;
  const QByteArray utf8 = optionsJson.toUtf8();
  if (utf8.trimmed().isEmpty()) {
    return options;
  }

  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(utf8, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return options;
  }

  const QJsonObject obj = doc.object();
  if (obj.contains("enableClipboard") && obj.value("enableClipboard").isBool()) {
    options.enableClipboard = obj.value("enableClipboard").toBool();
  }
  if (obj.contains("mapHomeDrive") && obj.value("mapHomeDrive").isBool()) {
    options.mapHomeDrive = obj.value("mapHomeDrive").toBool();
  }
  if (obj.contains("lastSuccessfulUsername") && obj.value("lastSuccessfulUsername").isString()) {
    options.lastSuccessfulUsername = obj.value("lastSuccessfulUsername").toString().trimmed();
  }
  return options;
}

QString makeSessionRuntimeOptionsJson(bool enableClipboard, bool mapHomeDrive,
                                      const QString& lastSuccessfulUsername = QString()) {
  QJsonObject obj;
  obj.insert("enableClipboard", enableClipboard);
  obj.insert("mapHomeDrive", mapHomeDrive);
  if (!lastSuccessfulUsername.trimmed().isEmpty()) {
    obj.insert("lastSuccessfulUsername", lastSuccessfulUsername.trimmed());
  }
  return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool gatewayVisibleForFolder(const vaultrdp::model::Gateway& gateway, const std::optional<QString>& folderId) {
  if (gateway.allowAnyFolder) {
    return true;
  }
  if (!folderId.has_value() || folderId->trimmed().isEmpty()) {
    return !gateway.folderId.has_value() || gateway.folderId->trimmed().isEmpty();
  }
  return gateway.folderId.has_value() && gateway.folderId->trimmed() == folderId->trimmed();
}

std::vector<std::pair<QString, QString>> gatewayOptionsForFolder(
    const std::vector<vaultrdp::model::Gateway>& gateways, const std::optional<QString>& folderId,
    const std::optional<QString>& forceIncludeGatewayId = std::nullopt) {
  std::vector<std::pair<QString, QString>> out;
  out.reserve(gateways.size());
  for (const auto& gateway : gateways) {
    const bool forceInclude = forceIncludeGatewayId.has_value() && gateway.id == forceIncludeGatewayId.value();
    if (forceInclude || gatewayVisibleForFolder(gateway, folderId)) {
      out.emplace_back(gateway.id, gateway.name);
    }
  }
  return out;
}

std::optional<QString> parentFolderIdForIndex(const QModelIndex& parentIndex) {
  if (!parentIndex.isValid()) {
    return std::nullopt;
  }
  if (parentIndex.data(kItemTypeRole).toInt() != kItemTypeFolder) {
    return std::nullopt;
  }
  const QString id = parentIndex.data(kItemIdRole).toString().trimmed();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  return id;
}

class FolderTreeView : public QTreeView {
 public:
  struct DragPayload {
    int itemType = 0;
    QString itemId;
    std::optional<QString> sourceFolderId;
  };
  using DropCallback = std::function<void(const DragPayload&, const std::optional<QString>& destinationFolderId)>;

  explicit FolderTreeView(QWidget* parent = nullptr) : QTreeView(parent) {}

  void setDropCallback(DropCallback callback) { dropCallback_ = std::move(callback); }

 protected:
  void startDrag(Qt::DropActions supportedActions) override {
    dragPayload_ = DragPayload();
    const QModelIndex index = currentIndex();
    if (index.isValid()) {
      dragPayload_.itemType = index.data(kItemTypeRole).toInt();
      dragPayload_.itemId = index.data(kItemIdRole).toString().trimmed();
      dragPayload_.sourceFolderId = parentFolderIdForIndex(index.parent());
    }
    QTreeView::startDrag(supportedActions);
  }

  void dropEvent(QDropEvent* event) override {
    const QModelIndex dropIndex = indexAt(event->position().toPoint());
    const auto dropPos = dropIndicatorPosition();
    QTreeView::dropEvent(event);

    if (!dropCallback_.has_value()) {
      return;
    }
    if (dragPayload_.itemId.isEmpty()) {
      return;
    }

    const std::optional<QString> destinationFolderId = destinationFolderFromDrop(dropIndex, dropPos);
    dropCallback_.value()(dragPayload_, destinationFolderId);
  }

 private:
  std::optional<QString> destinationFolderFromDrop(const QModelIndex& dropIndex,
                                                   DropIndicatorPosition dropPosition) const {
    if (dropPosition == OnViewport) {
      return std::nullopt;
    }

    if (dropPosition == OnItem && dropIndex.isValid() && dropIndex.data(kItemTypeRole).toInt() == kItemTypeFolder) {
      const QString folderId = dropIndex.data(kItemIdRole).toString().trimmed();
      if (!folderId.isEmpty()) {
        return folderId;
      }
      return std::nullopt;
    }

    return parentFolderIdForIndex(dropIndex.parent());
  }

  DragPayload dragPayload_;
  std::optional<DropCallback> dropCallback_;
};
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
  auto* dragTreeView = new FolderTreeView(mainSplitter_);
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
  dragTreeView->setDropCallback([this](const FolderTreeView::DragPayload& payload,
                                       const std::optional<QString>& destinationFolderId) {
    if (isReloadingTree_) {
      return;
    }
    if (payload.itemType != kItemTypeFolder && payload.itemType != kItemTypeConnection &&
        payload.itemType != kItemTypeGateway) {
      return;
    }
    if (payload.sourceFolderId == destinationFolderId) {
      return;
    }

    bool ok = false;
    QString failureTitle;
    if (payload.itemType == kItemTypeConnection) {
      ok = connectionRepository_->moveConnectionToFolder(payload.itemId, destinationFolderId);
      failureTitle = "Move Connection";
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
    reloadFolderTree();
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
  connect(folderTreeView_, &QTreeView::doubleClicked, [this](const QModelIndex&) {
    connectSelectedConnection();
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

void MainWindow::setupMenuBar() {
  auto* fileMenu = menuBar()->addMenu("&File");
  newFolderAction_ = fileMenu->addAction("New Folder", this, &MainWindow::createFolder);
  newConnectionAction_ = fileMenu->addAction("New Connection", this, &MainWindow::createConnection);
  fileMenu->addAction("New Gateway", this, &MainWindow::createGateway);
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
  NewConnectionDialog dialog(this);
  const auto gateways = gatewayRepository_->listGateways();
  const auto gatewayOptions = gatewayOptionsForFolder(gateways, selectedFolderId());
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

  if (dialog.saveCredential() && hasCredentialFields) {
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

void MainWindow::createGateway() {
  qInfo() << "[ui] createGateway requested";
  NewGatewayDialog dialog(this);
  dialog.setInitialValues(QString(), QString(), 443, vaultrdp::model::GatewayCredentialMode::SameAsConnection,
                          QString(), QString(), QString(), false);
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
    if (dialog.username().isEmpty() || dialog.password().isEmpty()) {
      db.rollback();
      QMessageBox::warning(this, "New Gateway",
                           "Username and password are required for Saved Credentials mode.");
      return;
    }
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
  const auto gateways = gatewayRepository_->listGateways();
  const std::optional<QString> editFolderId =
      connection.folderId.trimmed().isEmpty() ? std::nullopt : std::optional<QString>(connection.folderId);
  const auto gatewayOptions = gatewayOptionsForFolder(gateways, editFolderId, connection.gatewayId);
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
                          connection.gatewayId);

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
  if (!dialog.saveCredential()) {
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
  if (selectedGatewayId().has_value()) {
    editSelectedGateway();
  }
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
  dialog.setDialogTitle("Edit Gateway");
  dialog.setInitialValues(maybeGateway->name, maybeGateway->host, maybeGateway->port,
                          maybeGateway->credentialMode, existingUsername, existingDomain, existingPassword,
                          maybeGateway->allowAnyFolder);
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
    if (dialog.username().isEmpty() || dialog.password().isEmpty()) {
      db.rollback();
      QMessageBox::warning(this, "Edit Gateway",
                           "Username and password are required for Saved Credentials mode.");
      return;
    }
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

    bool ok = false;
    const QString oldPass = QInputDialog::getText(&dialog, "", "Current password:",
                                                  QLineEdit::Password, QString(), &ok);
    if (!ok || oldPass.isEmpty()) {
      return;
    }
    const QString newPass = QInputDialog::getText(&dialog, "", "New password:",
                                                  QLineEdit::Password, QString(), &ok);
    if (!ok || newPass.isEmpty()) {
      return;
    }
    const QString confirm = QInputDialog::getText(&dialog, "", "Confirm new password:",
                                                  QLineEdit::Password, QString(), &ok);
    if (!ok || confirm.isEmpty()) {
      return;
    }
    if (newPass != confirm) {
      QMessageBox::warning(&dialog, "Change Password", "New passwords do not match.");
      return;
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

  bool ok = false;
  const QString passphrase =
      QInputDialog::getText(this, "", "Master passphrase:", QLineEdit::Password, QString(), &ok);
  if (!ok || passphrase.isEmpty()) {
    return false;
  }

  if (!vaultManager_->unlock(passphrase)) {
    QMessageBox::warning(this, "Unlock Vault", "Incorrect passphrase.");
    updateVaultStatus();
    return false;
  }

  updateVaultStatus();
  return true;
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
  if (settings.value("vault/first_startup_encryption_wizard_seen", false).toBool()) {
    return;
  }

  settings.setValue("vault/first_startup_encryption_wizard_seen", true);

  if (vaultManager_->state() != vaultrdp::core::VaultState::Disabled) {
    return;
  }

  QTimer::singleShot(0, this, [this]() {
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
    const QString passphrase = QInputDialog::getText(
        this, "", "Set vault password:",
        QLineEdit::Password, QString(), &ok);
    if (!ok || passphrase.isEmpty()) {
      updateVaultStatus();
      return;
    }

    const QString confirm = QInputDialog::getText(this, "", "Confirm vault password:",
                                                  QLineEdit::Password, QString(), &ok);
    if (!ok || confirm.isEmpty()) {
      updateVaultStatus();
      return;
    }

    if (passphrase != confirm) {
      QMessageBox::warning(this, "Enable Encryption", "Passwords do not match.");
      updateVaultStatus();
      return;
    }

    if (!vaultManager_->enable(passphrase)) {
      QMessageBox::warning(this, "Enable Encryption",
                           "Failed to enable encryption. Password must satisfy policy.");
    }
    updateVaultStatus();
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

  if (itemType == kItemTypeVaultRoot) {
    return std::nullopt;
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
  const SessionRuntimeOptions sessionOptions = parseSessionRuntimeOptions(launchInfo.connection.optionsJson);
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
  } else if (itemType == kItemTypeGateway) {
    menu.addAction("Edit...", this, &MainWindow::editSelectedGateway);
    menu.addAction("Duplicate", this, &MainWindow::duplicateSelectedGateway);
    menu.addAction("Rename", this, &MainWindow::renameSelectedItem);
    menu.addAction("Delete", this, &MainWindow::deleteSelectedItem);
    menu.addSeparator();
    menu.addAction("Copy Hostname", this, &MainWindow::copySelectedHostname);
    menu.addAction("Copy Username", this, &MainWindow::copySelectedUsername);
  } else {
    menu.addAction("New Folder", this, &MainWindow::createFolder);
    menu.addAction("New Connection...", this, &MainWindow::createConnection);
    menu.addAction("New Gateway...", this, &MainWindow::createGateway);
    if (itemType == kItemTypeFolder) {
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
    } else {
      menu.addSeparator();
      menu.addAction("Expand All", folderTreeView_, &QTreeView::expandAll);
      menu.addAction("Collapse All", folderTreeView_, &QTreeView::collapseAll);
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
  if (itemType != kItemTypeFolder && itemType != kItemTypeConnection && itemType != kItemTypeGateway) {
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
  } else if (itemType == kItemTypeGateway) {
    ok = gatewayRepository_->renameGateway(itemId, newName);
  }

  if (!ok) {
    QMessageBox::warning(this, "Rename", "Failed to rename item.");
    reloadFolderTree();
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

  QDialog dialog(this);
  dialog.setWindowTitle("");
  dialog.resize(420, 160);

  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* intro = new QLabel(forGateway ? "Enter gateway credentials." : "Enter connection credentials.", &dialog);
  layout->addWidget(intro);
  auto* usernameEdit = new QLineEdit(&dialog);
  auto* domainEdit = new QLineEdit(&dialog);
  auto* passwordEdit = new QLineEdit(&dialog);
  passwordEdit->setEchoMode(QLineEdit::Password);
  if (suggestedUsername.has_value()) {
    usernameEdit->setText(suggestedUsername.value());
  }
  if (suggestedDomain.has_value()) {
    domainEdit->setText(suggestedDomain.value());
  }

  form->addRow("Username", usernameEdit);
  form->addRow("Domain", domainEdit);
  form->addRow("Password", passwordEdit);
  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  while (true) {
    if (dialog.exec() != QDialog::Accepted) {
      return false;
    }
    const QString username = usernameEdit->text().trimmed();
    const QString password = passwordEdit->text();
    if (username.isEmpty() || password.isEmpty()) {
      QMessageBox::warning(this, "Credentials Required", "Username and password are required.");
      continue;
    }
    *usernameOut = username;
    *domainOut = domainEdit->text().trimmed();
    *passwordOut = password;
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
