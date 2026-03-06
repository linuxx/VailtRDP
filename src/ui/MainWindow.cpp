#include "ui/MainWindow.hpp"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
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
#include <QResizeEvent>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QShortcut>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QScreen>
#include <QWindow>
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
#include "ui/FolderTreeView.hpp"
#include "ui/GatewayScope.hpp"
#include "ui/IconTheme.hpp"
#include "ui/NewConnectionDialog.hpp"
#include "ui/NewGatewayDialog.hpp"
#include "ui/CredentialPromptDialog.hpp"
#include "ui/CredentialScope.hpp"
#include "ui/RootScope.hpp"
#include "ui/SessionRuntimeOptions.hpp"
#include "ui/SessionController.hpp"
#include "ui/SessionTabContent.hpp"
#include "ui/TreeItemRoles.hpp"
#include "ui/NewCredentialDialog.hpp"

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
using vaultrdp::ui::gatewayOptionsForFolder;
using vaultrdp::ui::credentialOptionsForFolder;
using vaultrdp::ui::themedIcon;
using vaultrdp::ui::makeSessionRuntimeOptionsJson;
using vaultrdp::ui::parseSessionRuntimeOptions;
using vaultrdp::ui::SessionRuntimeOptions;

std::optional<QString> promptPasswordValue(QWidget* parent, const QString& promptText) {
  QDialog dialog(parent);
  dialog.setWindowTitle("");
  dialog.setMinimumSize(420, 140);
  dialog.resize(460, 150);

  auto* layout = new QVBoxLayout(&dialog);
  auto* promptLabel = new QLabel(promptText, &dialog);
  promptLabel->setWordWrap(true);
  auto* passwordEdit = new QLineEdit(&dialog);
  passwordEdit->setEchoMode(QLineEdit::Password);

  auto* form = new QFormLayout();
  form->addRow("Password", passwordEdit);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  layout->addWidget(promptLabel);
  layout->addLayout(form);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return passwordEdit->text();
}

std::optional<QString> promptTextValue(QWidget* parent, const QString& promptText, const QString& labelText,
                                       const QString& initialValue = QString()) {
  QDialog dialog(parent);
  dialog.setWindowTitle("");
  dialog.setMinimumSize(420, 140);
  dialog.resize(460, 150);

  auto* layout = new QVBoxLayout(&dialog);
  auto* promptLabel = new QLabel(promptText, &dialog);
  promptLabel->setWordWrap(true);
  auto* textEdit = new QLineEdit(&dialog);
  textEdit->setText(initialValue);

  auto* form = new QFormLayout();
  form->addRow(labelText, textEdit);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  layout->addWidget(promptLabel);
  layout->addLayout(form);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return textEdit->text().trimmed();
}
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
      treeSearchEdit_(nullptr),
      sessionTabWidget_(nullptr),
      newFolderAction_(nullptr),
      newConnectionAction_(nullptr),
      newGatewayAction_(nullptr),
      newCredentialAction_(nullptr),
      connectAction_(nullptr),
      logoffAction_(nullptr),
      exitFullscreenAction_(nullptr),
      disconnectAction_(nullptr),
      disconnectAllAction_(nullptr),
      fullscreenSessionAction_(nullptr),
      lockVaultAction_(nullptr),
      themeSystemAction_(nullptr),
      themeDarkAction_(nullptr),
      themeLightAction_(nullptr),
      vaultStatusLabel_(nullptr),
      debugModeLabel_(nullptr),
      unlockVaultButton_(nullptr),
      treeConnectButton_(nullptr),
      treeNewConnectionButton_(nullptr),
      treeNewFolderButton_(nullptr),
      treeNewCredentialButton_(nullptr),
      treeNewGatewayButton_(nullptr),
      mainToolBar_(nullptr),
      mainSplitter_(nullptr),
      treePaneWidget_(nullptr),
      welcomeTab_(nullptr),
      fullscreenShortcut_(nullptr),
      exitFullscreenShortcut_(nullptr),
      fullscreenMode_(FullscreenMode::Windowed),
      sessionFullscreenConnectionId_(),
      splitterSizesBeforeSessionFullscreen_(),
      windowStateBeforeSessionFullscreen_(Qt::WindowNoState),
      sessionController_(std::make_unique<vaultrdp::ui::SessionController>()),
      sessionGenerationCounter_(0),
      suppressClipboardEvent_(false),
      ignoreClipboardEventsUntilMs_(0),
      lastClipboardWasRemoteFileUris_(false),
      isReloadingTree_(false),
      isApplyingTreeFilter_(false),
      treeMutationGuard_(false),
      treeReloadScheduled_(false) {
  setupUi();
  setupMenuBar();
  setupToolBar();
  applyTheme(savedThemeMode());
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
  auto* treePane = new QWidget(mainSplitter_);
  treePaneWidget_ = treePane;
  auto* treePaneLayout = new QVBoxLayout(treePane);
  treePaneLayout->setContentsMargins(0, 0, 0, 0);
  treePaneLayout->setSpacing(0);

  auto* treeActionBar = new QWidget(treePane);
  treeActionBar->setObjectName("treeActionBar");
  auto* treeActionLayout = new QHBoxLayout(treeActionBar);
  treeActionLayout->setContentsMargins(8, 6, 8, 6);
  treeActionLayout->setSpacing(6);
  treeActionLayout->addStretch();

  treeConnectButton_ = new QToolButton(treeActionBar);
  treeConnectButton_->setObjectName("treeActionButton");
  treeConnectButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  treeConnectButton_->setToolTip("Connect selected connection");
  treeConnectButton_->setFixedSize(36, 36);
  treeConnectButton_->setIconSize(QSize(26, 26));
  connect(treeConnectButton_, &QToolButton::clicked, this, &MainWindow::connectSelectedConnection);
  treeActionLayout->addWidget(treeConnectButton_);

  treeNewConnectionButton_ = new QToolButton(treeActionBar);
  treeNewConnectionButton_->setObjectName("treeActionButton");
  treeNewConnectionButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  treeNewConnectionButton_->setToolTip("New connection");
  treeNewConnectionButton_->setFixedSize(36, 36);
  treeNewConnectionButton_->setIconSize(QSize(26, 26));
  connect(treeNewConnectionButton_, &QToolButton::clicked, this, &MainWindow::createConnection);
  treeActionLayout->addWidget(treeNewConnectionButton_);

  treeNewFolderButton_ = new QToolButton(treeActionBar);
  treeNewFolderButton_->setObjectName("treeActionButton");
  treeNewFolderButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  treeNewFolderButton_->setToolTip("New folder");
  treeNewFolderButton_->setFixedSize(36, 36);
  treeNewFolderButton_->setIconSize(QSize(26, 26));
  connect(treeNewFolderButton_, &QToolButton::clicked, this, &MainWindow::createFolder);
  treeActionLayout->addWidget(treeNewFolderButton_);

  treeNewCredentialButton_ = new QToolButton(treeActionBar);
  treeNewCredentialButton_->setObjectName("treeActionButton");
  treeNewCredentialButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  treeNewCredentialButton_->setToolTip("New credential set");
  treeNewCredentialButton_->setFixedSize(36, 36);
  treeNewCredentialButton_->setIconSize(QSize(26, 26));
  connect(treeNewCredentialButton_, &QToolButton::clicked, this, &MainWindow::createCredential);
  treeActionLayout->addWidget(treeNewCredentialButton_);

  treeNewGatewayButton_ = new QToolButton(treeActionBar);
  treeNewGatewayButton_->setObjectName("treeActionButton");
  treeNewGatewayButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  treeNewGatewayButton_->setToolTip("New gateway");
  treeNewGatewayButton_->setFixedSize(36, 36);
  treeNewGatewayButton_->setIconSize(QSize(26, 26));
  connect(treeNewGatewayButton_, &QToolButton::clicked, this, &MainWindow::createGateway);
  treeActionLayout->addWidget(treeNewGatewayButton_);

  treeActionLayout->addStretch();
  treePaneLayout->addWidget(treeActionBar);

  auto* dragTreeView = new vaultrdp::ui::FolderTreeView(treePane);
  folderTreeView_ = dragTreeView;
  folderTreeView_->setHeaderHidden(true);
  folderTreeView_->header()->setStretchLastSection(true);
  folderTreeView_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  folderTreeView_->setIndentation(18);
  folderTreeView_->setUniformRowHeights(true);
  folderTreeView_->setEditTriggers(QAbstractItemView::EditKeyPressed);
  folderTreeView_->setContextMenuPolicy(Qt::CustomContextMenu);
  folderTreeView_->setSelectionMode(QAbstractItemView::SingleSelection);
  folderTreeView_->setIconSize(QSize(20, 20));
  folderTreeView_->setDragEnabled(true);
  folderTreeView_->setAcceptDrops(true);
  folderTreeView_->setDropIndicatorShown(true);
  folderTreeView_->setDragDropMode(QAbstractItemView::InternalMove);
  folderTreeView_->setDefaultDropAction(Qt::MoveAction);
  treePaneLayout->addWidget(folderTreeView_);
  connect(folderTreeView_, &QTreeView::customContextMenuRequested, this, &MainWindow::showTreeContextMenu);

  folderTreeModel_ = new QStandardItemModel(folderTreeView_);
  folderTreeModel_->setHorizontalHeaderLabels({"VaultRDP"});
  folderTreeView_->setModel(folderTreeModel_);
  connect(folderTreeModel_, &QStandardItemModel::itemChanged, this, &MainWindow::onTreeItemChanged);
  connect(folderTreeView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this](const QModelIndex&, const QModelIndex&) {
            updateCreateActionAvailability();
            if (isReloadingTree_ || isApplyingTreeFilter_) {
              return;
            }
            QStringList expandedKeys;
            QString selectedKey;
            captureTreeState(&expandedKeys, &selectedKey);
            QSettings settings;
            settings.setValue("ui/tree_expanded_keys", expandedKeys);
            settings.setValue("ui/tree_selected_key", selectedKey);
          });
  connect(folderTreeView_, &QTreeView::expanded, this, [this](const QModelIndex&) {
    if (isReloadingTree_ || isApplyingTreeFilter_) {
      return;
    }
    QStringList expandedKeys;
    QString selectedKey;
    captureTreeState(&expandedKeys, &selectedKey);
    QSettings settings;
    settings.setValue("ui/tree_expanded_keys", expandedKeys);
    settings.setValue("ui/tree_selected_key", selectedKey);
  });
  connect(folderTreeView_, &QTreeView::collapsed, this, [this](const QModelIndex&) {
    if (isReloadingTree_ || isApplyingTreeFilter_) {
      return;
    }
    QStringList expandedKeys;
    QString selectedKey;
    captureTreeState(&expandedKeys, &selectedKey);
    QSettings settings;
    settings.setValue("ui/tree_expanded_keys", expandedKeys);
    settings.setValue("ui/tree_selected_key", selectedKey);
  });
  dragTreeView->setDropCallback([this](const vaultrdp::ui::FolderTreeView::DragPayload& payload,
                                       const std::optional<QString>& destinationFolderId) {
    const auto scheduleReload = [this]() {
      scheduleTreeReload();
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

  auto* sessionPane = new QWidget(mainSplitter_);
  auto* sessionPaneLayout = new QVBoxLayout(sessionPane);
  sessionPaneLayout->setContentsMargins(0, 0, 0, 0);
  sessionPaneLayout->setSpacing(0);

  sessionTabWidget_ = new QTabWidget(sessionPane);
  sessionTabWidget_->setTabsClosable(true);
  sessionTabWidget_->setMovable(true);
  sessionTabWidget_->setDocumentMode(true);
  sessionPaneLayout->addWidget(sessionTabWidget_);

  welcomeTab_ = new QWidget(sessionTabWidget_);
  auto* emptyLayout = new QVBoxLayout(welcomeTab_);
  emptyLayout->setContentsMargins(32, 36, 32, 36);
  emptyLayout->setSpacing(16);
  emptyLayout->addStretch();

  auto* centerBlock = new QWidget(welcomeTab_);
  centerBlock->setObjectName("welcomeCenterBlock");
  centerBlock->setMaximumWidth(980);
  auto* centerLayout = new QVBoxLayout(centerBlock);
  centerLayout->setContentsMargins(0, 0, 0, 0);
  centerLayout->setSpacing(14);

  auto* titleLabel = new QLabel("No Active Sessions", centerBlock);
  titleLabel->setObjectName("welcomeTitleLabel");
  titleLabel->setAlignment(Qt::AlignHCenter);
  centerLayout->addWidget(titleLabel);

  auto* subtitleLabel =
      new QLabel("No active sessions. Select one of the options below to add a new item.", centerBlock);
  subtitleLabel->setObjectName("welcomeSubtitleLabel");
  subtitleLabel->setAlignment(Qt::AlignHCenter);
  subtitleLabel->setWordWrap(true);
  centerLayout->addWidget(subtitleLabel);

  auto* cardsRow = new QHBoxLayout();
  cardsRow->setSpacing(16);
  cardsRow->setContentsMargins(0, 6, 0, 0);

  auto* newConnectionCard = new QToolButton(centerBlock);
  newConnectionCard->setObjectName("welcomeCardButton");
  newConnectionCard->setText("New Connection");
  newConnectionCard->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewConnection, 112, this));
  newConnectionCard->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  newConnectionCard->setIconSize(QSize(112, 112));
  newConnectionCard->setFixedSize(280, 228);
  connect(newConnectionCard, &QToolButton::clicked, this, &MainWindow::createConnection);

  auto* newGatewayCard = new QToolButton(centerBlock);
  newGatewayCard->setObjectName("welcomeCardButton");
  newGatewayCard->setText("New Gateway");
  newGatewayCard->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewGateway, 112, this));
  newGatewayCard->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  newGatewayCard->setIconSize(QSize(112, 112));
  newGatewayCard->setFixedSize(280, 228);
  connect(newGatewayCard, &QToolButton::clicked, this, &MainWindow::createGateway);

  auto* newCredentialCard = new QToolButton(centerBlock);
  newCredentialCard->setObjectName("welcomeCardButton");
  newCredentialCard->setText("New Credential");
  newCredentialCard->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewCredential, 112, this));
  newCredentialCard->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  newCredentialCard->setIconSize(QSize(112, 112));
  newCredentialCard->setFixedSize(280, 228);
  connect(newCredentialCard, &QToolButton::clicked, this, &MainWindow::createCredential);

  cardsRow->addStretch();
  cardsRow->addWidget(newConnectionCard);
  cardsRow->addWidget(newGatewayCard);
  cardsRow->addWidget(newCredentialCard);
  cardsRow->addStretch();
  centerLayout->addLayout(cardsRow);

  emptyLayout->addWidget(centerBlock, 0, Qt::AlignHCenter);
  emptyLayout->addStretch();

  sessionTabWidget_->addTab(welcomeTab_, "Welcome");
  connect(sessionTabWidget_, &QTabWidget::tabCloseRequested, this, &MainWindow::handleTabCloseRequested);
  connect(sessionTabWidget_, &QTabWidget::currentChanged, this, [this](int) {
    syncClipboardToFocusedSession();
    updateCreateActionAvailability();
    if (fullscreenSessionAction_ != nullptr) {
      fullscreenSessionAction_->setEnabled(currentSessionConnectionId().has_value());
    }
    if (isSessionFullscreenActive()) {
      const auto currentId = currentSessionConnectionId();
      if (currentId.has_value()) {
        sessionFullscreenConnectionId_ = currentId.value();
      } else {
        exitSessionFullscreen();
      }
    }
  });
  if (auto* tabBar = sessionTabWidget_->tabBar(); tabBar != nullptr) {
    tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar, &QTabBar::customContextMenuRequested, this, [this, tabBar](const QPoint& pos) {
      const int index = tabBar->tabAt(pos);
      if (index < 0) {
        return;
      }
      QWidget* tab = sessionTabWidget_->widget(index);
      if (tab == nullptr || tab == welcomeTab_) {
        return;
      }
      sessionTabWidget_->setCurrentIndex(index);
      QMenu menu(this);
      auto* fullscreenAction =
          menu.addAction("Open Full Screen", this, &MainWindow::toggleCurrentSessionFullscreen);
      fullscreenAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connect, this));
      auto* logoffAction = menu.addAction("Logoff", this, &MainWindow::logoffCurrentSession);
      logoffAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Logoff, this));
      auto* disconnectAction = menu.addAction("Disconnect", this, [this, index]() {
        handleTabCloseRequested(index);
      });
      disconnectAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Disconnect, this));
      menu.exec(tabBar->mapToGlobal(pos));
    });
  }
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
        if (QApplication::activeModalWidget() != nullptr) {
          return;
        }
        if (treeMutationGuard_) {
          return;
        }
        if (folderTreeView_ != nullptr) {
          QWidget* fw = QApplication::focusWidget();
          if (fw != nullptr && folderTreeView_->isAncestorOf(fw) && qobject_cast<QLineEdit*>(fw) != nullptr) {
            return;
          }
        }
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
  mainSplitter_->setSizes({300, 980});

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
  unlockVaultButton_->setVisible(false);

  status->showMessage("DB: Ready");
  setStatusBar(status);
  fullscreenShortcut_ = new QShortcut(QKeySequence(Qt::Key_F11), this);
  connect(fullscreenShortcut_, &QShortcut::activated, this, &MainWindow::toggleCurrentSessionFullscreen);
  exitFullscreenShortcut_ = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(exitFullscreenShortcut_, &QShortcut::activated, this, &MainWindow::exitSessionFullscreen);
  restoreUiSettings();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  disconnectAllSessions();
  persistUiSettings();
  QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
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
  const bool maximized = !isFullScreen() && isMaximized();
  settings.setValue("ui/main_window_maximized", maximized);

  if (!maximized && !isFullScreen()) {
    settings.setValue("ui/main_window_geometry", saveGeometry());
  }
  if (mainSplitter_ != nullptr) {
    if (isSessionFullscreenActive() && splitterSizesBeforeSessionFullscreen_.size() >= 2) {
      settings.setValue("ui/main_splitter_sizes", QVariant::fromValue(splitterSizesBeforeSessionFullscreen_));
    } else {
      settings.setValue("ui/main_splitter_sizes", QVariant::fromValue(mainSplitter_->sizes()));
    }
  }

  QStringList expandedKeys;
  QString selectedKey;
  captureTreeState(&expandedKeys, &selectedKey);
  settings.setValue("ui/tree_expanded_keys", expandedKeys);
  settings.setValue("ui/tree_selected_key", selectedKey);
}

QString MainWindow::treeItemKey(const QModelIndex& index) const {
  if (!index.isValid()) {
    return QString("%1:root").arg(kItemTypeVaultRoot);
  }
  const int itemType = index.data(kItemTypeRole).toInt();
  QString itemId = index.data(kItemIdRole).toString().trimmed();
  if (itemType == kItemTypeVaultRoot || itemId.isEmpty()) {
    itemId = "root";
  }
  return QString("%1:%2").arg(itemType).arg(itemId);
}

void MainWindow::captureTreeState(QStringList* expandedKeys, QString* selectedKey) const {
  if (expandedKeys == nullptr || selectedKey == nullptr || folderTreeView_ == nullptr || folderTreeModel_ == nullptr) {
    return;
  }

  expandedKeys->clear();
  selectedKey->clear();

  if (folderTreeView_->selectionModel() != nullptr) {
    *selectedKey = treeItemKey(folderTreeView_->selectionModel()->currentIndex());
  }

  std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
    const int rows = folderTreeModel_->rowCount(parent);
    for (int row = 0; row < rows; ++row) {
      const QModelIndex index = folderTreeModel_->index(row, 0, parent);
      if (!index.isValid()) {
        continue;
      }
      if (folderTreeView_->isExpanded(index)) {
        expandedKeys->push_back(treeItemKey(index));
      }
      walk(index);
    }
  };
  walk(QModelIndex());
}

void MainWindow::restoreTreeState(const QStringList& expandedKeys, const QString& selectedKey) {
  if (folderTreeView_ == nullptr || folderTreeModel_ == nullptr) {
    return;
  }

  const QSet<QString> expandedSet(expandedKeys.begin(), expandedKeys.end());
  QModelIndex selectedIndex;

  std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
    const int rows = folderTreeModel_->rowCount(parent);
    for (int row = 0; row < rows; ++row) {
      const QModelIndex index = folderTreeModel_->index(row, 0, parent);
      if (!index.isValid()) {
        continue;
      }

      const QString key = treeItemKey(index);
      if (expandedSet.contains(key)) {
        folderTreeView_->setExpanded(index, true);
      }
      if (!selectedKey.isEmpty() && key == selectedKey) {
        selectedIndex = index;
      }
      walk(index);
    }
  };
  walk(QModelIndex());

  if (!selectedIndex.isValid()) {
    selectedIndex = folderTreeModel_->index(0, 0);
  }
  if (selectedIndex.isValid()) {
    folderTreeView_->setCurrentIndex(selectedIndex);
  }
}

void MainWindow::updateCreateActionAvailability() {
  const QModelIndex index = folderTreeView_ != nullptr ? folderTreeView_->currentIndex() : QModelIndex();
  const int itemType = index.isValid() ? index.data(kItemTypeRole).toInt() : kItemTypeVaultRoot;
  const bool atVaultLevel = !index.isValid() || itemType == kItemTypeVaultRoot;
  const bool vaultUsable = vaultManager_ != nullptr &&
                           vaultManager_->state() != vaultrdp::core::VaultState::Locked;
  const bool canConnect = vaultUsable && itemType == kItemTypeConnection;
  const QString selectedConnectionId =
      (itemType == kItemTypeConnection) ? index.data(kItemIdRole).toString() : QString();
  const bool selectedConnectionActive =
      !selectedConnectionId.isEmpty() && hasActiveSessionForConnection(selectedConnectionId);
  const bool canCreateUnderCurrent = vaultUsable && !atVaultLevel;
  const bool canCreateFolder = vaultUsable;

  if (newFolderAction_ != nullptr) {
    newFolderAction_->setText(atVaultLevel ? "New Root Folder" : "New Subfolder");
    newFolderAction_->setEnabled(canCreateFolder);
  }
  if (newConnectionAction_ != nullptr) {
    newConnectionAction_->setEnabled(canCreateUnderCurrent);
  }
  if (newCredentialAction_ != nullptr) {
    newCredentialAction_->setEnabled(canCreateUnderCurrent);
  }
  if (newGatewayAction_ != nullptr) {
    newGatewayAction_->setEnabled(canCreateUnderCurrent);
  }
  if (connectAction_ != nullptr) {
    connectAction_->setText(selectedConnectionActive ? "Disconnect" : "Connect");
    connectAction_->setToolTip(selectedConnectionActive ? "Disconnect selected connection"
                                                        : "Connect selected connection");
    connectAction_->setIcon(
        themedIcon(selectedConnectionActive ? vaultrdp::ui::AppIcon::Disconnect : vaultrdp::ui::AppIcon::Connect,
                   this));
    connectAction_->setEnabled(canConnect);
  }
  if (logoffAction_ != nullptr) {
    const auto currentId = currentSessionConnectionId();
    const bool canLogoff = currentId.has_value() &&
                           sessionsByConnection_.value(currentId.value(), nullptr) != nullptr &&
                           sessionsByConnection_.value(currentId.value(), nullptr)->state() ==
                               vaultrdp::protocols::SessionState::Connected;
    logoffAction_->setEnabled(canLogoff);
  }
  if (exitFullscreenAction_ != nullptr) {
    exitFullscreenAction_->setVisible(isSessionFullscreenActive());
    exitFullscreenAction_->setEnabled(isSessionFullscreenActive());
  }
  if (fullscreenSessionAction_ != nullptr) {
    fullscreenSessionAction_->setEnabled(currentSessionConnectionId().has_value());
  }
  if (treeConnectButton_ != nullptr) {
    treeConnectButton_->setToolTip(selectedConnectionActive ? "Disconnect selected connection"
                                                            : "Connect selected connection");
    treeConnectButton_->setIcon(
        themedIcon(selectedConnectionActive ? vaultrdp::ui::AppIcon::Disconnect : vaultrdp::ui::AppIcon::Connect,
                   this));
    treeConnectButton_->setEnabled(canConnect);
  }
  if (treeNewConnectionButton_ != nullptr) {
    treeNewConnectionButton_->setEnabled(canCreateUnderCurrent);
  }
  if (treeNewFolderButton_ != nullptr) {
    treeNewFolderButton_->setEnabled(canCreateFolder);
    treeNewFolderButton_->setToolTip(atVaultLevel ? "New root folder" : "New subfolder");
  }
  if (treeNewCredentialButton_ != nullptr) {
    treeNewCredentialButton_->setEnabled(canCreateUnderCurrent);
  }
  if (treeNewGatewayButton_ != nullptr) {
    treeNewGatewayButton_->setEnabled(canCreateUnderCurrent);
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
  newFolderAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewFolder, this));
  newConnectionAction_ = fileMenu->addAction("New Connection", this, &MainWindow::createConnection);
  newConnectionAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewConnection, this));
  newCredentialAction_ = fileMenu->addAction("New Credential Set", this, &MainWindow::createCredential);
  newCredentialAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewCredential, this));
  newGatewayAction_ = fileMenu->addAction("New Gateway", this, &MainWindow::createGateway);
  newGatewayAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewGateway, this));
  fileMenu->addSeparator();
  fileMenu->addAction("Exit", this, &QWidget::close);

  auto* editMenu = menuBar()->addMenu("&Edit");
  auto* editAction = editMenu->addAction("Edit Selected", this, &MainWindow::editSelectedItem);
  editAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Edit, this));
  auto* duplicateAction = editMenu->addAction("Duplicate");
  duplicateAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Duplicate, this));
  auto* deleteAction = editMenu->addAction("Delete");
  deleteAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Delete, this));
  auto* renameAction = editMenu->addAction("Rename");
  renameAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Rename, this));

  auto* viewMenu = menuBar()->addMenu("&View");
  viewMenu->addAction("Expand All", folderTreeView_, &QTreeView::expandAll);
  viewMenu->addAction("Collapse All", folderTreeView_, &QTreeView::collapseAll);

  auto* settingsMenu = menuBar()->addMenu("&Settings");
  auto* themeMenu = settingsMenu->addMenu("Theme");
  auto* themeActionGroup = new QActionGroup(this);
  themeActionGroup->setExclusive(true);
  themeSystemAction_ = themeMenu->addAction("Use system (default)");
  themeSystemAction_->setCheckable(true);
  themeDarkAction_ = themeMenu->addAction("Dark");
  themeDarkAction_->setCheckable(true);
  themeLightAction_ = themeMenu->addAction("Light");
  themeLightAction_->setCheckable(true);
  themeActionGroup->addAction(themeSystemAction_);
  themeActionGroup->addAction(themeDarkAction_);
  themeActionGroup->addAction(themeLightAction_);

  const ThemeMode mode = savedThemeMode();
  themeSystemAction_->setChecked(mode == ThemeMode::System);
  themeDarkAction_->setChecked(mode == ThemeMode::Dark);
  themeLightAction_->setChecked(mode == ThemeMode::Light);

  connect(themeSystemAction_, &QAction::triggered, this, [this]() { setThemeMode(ThemeMode::System); });
  connect(themeDarkAction_, &QAction::triggered, this, [this]() { setThemeMode(ThemeMode::Dark); });
  connect(themeLightAction_, &QAction::triggered, this, [this]() { setThemeMode(ThemeMode::Light); });

  auto* sessionMenu = menuBar()->addMenu("&Session");
  connectAction_ = sessionMenu->addAction("Connect", this, &MainWindow::connectOrDisconnectSelectedConnection);
  connectAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connect, this));
  logoffAction_ = sessionMenu->addAction("Logoff", this, &MainWindow::logoffCurrentSession);
  logoffAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Logoff, this));
  logoffAction_->setEnabled(false);
  fullscreenSessionAction_ =
      sessionMenu->addAction("Open Full Screen", this, &MainWindow::toggleCurrentSessionFullscreen);
  fullscreenSessionAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connect, this));
  fullscreenSessionAction_->setEnabled(false);
  sessionMenu->addSeparator();
  disconnectAction_ = sessionMenu->addAction("Disconnect", this, &MainWindow::disconnectCurrentSession);
  disconnectAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Disconnect, this));
  disconnectAllAction_ = sessionMenu->addAction("Disconnect All", this, &MainWindow::disconnectAllSessions);
  disconnectAllAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Disconnect, this));

  auto* vaultMenu = menuBar()->addMenu("&Vault");
  lockVaultAction_ = vaultMenu->addAction("Lock Vault", [this]() {
    if (vaultManager_->state() == vaultrdp::core::VaultState::Locked) {
      ensureVaultUnlocked();
    } else {
      vaultManager_->lock();
    }
    updateVaultStatus();
  });
  lockVaultAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Lock, this));
  auto* vaultSettingsAction = vaultMenu->addAction("Vault Settings...", this, &MainWindow::showVaultSettingsDialog);
  vaultSettingsAction->setIcon(themedIcon(vaultrdp::ui::AppIcon::Settings, this));

  auto* helpMenu = menuBar()->addMenu("&Help");
  helpMenu->addAction("About VaultRDP");
}

void MainWindow::setupToolBar() {
  mainToolBar_ = addToolBar("Main");
  mainToolBar_->setMovable(false);
  mainToolBar_->setObjectName("topToolBar");
  mainToolBar_->setIconSize(QSize(22, 22));

  auto* appMenuButton = new QToolButton(mainToolBar_);
  appMenuButton->setObjectName("topMenuButton");
  appMenuButton->setToolTip("Menu");
  appMenuButton->setPopupMode(QToolButton::InstantPopup);
  appMenuButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  appMenuButton->setIcon(themedIcon(vaultrdp::ui::AppIcon::Menu, this));
  appMenuButton->setIconSize(QSize(30, 30));
  appMenuButton->setFixedSize(46, 46);
  auto* appMenu = new QMenu(appMenuButton);

  auto addSubMenuFromMenuBar = [this, appMenu](const QString& title) {
    if (menuBar() == nullptr) {
      return;
    }
    for (QAction* action : menuBar()->actions()) {
      if (action == nullptr || action->menu() == nullptr) {
        continue;
      }
      const QString menuTitle = action->text();
      if (menuTitle.compare(title, Qt::CaseInsensitive) != 0) {
        continue;
      }
      auto* sub = appMenu->addMenu(menuTitle);
      for (QAction* subAction : action->menu()->actions()) {
        sub->addAction(subAction);
      }
      return;
    }
  };
  addSubMenuFromMenuBar("&File");
  addSubMenuFromMenuBar("&Edit");
  addSubMenuFromMenuBar("&View");
  addSubMenuFromMenuBar("&Settings");
  addSubMenuFromMenuBar("&Session");
  addSubMenuFromMenuBar("&Vault");
  addSubMenuFromMenuBar("&Help");
  appMenuButton->setMenu(appMenu);
  mainToolBar_->addWidget(appMenuButton);
  if (menuBar() != nullptr) {
    menuBar()->setVisible(false);
  }

  auto* brandIconLabel = new QLabel(mainToolBar_);
  brandIconLabel->setPixmap(themedIcon(vaultrdp::ui::AppIcon::Brand, 44, this).pixmap(44, 44));
  brandIconLabel->setObjectName("topBrandIconLabel");
  mainToolBar_->addWidget(brandIconLabel);

  auto* brandLabel = new QLabel("VaultRDP", mainToolBar_);
  brandLabel->setObjectName("topBrandLabel");
  brandLabel->setMargin(2);
  mainToolBar_->addWidget(brandLabel);
  mainToolBar_->addSeparator();

  treeSearchEdit_ = new QLineEdit(mainToolBar_);
  treeSearchEdit_->setPlaceholderText("Search tree...");
  treeSearchEdit_->setClearButtonEnabled(true);
  treeSearchEdit_->setMinimumWidth(420);
  treeSearchEdit_->setMaximumWidth(760);
  treeSearchEdit_->setObjectName("topSearchEdit");
  mainToolBar_->addWidget(treeSearchEdit_);
  connect(treeSearchEdit_, &QLineEdit::textChanged, this, &MainWindow::applyTreeFilter);

  auto* spacer = new QWidget(mainToolBar_);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  mainToolBar_->addWidget(spacer);

  mainToolBar_->addSeparator();
  mainToolBar_->addAction(connectAction_);
  mainToolBar_->addSeparator();
  mainToolBar_->addAction(logoffAction_);
  exitFullscreenAction_ = new QAction(themedIcon(vaultrdp::ui::AppIcon::Disconnect, this), "Exit Full Screen", this);
  connect(exitFullscreenAction_, &QAction::triggered, this, &MainWindow::exitSessionFullscreen);
  exitFullscreenAction_->setVisible(false);
  exitFullscreenAction_->setEnabled(false);
  mainToolBar_->addAction(exitFullscreenAction_);
  mainToolBar_->addSeparator();
  mainToolBar_->addAction(lockVaultAction_);

  if (auto* connectButton = qobject_cast<QToolButton*>(mainToolBar_->widgetForAction(connectAction_))) {
    connectButton->setObjectName("connectButton");
    connectButton->setMinimumHeight(46);
    connectButton->setIconSize(QSize(24, 24));
    connectButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connectButton->setToolTip("Connect");
  }
  if (auto* lockButton = qobject_cast<QToolButton*>(mainToolBar_->widgetForAction(lockVaultAction_))) {
    lockButton->setObjectName("lockButton");
    lockButton->setMinimumHeight(46);
    lockButton->setIconSize(QSize(24, 24));
    lockButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  }
  if (auto* logoffButton = qobject_cast<QToolButton*>(mainToolBar_->widgetForAction(logoffAction_))) {
    logoffButton->setObjectName("logoffButton");
    logoffButton->setMinimumHeight(46);
    logoffButton->setIconSize(QSize(24, 24));
    logoffButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    logoffButton->setToolTip("Logoff remote session");
  }
  if (auto* exitFsButton = qobject_cast<QToolButton*>(
          mainToolBar_->widgetForAction(exitFullscreenAction_))) {
    exitFsButton->setObjectName("exitFullscreenButton");
    exitFsButton->setMinimumHeight(46);
    exitFsButton->setIconSize(QSize(24, 24));
    exitFsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exitFsButton->setToolTip("Exit full screen");
  }
}

void MainWindow::scheduleTreeReload() {
  if (treeReloadScheduled_) {
    return;
  }
  treeReloadScheduled_ = true;
  QTimer::singleShot(0, this, [this]() {
    treeReloadScheduled_ = false;
    reloadFolderTree();
  });
}

void MainWindow::reloadFolderTree() {
  QStringList expandedKeys;
  QString selectedKey;
  const bool hadExistingTree = (folderTreeModel_ != nullptr && folderTreeModel_->rowCount() > 0);
  bool hasPersistedTreeState = false;
  if (hadExistingTree) {
    captureTreeState(&expandedKeys, &selectedKey);
  } else {
    QSettings settings;
    hasPersistedTreeState =
        settings.contains("ui/tree_expanded_keys") || settings.contains("ui/tree_selected_key");
    expandedKeys = settings.value("ui/tree_expanded_keys").toStringList();
    selectedKey = settings.value("ui/tree_selected_key").toString();
  }

  isReloadingTree_ = true;
  treeMutationGuard_ = true;
  QSignalBlocker modelBlocker(folderTreeModel_);
  folderTreeModel_->removeRows(0, folderTreeModel_->rowCount());

  auto* vaultRoot = new QStandardItem("Vault");
  vaultRoot->setIcon(themedIcon(vaultrdp::ui::AppIcon::Vault, this));
  vaultRoot->setEditable(false);
  vaultRoot->setDragEnabled(false);
  vaultRoot->setDropEnabled(true);
  vaultRoot->setData(kItemTypeVaultRoot, kItemTypeRole);
  vaultRoot->setData(QString(), kItemIdRole);
  vaultRoot->setData(QString(), kItemFolderIdRole);
  vaultRoot->setData(vaultRoot->text(), kItemOriginalNameRole);
  folderTreeModel_->appendRow(vaultRoot);

  const auto folders = repository_->listFolders();
  const auto connections = connectionRepository_->listConnections();
  const auto credentials = credentialRepository_->listCredentials();
  const auto gateways = gatewayRepository_->listGateways();

  QHash<QString, QStandardItem*> folderItemById;
  for (const auto& folder : folders) {
    auto* folderItem = new QStandardItem(folder.name);
    folderItem->setIcon(themedIcon(vaultrdp::ui::AppIcon::Folder, this));
    folderItem->setEditable(true);
    folderItem->setDragEnabled(true);
    folderItem->setDropEnabled(true);
    folderItem->setData(kItemTypeFolder, kItemTypeRole);
    folderItem->setData(folder.id, kItemIdRole);
    folderItem->setData(folder.id, kItemFolderIdRole);
    folderItem->setData(folder.name, kItemOriginalNameRole);
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
    item->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connection, this));
    item->setEditable(true);
    item->setDragEnabled(true);
    item->setDropEnabled(false);
    item->setData(kItemTypeConnection, kItemTypeRole);
    item->setData(connection.id, kItemIdRole);
    item->setData(connection.folderId, kItemFolderIdRole);
    item->setData(connection.name, kItemOriginalNameRole);

    QStandardItem* folderParent = folderItemById.value(connection.folderId, nullptr);
    if (folderParent != nullptr) {
      folderParent->appendRow(item);
    } else {
      vaultRoot->appendRow(item);
    }
  }

  for (const auto& credential : credentials) {
    auto* item = new QStandardItem(credential.name);
    item->setIcon(themedIcon(vaultrdp::ui::AppIcon::Credential, this));
    item->setEditable(true);
    item->setDragEnabled(true);
    item->setDropEnabled(false);
    item->setData(kItemTypeCredential, kItemTypeRole);
    item->setData(credential.id, kItemIdRole);
    item->setData(credential.folderId.has_value() ? credential.folderId.value() : QString(), kItemFolderIdRole);
    item->setData(credential.name, kItemOriginalNameRole);
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
    item->setIcon(themedIcon(vaultrdp::ui::AppIcon::Gateway, this));
    item->setEditable(true);
    item->setDragEnabled(true);
    item->setDropEnabled(false);
    item->setData(kItemTypeGateway, kItemTypeRole);
    item->setData(gateway.id, kItemIdRole);
    item->setData(gateway.folderId.has_value() ? gateway.folderId.value() : QString(), kItemFolderIdRole);
    item->setData(gateway.name, kItemOriginalNameRole);
    QStandardItem* folderParent =
        gateway.folderId.has_value() ? folderItemById.value(gateway.folderId.value(), nullptr) : nullptr;
    if (folderParent != nullptr) {
      folderParent->appendRow(item);
    } else {
      vaultRoot->appendRow(item);
    }
  }

  restoreTreeState(expandedKeys, selectedKey);
  if (!hadExistingTree && !hasPersistedTreeState && expandedKeys.isEmpty()) {
    const QModelIndex rootIndex = folderTreeModel_->index(0, 0);
    if (rootIndex.isValid()) {
      folderTreeView_->setExpanded(rootIndex, true);
    }
  }
  if (treeSearchEdit_ != nullptr && !treeSearchEdit_->text().trimmed().isEmpty()) {
    applyTreeFilter(treeSearchEdit_->text());
  }
  if (folderTreeView_->currentIndex().isValid() == false) {
    const QModelIndex rootIndex = folderTreeModel_->index(0, 0);
    if (rootIndex.isValid()) {
      folderTreeView_->setCurrentIndex(rootIndex);
    }
  }
  updateCreateActionAvailability();
  isReloadingTree_ = false;
  treeMutationGuard_ = false;
}

void MainWindow::applyTreeFilter(const QString& filterText) {
  if (folderTreeModel_ == nullptr || folderTreeView_ == nullptr) {
    return;
  }

  isApplyingTreeFilter_ = true;
  const QString filterLower = filterText.trimmed().toLower();
  const QModelIndex rootIndex = folderTreeModel_->index(0, 0);
  if (!rootIndex.isValid()) {
    isApplyingTreeFilter_ = false;
    return;
  }

  if (filterLower.isEmpty()) {
    std::function<void(const QModelIndex&)> clearHidden = [&](const QModelIndex& parent) {
      const int rows = folderTreeModel_->rowCount(parent);
      for (int row = 0; row < rows; ++row) {
        folderTreeView_->setRowHidden(row, parent, false);
        const QModelIndex child = folderTreeModel_->index(row, 0, parent);
        if (child.isValid()) {
          clearHidden(child);
        }
      }
    };
    clearHidden(QModelIndex());
    QSettings settings;
    restoreTreeState(settings.value("ui/tree_expanded_keys").toStringList(),
                     settings.value("ui/tree_selected_key").toString());
    isApplyingTreeFilter_ = false;
    return;
  }

  const int rootRows = folderTreeModel_->rowCount();
  for (int row = 0; row < rootRows; ++row) {
    const QModelIndex index = folderTreeModel_->index(row, 0);
    const bool keep = applyTreeFilterRecursive(index, filterLower);
    folderTreeView_->setRowHidden(row, QModelIndex(), !keep);
  }
  isApplyingTreeFilter_ = false;
}

bool MainWindow::applyTreeFilterRecursive(const QModelIndex& index, const QString& filterLower) {
  if (!index.isValid()) {
    return false;
  }

  const QString text = index.data(Qt::DisplayRole).toString().toLower();
  bool selfMatch = text.contains(filterLower);
  bool childMatch = false;

  const int rows = folderTreeModel_->rowCount(index);
  for (int row = 0; row < rows; ++row) {
    const QModelIndex child = folderTreeModel_->index(row, 0, index);
    const bool keepChild = applyTreeFilterRecursive(child, filterLower);
    folderTreeView_->setRowHidden(row, index, !keepChild);
    childMatch = childMatch || keepChild;
  }

  const bool keep = selfMatch || childMatch;
  if (keep && childMatch) {
    folderTreeView_->setExpanded(index, true);
  }
  return keep;
}

MainWindow::ThemeMode MainWindow::savedThemeMode() const {
  QSettings settings;
  const int raw = settings.value("ui/theme_mode", static_cast<int>(ThemeMode::System)).toInt();
  if (raw == static_cast<int>(ThemeMode::Dark)) {
    return ThemeMode::Dark;
  }
  if (raw == static_cast<int>(ThemeMode::Light)) {
    return ThemeMode::Light;
  }
  return ThemeMode::System;
}

void MainWindow::setThemeMode(ThemeMode mode) {
  QSettings settings;
  settings.setValue("ui/theme_mode", static_cast<int>(mode));
  applyTheme(mode);
}

void MainWindow::applyTheme(ThemeMode mode) {
  auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
  if (app == nullptr) {
    return;
  }

  ThemeMode effectiveMode = mode;
  if (mode == ThemeMode::System) {
    app->setPalette(QPalette());
    const QColor systemWindow = app->palette().color(QPalette::Window);
    effectiveMode = (systemWindow.lightness() < 128) ? ThemeMode::Dark : ThemeMode::Light;
  }

  app->setStyle("Fusion");
  QPalette palette;
  if (effectiveMode == ThemeMode::Dark) {
      palette.setColor(QPalette::Window, QColor(32, 36, 44));
      palette.setColor(QPalette::WindowText, QColor(225, 228, 235));
      palette.setColor(QPalette::Base, QColor(26, 30, 36));
      palette.setColor(QPalette::AlternateBase, QColor(36, 40, 48));
      palette.setColor(QPalette::ToolTipBase, QColor(245, 245, 245));
      palette.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
      palette.setColor(QPalette::Text, QColor(225, 228, 235));
      palette.setColor(QPalette::Button, QColor(42, 47, 56));
      palette.setColor(QPalette::ButtonText, QColor(225, 228, 235));
      palette.setColor(QPalette::Highlight, QColor(70, 132, 255));
      palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
      palette.setColor(QPalette::Disabled, QPalette::Text, QColor(130, 135, 145));
      palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(130, 135, 145));
  } else {
    palette = QApplication::style()->standardPalette();
    palette.setColor(QPalette::Highlight, QColor(70, 132, 255));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  }
  app->setPalette(palette);

  if (effectiveMode == ThemeMode::Dark) {
      app->setStyleSheet(
        "QMainWindow{background:#1d2129;}"
        "QMenuBar{background:#262b35;color:#dfe4ec;border-bottom:1px solid #313846;}"
        "QMenuBar::item:selected{background:#323a49;}"
        "QMenu{background:#252b35;color:#dfe4ec;border:1px solid #3a4252;}"
        "QMenu::item:selected{background:#3a4252;}"
        "QMenu::item:disabled{color:#7e8897;}"
        "QToolBar#topToolBar{background:#262b35;border-bottom:1px solid #313846;spacing:10px;padding:8px 10px;}"
        "QToolButton#topMenuButton{background:#313846;border:1px solid #465064;border-radius:8px;color:#e0e6f1;padding:0;}"
        "QToolButton#topMenuButton:hover{background:#3b4455;}"
        "QLabel#topBrandLabel{font-size:18px;font-weight:700;color:#e6ebf2;padding-left:4px;padding-right:8px;}"
        "QLineEdit#topSearchEdit{background:#1f2430;border:1px solid #3a4252;border-radius:8px;padding:9px 12px;color:#dfe4ec;min-height:20px;}"
        "QWidget#treeActionBar{background:#232934;border-bottom:1px solid #313846;}"
        "QToolButton#treeActionButton{background:#2f3643;border:1px solid #3f495a;border-radius:7px;padding:0;color:#d9e0eb;}"
        "QToolButton#treeActionButton:disabled{background:#2a303b;border-color:#394250;color:#7e8897;}"
        "QToolButton{background:#333b49;border:1px solid #435066;border-radius:8px;padding:9px 15px;color:#d9e0eb;}"
        "QToolButton#connectButton{background:#2f4f88;border-color:#3e5f9a;color:#ecf2ff;padding:0;}"
        "QToolButton#lockButton{background:#3b414d;border-color:#4a5261;color:#d3dae5;padding:0;}"
        "QToolButton:disabled{background:#2b313d;border-color:#394250;color:#7e8897;}"
        "QTreeView{background:#1d222c;border-right:1px solid #2f3643;padding:6px 4px;show-decoration-selected:1;}"
        "QTreeView::item{padding:5px 6px;border-radius:6px;min-height:22px;}"
        "QTreeView::item:selected{background:#2e5ea8;color:#f2f7ff;}"
        "QTabWidget::pane{border:0;}"
        "QTabBar::tab{background:#2a303b;color:#dbe2eb;padding:8px 12px;border-top-left-radius:8px;border-top-right-radius:8px;}"
        "QTabBar::tab:selected{background:#3a4352;color:#ffffff;}"
        "QPushButton#welcomeCardButton,QToolButton#welcomeCardButton{background:#242a35;border:1px solid #353d4d;border-radius:14px;color:#e0e6ef;font-size:16px;font-weight:650;text-align:center;padding:16px;}"
        "QPushButton#welcomeCardButton:hover,QToolButton#welcomeCardButton:hover{background:#2a3240;border-color:#4268a8;}"
        "QWidget#welcomeCenterBlock{background:transparent;}"
        "QLabel#welcomeTitleLabel{font-size:48px;font-weight:700;color:#e3e8f0;}"
        "QLabel#welcomeSubtitleLabel{font-size:24px;color:#aeb6c3;}"
        "QStatusBar{background:#1f2430;color:#aeb6c3;border-top:1px solid #303846;}");
  } else {
      app->setStyleSheet(
        "QMainWindow{background:#f3f4f7;}"
        "QMenuBar{background:#f0f2f6;color:#2c3440;border-bottom:1px solid #d9dde6;}"
        "QMenuBar::item:selected{background:#e3e7ef;}"
        "QMenu{background:#ffffff;color:#2f3742;border:1px solid #d5dae4;}"
        "QMenu::item:selected{background:#e8edf7;}"
        "QMenu::item:disabled{color:#9aa3b3;}"
        "QToolBar#topToolBar{background:#f0f2f6;border-bottom:1px solid #d9dde6;spacing:10px;padding:8px 10px;}"
        "QToolButton#topMenuButton{background:#e2e6ee;border:1px solid #c8cfdb;border-radius:8px;color:#4f5b6f;padding:0;}"
        "QToolButton#topMenuButton:hover{background:#d8deea;}"
        "QLabel#topBrandLabel{font-size:18px;font-weight:700;color:#273240;padding-left:4px;padding-right:8px;}"
        "QLineEdit#topSearchEdit{background:#ffffff;border:1px solid #cfd5e1;border-radius:8px;padding:9px 12px;color:#2f3742;min-height:20px;}"
        "QWidget#treeActionBar{background:#f2f4f8;border-bottom:1px solid #d9dde6;}"
        "QToolButton#treeActionButton{background:#e8edf5;border:1px solid #d0d8e5;border-radius:7px;padding:0;color:#4f5b6f;}"
        "QToolButton#treeActionButton:disabled{background:#e6ebf3;border-color:#d0d7e4;color:#8f98a8;}"
        "QToolButton{background:#e8edf5;border:1px solid #d0d8e5;border-radius:8px;padding:9px 15px;color:#4f5b6f;}"
        "QToolButton#connectButton{background:#3f7ee8;border-color:#4f8cee;color:#ffffff;padding:0;}"
        "QToolButton#lockButton{background:#e8edf5;border-color:#d0d8e5;color:#4f5b6f;padding:0;}"
        "QToolButton:disabled{background:#e6ebf3;border-color:#d0d7e4;color:#8f98a8;}"
        "QTreeView{background:#f7f8fb;border-right:1px solid #d9dde6;padding:6px 4px;show-decoration-selected:1;}"
        "QTreeView::item{padding:5px 6px;border-radius:6px;min-height:22px;}"
        "QTreeView::item:selected{background:#dbe8ff;color:#233040;}"
        "QTabWidget::pane{border:0;}"
        "QTabBar::tab{background:#e8ecf4;color:#374255;padding:8px 12px;border-top-left-radius:8px;border-top-right-radius:8px;}"
        "QTabBar::tab:selected{background:#ffffff;color:#1f2b3a;}"
        "QPushButton#welcomeCardButton,QToolButton#welcomeCardButton{background:#ffffff;border:1px solid #d9dfe9;border-radius:14px;color:#2f3742;font-size:16px;font-weight:650;text-align:center;padding:16px;}"
        "QPushButton#welcomeCardButton:hover,QToolButton#welcomeCardButton:hover{background:#f6f8fc;border-color:#91b4f2;}"
        "QWidget#welcomeCenterBlock{background:transparent;}"
        "QLabel#welcomeTitleLabel{font-size:48px;font-weight:700;color:#2b3440;}"
        "QLabel#welcomeSubtitleLabel{font-size:24px;color:#677283;}"
        "QStatusBar{background:#f0f2f6;color:#5f6979;border-top:1px solid #d9dde6;}");
  }

  if (newFolderAction_ != nullptr) {
    newFolderAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewFolder, this));
  }
  if (newConnectionAction_ != nullptr) {
    newConnectionAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewConnection, this));
  }
  if (newCredentialAction_ != nullptr) {
    newCredentialAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewCredential, this));
  }
  if (newGatewayAction_ != nullptr) {
    newGatewayAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewGateway, this));
  }
  if (connectAction_ != nullptr) {
    const QModelIndex currentIndex = folderTreeView_ != nullptr ? folderTreeView_->currentIndex() : QModelIndex();
    const bool selectedConnectionActive =
        currentIndex.isValid() && currentIndex.data(kItemTypeRole).toInt() == kItemTypeConnection &&
        hasActiveSessionForConnection(currentIndex.data(kItemIdRole).toString());
    connectAction_->setIcon(
        themedIcon(selectedConnectionActive ? vaultrdp::ui::AppIcon::Disconnect : vaultrdp::ui::AppIcon::Connect,
                   this));
  }
  if (logoffAction_ != nullptr) {
    logoffAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Logoff, this));
  }
  if (exitFullscreenAction_ != nullptr) {
    exitFullscreenAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Disconnect, this));
  }
  if (fullscreenSessionAction_ != nullptr) {
    fullscreenSessionAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connect, this));
  }
  if (treeConnectButton_ != nullptr) {
    treeConnectButton_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connect, this));
    treeConnectButton_->setIconSize(QSize(26, 26));
  }
  if (treeNewConnectionButton_ != nullptr) {
    treeNewConnectionButton_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewConnection, this));
    treeNewConnectionButton_->setIconSize(QSize(26, 26));
  }
  if (treeNewFolderButton_ != nullptr) {
    treeNewFolderButton_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewFolder, this));
    treeNewFolderButton_->setIconSize(QSize(26, 26));
  }
  if (treeNewCredentialButton_ != nullptr) {
    treeNewCredentialButton_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewCredential, this));
    treeNewCredentialButton_->setIconSize(QSize(26, 26));
  }
  if (treeNewGatewayButton_ != nullptr) {
    treeNewGatewayButton_->setIcon(themedIcon(vaultrdp::ui::AppIcon::NewGateway, this));
    treeNewGatewayButton_->setIconSize(QSize(26, 26));
  }
  if (disconnectAction_ != nullptr) {
    disconnectAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Disconnect, this));
  }
  if (disconnectAllAction_ != nullptr) {
    disconnectAllAction_->setIcon(themedIcon(vaultrdp::ui::AppIcon::Disconnect, this));
  }
  if (lockVaultAction_ != nullptr) {
    const bool locked = vaultManager_ != nullptr && vaultManager_->state() == vaultrdp::core::VaultState::Locked;
    lockVaultAction_->setIcon(
        themedIcon(locked ? vaultrdp::ui::AppIcon::Unlock : vaultrdp::ui::AppIcon::Lock, this));
  }
  if (auto* brandIconLabel = findChild<QLabel*>("topBrandIconLabel")) {
    brandIconLabel->setPixmap(themedIcon(vaultrdp::ui::AppIcon::Brand, 44, this).pixmap(44, 44));
  }
  if (auto* topMenuButton = findChild<QToolButton*>("topMenuButton")) {
    topMenuButton->setIcon(themedIcon(vaultrdp::ui::AppIcon::Menu, this));
    topMenuButton->setIconSize(QSize(30, 30));
    topMenuButton->setFixedSize(46, 46);
  }
  if (auto* connectButton = qobject_cast<QToolButton*>(mainToolBar_ != nullptr
                                                             ? mainToolBar_->widgetForAction(connectAction_)
                                                             : nullptr)) {
    connectButton->setIconSize(QSize(24, 24));
    connectButton->setMinimumHeight(46);
    connectButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  }
  if (auto* logoffButton = qobject_cast<QToolButton*>(mainToolBar_ != nullptr
                                                           ? mainToolBar_->widgetForAction(logoffAction_)
                                                           : nullptr)) {
    logoffButton->setIconSize(QSize(24, 24));
    logoffButton->setMinimumHeight(46);
    logoffButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  }
  if (auto* exitFsButton = qobject_cast<QToolButton*>(mainToolBar_ != nullptr
                                                           ? mainToolBar_->widgetForAction(exitFullscreenAction_)
                                                           : nullptr)) {
    exitFsButton->setIconSize(QSize(24, 24));
    exitFsButton->setMinimumHeight(46);
    exitFsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  }
  if (auto* lockButton = qobject_cast<QToolButton*>(mainToolBar_ != nullptr
                                                         ? mainToolBar_->widgetForAction(lockVaultAction_)
                                                         : nullptr)) {
    lockButton->setIconSize(QSize(24, 24));
    lockButton->setMinimumHeight(46);
    lockButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  }
  if (folderTreeModel_ != nullptr) {
    QSignalBlocker modelBlocker(folderTreeModel_);
    std::function<void(const QModelIndex&)> refreshIcons = [&](const QModelIndex& parent) {
      const int rows = folderTreeModel_->rowCount(parent);
      for (int row = 0; row < rows; ++row) {
        const QModelIndex index = folderTreeModel_->index(row, 0, parent);
        if (!index.isValid()) {
          continue;
        }
        QStandardItem* item = folderTreeModel_->itemFromIndex(index);
        if (item != nullptr) {
          const int itemType = index.data(kItemTypeRole).toInt();
          switch (itemType) {
            case kItemTypeVaultRoot:
              item->setIcon(themedIcon(vaultrdp::ui::AppIcon::Vault, this));
              break;
            case kItemTypeFolder:
              item->setIcon(themedIcon(vaultrdp::ui::AppIcon::Folder, this));
              break;
            case kItemTypeConnection:
              item->setIcon(themedIcon(vaultrdp::ui::AppIcon::Connection, this));
              break;
            case kItemTypeGateway:
              item->setIcon(themedIcon(vaultrdp::ui::AppIcon::Gateway, this));
              break;
            case kItemTypeCredential:
              item->setIcon(themedIcon(vaultrdp::ui::AppIcon::Credential, this));
              break;
            default:
              break;
          }
        }
        refreshIcons(index);
      }
    };
    refreshIcons(QModelIndex());
  }
}

void MainWindow::createFolder() {
  qInfo() << "[ui] createFolder requested";
  const bool atVaultLevel = !selectedFolderId().has_value();
  const auto name = promptTextValue(this, atVaultLevel ? "Create a new root folder." : "Create a new subfolder.",
                                    "Folder name");
  if (!name.has_value() || name->trimmed().isEmpty()) {
    return;
  }

  const auto created = repository_->createFolder(name.value(), selectedFolderId());
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
    const auto passphrase = promptPasswordValue(&dialog, "Set vault password:");
    if (!passphrase.has_value() || passphrase->isEmpty()) {
      return;
    }
    const auto confirm = promptPasswordValue(&dialog, "Confirm vault password:");
    if (!confirm.has_value() || confirm->isEmpty()) {
      return;
    }
    if (passphrase.value() != confirm.value()) {
      QMessageBox::warning(&dialog, "Enable Encryption", "Passwords do not match.");
      return;
    }
    if (!vaultManager_->enable(passphrase.value())) {
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
      const auto oldPass = promptPasswordValue(&dialog, "Current password:");
      if (!oldPass.has_value() || oldPass->isEmpty()) {
        return;
      }
      const auto newPass = promptPasswordValue(&dialog, "New password:");
      if (!newPass.has_value() || newPass->isEmpty()) {
        return;
      }
      const auto confirm = promptPasswordValue(&dialog, "Confirm new password:");
      if (!confirm.has_value() || confirm->isEmpty()) {
        return;
      }
      if (newPass.value() != confirm.value()) {
        QMessageBox::warning(&dialog, "Change Password", "New passwords do not match.");
        continue;
      }
      if (!vaultManager_->rotatePassphrase(oldPass.value(), newPass.value())) {
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
    const auto passphrase = promptPasswordValue(this, "Set master passphrase:");
    if (!passphrase.has_value() || passphrase->isEmpty()) {
      return false;
    }

    const auto confirm = promptPasswordValue(this, "Confirm master passphrase:");
    if (!confirm.has_value() || confirm->isEmpty()) {
      return false;
    }

    if (passphrase.value() != confirm.value()) {
      QMessageBox::warning(this, "Enable Vault", "Passphrases do not match.");
      return false;
    }

    if (!vaultManager_->enable(passphrase.value())) {
      QMessageBox::warning(this, "Enable Vault",
                           "Failed to enable vault. Passphrase must include uppercase, lowercase, and number.");
      updateVaultStatus();
      return false;
    }

    updateVaultStatus();
    return true;
  }

  while (true) {
    const auto passphrase = promptPasswordValue(this, "Master passphrase:");
    if (!passphrase.has_value() || passphrase->isEmpty()) {
      return false;
    }

    if (vaultManager_->unlock(passphrase.value())) {
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
  const bool vaultLocked = vaultManager_->state() == vaultrdp::core::VaultState::Locked;
  lockVaultAction_->setVisible(true);
  lockVaultAction_->setText(vaultLocked ? "Unlock" : "Lock");
  lockVaultAction_->setIcon(
      themedIcon(vaultLocked ? vaultrdp::ui::AppIcon::Unlock : vaultrdp::ui::AppIcon::Lock, this));
  lockVaultAction_->setEnabled(encryptionEnabled && (vaultUnlocked || vaultLocked));
  const bool vaultUsable = vaultManager_->state() != vaultrdp::core::VaultState::Locked;
  connectAction_->setEnabled(vaultUsable);
  disconnectAction_->setEnabled(vaultUsable);
  disconnectAllAction_->setEnabled(vaultUsable);
  applyVaultUiState();
  updateCreateActionAvailability();
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
    for (QAction* action : mainToolBar_->actions()) {
      if (action == nullptr) {
        continue;
      }
      if (action == lockVaultAction_) {
        action->setEnabled(true);
      } else {
        action->setEnabled(!locked);
      }
    }
  }
  if (treeSearchEdit_ != nullptr) {
    treeSearchEdit_->setEnabled(!locked);
  }
  if (auto* topMenuButton = findChild<QToolButton*>("topMenuButton")) {
    topMenuButton->setEnabled(!locked);
  }
  if (unlockVaultButton_ != nullptr) {
    unlockVaultButton_->setVisible(false);
    unlockVaultButton_->setEnabled(false);
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

      const auto passphrase = promptPasswordValue(this, "Set vault password:");
      if (!passphrase.has_value() || passphrase->isEmpty()) {
        // Back to Enable/Skip prompt.
        continue;
      }

      const auto confirm = promptPasswordValue(this, "Confirm vault password:");
      if (!confirm.has_value() || confirm->isEmpty()) {
        // Back to Enable/Skip prompt.
        continue;
      }

      if (passphrase.value() != confirm.value()) {
        QMessageBox::warning(this, "Enable Encryption", "Passwords do not match.");
        // Back to Enable/Skip prompt.
        continue;
      }

      if (!vaultManager_->enable(passphrase.value())) {
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

bool MainWindow::isSessionGenerationCurrent(const QString& connectionId, quint64 generation,
                                            const char* eventName) const {
  if (sessionGenerationByConnection_.value(connectionId, 0) == generation) {
    return true;
  }
  qInfo().noquote() << "[session conn=" + connectionId + " gen=" + QString::number(generation) +
                           "] ignoring stale " + QString::fromUtf8(eventName);
  return false;
}

void MainWindow::resetSessionControllerStateForManualConnect(const QString& connectionId) {
  sessionController_->setAutoReconnectBlocked(connectionId, false);
  sessionController_->setAuthFailurePromptCount(connectionId, 0);
  sessionController_->setLastAuthFailureWasGateway(connectionId, false);
  sessionController_->setLastAuthPromptMs(connectionId, 0);
  sessionController_->setReconnectAttempts(connectionId, 0);
  sessionController_->setAutoReconnectArmed(connectionId, false);
}

void MainWindow::closeSessionTabForConnection(const QString& connectionId) {
  QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
  if (tab == nullptr) {
    return;
  }
  const int index = sessionTabWidget_->indexOf(tab);
  if (index >= 0) {
    handleTabCloseRequested(index);
  }
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
  sessionController_->onSessionCreated(connection.id);
  const SessionRuntimeOptions sessionOptions = parseSessionRuntimeOptions(connection.optionsJson);
  sessionClipboardEnabledByConnection_.insert(connection.id, sessionOptions.enableClipboard);

  QSize viewport = sessionWidget->viewportSize();
  viewport.setWidth(qMax(320, viewport.width()));
  viewport.setHeight(qMax(240, viewport.height()));
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
          [this, connectionId = connection.id](int qtKey, quint32 nativeScanCode, bool pressed) {
            auto* activeSession = sessionsByConnection_.value(connectionId, nullptr);
            if (activeSession == nullptr) {
              return;
            }
            activeSession->sendKeyInput(qtKey, nativeScanCode, pressed);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::windowsKeyReleaseRequested, this,
          [this, connectionId = connection.id]() {
            auto* activeSession = sessionsByConnection_.value(connectionId, nullptr);
            if (activeSession == nullptr) {
              return;
            }
            activeSession->sendKeyInput(Qt::Key_Meta, 0, false);
            activeSession->sendKeyInput(Qt::Key_Super_L, 0, false);
            activeSession->sendKeyInput(Qt::Key_Super_R, 0, false);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::modifierResetRequested, this,
          [this, connectionId = connection.id]() {
            auto* activeSession = sessionsByConnection_.value(connectionId, nullptr);
            if (activeSession == nullptr) {
              return;
            }
            activeSession->sendKeyInput(Qt::Key_Meta, 0, false);
            activeSession->sendKeyInput(Qt::Key_Super_L, 0, false);
            activeSession->sendKeyInput(Qt::Key_Super_R, 0, false);
            activeSession->sendKeyInput(Qt::Key_Control, 0, false);
            activeSession->sendKeyInput(Qt::Key_Alt, 0, false);
            activeSession->sendKeyInput(Qt::Key_Shift, 0, false);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::mouseMoveInput, this,
          [this, connectionId = connection.id](int x, int y) {
            auto* activeSession = sessionsByConnection_.value(connectionId, nullptr);
            if (activeSession == nullptr) {
              return;
            }
            activeSession->sendMouseMove(x, y);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::mouseButtonInput, this,
          [this, connectionId = connection.id](Qt::MouseButton button, bool pressed, int x, int y) {
            auto* activeSession = sessionsByConnection_.value(connectionId, nullptr);
            if (activeSession == nullptr) {
              return;
            }
            activeSession->sendMouseButton(button, pressed, x, y);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::wheelInput, this,
          [this, connectionId = connection.id](Qt::Orientation orientation, int delta, int x, int y) {
            auto* activeSession = sessionsByConnection_.value(connectionId, nullptr);
            if (activeSession == nullptr) {
              return;
            }
            activeSession->sendWheel(orientation, delta, x, y);
          });
  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::viewportResizeRequested, this,
          [this, connectionId = connection.id](int width, int height) {
            Q_UNUSED(connectionId);
            Q_UNUSED(width);
            Q_UNUSED(height);
          });

  connect(sessionWidget, &vaultrdp::ui::SessionTabContent::reconnectRequested, this,
          [this, sessionGeneration, sessionWidget](const QString& connectionId) {
            auto* reconnectSession = sessionsByConnection_.value(connectionId, nullptr);
            if (reconnectSession == nullptr) {
              return;
            }
            if (!isSessionGenerationCurrent(connectionId, sessionGeneration, "reconnect request")) {
              return;
            }
            resetSessionControllerStateForManualConnect(connectionId);
            sessionWidget->setTransientStatusText("Reconnecting...");
            reconnectSession->connectSession();
          });

  connect(session, &vaultrdp::protocols::ISession::stateChanged, this,
          [this, connectionId = connection.id, sessionGeneration](vaultrdp::protocols::SessionState state) {
            if (!isSessionGenerationCurrent(connectionId, sessionGeneration, "stateChanged")) {
              return;
            }
            updateSessionTabState(connectionId, state);

            if (state == vaultrdp::protocols::SessionState::Connected) {
              sessionController_->onSessionConnected(connectionId);

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
              if (pendingFullscreenByConnection_.remove(connectionId) > 0) {
                enterSessionFullscreenForConnection(connectionId);
              }
              return;
            }

            if (state == vaultrdp::protocols::SessionState::Disconnected &&
                sessionController_->hasEverConnected(connectionId)) {
              if (sessionController_->isAutoReconnectBlocked(connectionId)) {
                return;
              }
              if (!sessionController_->isAutoReconnectArmed(connectionId)) {
                return;
              }
              const int attempts = sessionController_->reconnectAttempts(connectionId);
              if (attempts >= 3) {
                return;
              }

              sessionController_->setReconnectAttempts(connectionId, attempts + 1);
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
                sessionController_->setAutoReconnectArmed(connectionId, false);
                reconnectSession->connectSession();
              });
            }
          });
  connect(session, &vaultrdp::protocols::ISession::errorOccurred, this,
          [this, connectionId = connection.id, sessionGeneration](const QString& message) {
            if (!isSessionGenerationCurrent(connectionId, sessionGeneration, "error event")) {
              return;
            }
            QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
            auto* content = qobject_cast<vaultrdp::ui::SessionTabContent*>(tab);
            if (content != nullptr) {
              content->setErrorText(formatSessionErrorForDisplay(message));
            }

            if (!isAuthenticationFailureMessage(message)) {
              const QString lowered = message.toLower();
              const bool networkLikeFailure =
                  lowered.contains("transport failed") || lowered.contains("network") ||
                  lowered.contains("proxy_ts_connectfailed") || lowered.contains("proxy connect failed") ||
                  lowered.contains("dns") || lowered.contains("tls");
              if (networkLikeFailure) {
                sessionController_->setAutoReconnectArmed(connectionId, true);
              }
              return;
            }
            const bool gatewayAuthFailure = isGatewayAuthenticationFailureMessage(message);
            sessionController_->setLastAuthFailureWasGateway(connectionId, gatewayAuthFailure);
            sessionController_->setAutoReconnectBlocked(connectionId, true);
            sessionController_->setAutoReconnectArmed(connectionId, false);

            const int promptCount = sessionController_->incrementAuthFailurePromptCount(connectionId);
            if (promptCount > 3) {
              closeSessionTabForConnection(connectionId);
              return;
            }
            if (sessionController_->isAuthPromptActive(connectionId)) {
              return;
            }

            sessionController_->setAuthPromptActive(connectionId, true);
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const int baseDelayMs = qMin(5000, 1000 * promptCount);
            const qint64 earliest = sessionController_->lastAuthPromptMs(connectionId) + baseDelayMs;
            const int delayMs = static_cast<int>(qMax<qint64>(0, earliest - now));
            QTimer::singleShot(delayMs, this, [this, connectionId, sessionGeneration]() {
              if (!isSessionGenerationCurrent(connectionId, sessionGeneration, "auth prompt timer")) {
                sessionController_->setAuthPromptActive(connectionId, false);
                return;
              }
              const auto infoIt = launchInfoByConnection_.find(connectionId);
              if (infoIt == launchInfoByConnection_.end()) {
                sessionController_->setAuthPromptActive(connectionId, false);
                return;
              }

              auto retryInfo = infoIt.value();
              const bool gatewayCredsIndependent =
                  retryInfo.gatewayHost.has_value() &&
                  retryInfo.gatewayCredentialMode !=
                      vaultrdp::model::GatewayCredentialMode::SameAsConnection;
              const bool gatewayAuthFailureNow =
                  sessionController_->lastAuthFailureWasGateway(connectionId);
              const bool promptGatewayCreds = gatewayAuthFailureNow && retryInfo.gatewayHost.has_value();
              std::optional<QString> enteredUsername;
              std::optional<QString> enteredDomain;
              std::optional<QString> enteredPassword;
              sessionController_->setLastAuthPromptMs(connectionId, QDateTime::currentMSecsSinceEpoch());
              const std::optional<QString> suggestedUsername =
                  promptGatewayCreds ? retryInfo.gatewayUsername : retryInfo.username;
              const std::optional<QString> suggestedDomain =
                  promptGatewayCreds ? retryInfo.gatewayDomain : retryInfo.domain;
              const bool accepted =
                  promptForCredentials(suggestedUsername, suggestedDomain, &enteredUsername, &enteredDomain,
                                       &enteredPassword, promptGatewayCreds);
              sessionController_->setAuthPromptActive(connectionId, false);
              if (!accepted) {
                closeSessionTabForConnection(connectionId);
                return;
              }

              const int promptCountSnapshot = sessionController_->authFailurePromptCount(connectionId);
              const bool blockSnapshot = sessionController_->isAutoReconnectBlocked(connectionId);
              const qint64 promptMsSnapshot = sessionController_->lastAuthPromptMs(connectionId);
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

              closeSessionTabForConnection(connectionId);
              addSessionTab(retryInfo);
              sessionController_->setAuthFailurePromptCount(connectionId, promptCountSnapshot);
              sessionController_->setAutoReconnectBlocked(connectionId, blockSnapshot);
              sessionController_->setLastAuthPromptMs(connectionId, promptMsSnapshot);
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
              if (treeMutationGuard_ || QApplication::activeModalWidget() != nullptr) {
                return;
              }
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
              if (treeMutationGuard_ || QApplication::activeModalWidget() != nullptr) {
                return;
              }
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
            if (!isSessionGenerationCurrent(connectionId, sessionGeneration, "remoteLogoff")) {
              return;
            }
            sessionController_->setAutoReconnectBlocked(connectionId, true);
            sessionController_->setAutoReconnectArmed(connectionId, false);
            closeSessionTabForConnection(connectionId);
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
  updateCreateActionAvailability();
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

QString MainWindow::formatSessionErrorForDisplay(const QString& message) const {
  const QString lowered = message.toLower();

  if (lowered.contains("proxy_ts_connectfailed") || lowered.contains("proxy connect failed") ||
      lowered.contains("e_proxy_ts_connectfailed")) {
    return "Gateway connected, but it could not reach the target RDP host. Check destination host/port and gateway "
           "policy.";
  }
  if (lowered.contains("http_status_denied") || lowered.contains("401 unauthorized") ||
      lowered.contains("errconnect_access_denied")) {
    if (isGatewayAuthenticationFailureMessage(message)) {
      return "Gateway authentication failed. Verify gateway username/domain/password.";
    }
    return "Authentication failed. Verify username/domain/password.";
  }
  if (lowered.contains("network transport failed") || lowered.contains("transport failed")) {
    return "Network transport failed. Check network connectivity, gateway reachability, and TLS settings.";
  }
  if (lowered.contains("131094")) {
    return "Connection failed while negotiating through the gateway. Verify gateway credentials and target endpoint.";
  }
  return message;
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
