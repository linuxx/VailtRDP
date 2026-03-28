#include "ui/SessionWorkspace.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QTabWidget>
#include <QWidget>

#include "protocols/RdpSession.hpp"
#include "ui/SessionRuntimeOptions.hpp"
#include "ui/SessionTabContent.hpp"

namespace vaultrdp::ui {

using vaultrdp::ui::parseSessionRuntimeOptions;

SessionWorkspace::SessionWorkspace(QTabWidget* sessionTabWidget, QWidget* welcomeTab, QObject* sessionParent)
    : sessionTabWidget_(sessionTabWidget),
      welcomeTab_(welcomeTab),
      sessionParent_(sessionParent),
      controller_(std::make_unique<SessionController>()) {}

SessionController* SessionWorkspace::controller() {
  return controller_.get();
}

const SessionController* SessionWorkspace::controller() const {
  return controller_.get();
}

std::optional<SessionHandle> SessionWorkspace::createSessionTab(
    const vaultrdp::core::repository::ConnectionLaunchInfo& launchInfo) {
  if (sessionTabWidget_ == nullptr) {
    return std::nullopt;
  }

  const auto& connection = launchInfo.connection;
  const quint64 sessionGeneration = ++sessionGenerationCounter_;
  sessionGenerationByConnection_[connection.id] = sessionGeneration;
  qInfo().noquote() << "[session conn=" + connection.id + " gen=" + QString::number(sessionGeneration) +
                           "] addSessionTab name="
                    << connection.name;

  auto* sessionWidget = new SessionTabContent(connection.id, sessionTabWidget_);
  sessionWidget->setProperty("connection_id", connection.id);

  const int tabIndex = sessionTabWidget_->addTab(sessionWidget, connection.name);
  sessionTabWidget_->setCurrentIndex(tabIndex);
  sessionTabsByConnection_.insert(connection.id, sessionWidget);
  launchInfoByConnection_.insert(connection.id, launchInfo);
  controller_->onSessionCreated(connection.id);

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
                                                      sessionParent_);
  sessionsByConnection_.insert(connection.id, session);

  QObject::connect(sessionWidget, &vaultrdp::ui::SessionTabContent::keyInput, session,
          [session](int qtKey, quint32 nativeScanCode, bool pressed) {
            session->sendKeyInput(qtKey, nativeScanCode, pressed);
          });
  QObject::connect(sessionWidget, &vaultrdp::ui::SessionTabContent::windowsKeyReleaseRequested, session, [session]() {
    session->sendKeyInput(Qt::Key_Meta, 0, false);
    session->sendKeyInput(Qt::Key_Super_L, 0, false);
    session->sendKeyInput(Qt::Key_Super_R, 0, false);
  });
  QObject::connect(sessionWidget, &vaultrdp::ui::SessionTabContent::modifierResetRequested, session, [session]() {
    session->sendKeyInput(Qt::Key_Meta, 0, false);
    session->sendKeyInput(Qt::Key_Super_L, 0, false);
    session->sendKeyInput(Qt::Key_Super_R, 0, false);
    session->sendKeyInput(Qt::Key_Control, 0, false);
    session->sendKeyInput(Qt::Key_Alt, 0, false);
    session->sendKeyInput(Qt::Key_Shift, 0, false);
  });
  QObject::connect(sessionWidget, &vaultrdp::ui::SessionTabContent::mouseMoveInput, session,
          [session](int x, int y) { session->sendMouseMove(x, y); });
  QObject::connect(sessionWidget, &vaultrdp::ui::SessionTabContent::mouseButtonInput, session,
          [session](Qt::MouseButton button, bool pressed, int x, int y) {
            session->sendMouseButton(button, pressed, x, y);
          });
  QObject::connect(sessionWidget, &vaultrdp::ui::SessionTabContent::wheelInput, session,
          [session](Qt::Orientation orientation, int delta, int x, int y) {
            session->sendWheel(orientation, delta, x, y);
          });
  QObject::connect(sessionWidget, &vaultrdp::ui::SessionTabContent::viewportResizeRequested, session,
          [session](int width, int height) {
            session->resizeSession(width, height);
          });

  return SessionHandle{connection.id, sessionGeneration, sessionWidget, session};
}

bool SessionWorkspace::hasActiveSessionForConnection(const QString& connectionId) const {
  QWidget* tab = sessionTabsByConnection_.value(connectionId, nullptr);
  if (tab == nullptr || sessionTabWidget_ == nullptr) {
    return false;
  }
  return sessionTabWidget_->indexOf(tab) >= 0;
}

QWidget* SessionWorkspace::tabForConnection(const QString& connectionId) const {
  return sessionTabsByConnection_.value(connectionId, nullptr);
}

SessionTabContent* SessionWorkspace::contentForConnection(const QString& connectionId) const {
  return qobject_cast<SessionTabContent*>(tabForConnection(connectionId));
}

vaultrdp::protocols::RdpSession* SessionWorkspace::sessionForConnection(const QString& connectionId) const {
  return sessionsByConnection_.value(connectionId, nullptr);
}

std::optional<vaultrdp::core::repository::ConnectionLaunchInfo> SessionWorkspace::launchInfoForConnection(
    const QString& connectionId) const {
  const auto it = launchInfoByConnection_.find(connectionId);
  if (it == launchInfoByConnection_.end()) {
    return std::nullopt;
  }
  return it.value();
}

vaultrdp::core::repository::ConnectionLaunchInfo* SessionWorkspace::mutableLaunchInfoForConnection(
    const QString& connectionId) {
  auto it = launchInfoByConnection_.find(connectionId);
  if (it == launchInfoByConnection_.end()) {
    return nullptr;
  }
  return &it.value();
}

bool SessionWorkspace::isClipboardEnabledForConnection(const QString& connectionId) const {
  return sessionClipboardEnabledByConnection_.value(connectionId, true);
}

std::optional<QString> SessionWorkspace::currentSessionConnectionId() const {
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

void SessionWorkspace::ensureWelcomeTab() {
  if (sessionTabWidget_ == nullptr) {
    return;
  }

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

void SessionWorkspace::updateSessionTabState(const QString& connectionId,
                                             vaultrdp::protocols::SessionState state) {
  auto* content = contentForConnection(connectionId);
  if (content != nullptr) {
    content->setSessionState(state);
  }
}

void SessionWorkspace::syncClipboardToFocusedSession(const QString& clipboardText) {
  const auto connectionId = currentSessionConnectionId();
  if (!connectionId.has_value() || !isClipboardEnabledForConnection(connectionId.value())) {
    return;
  }

  auto* session = sessionForConnection(connectionId.value());
  if (session != nullptr) {
    session->setLocalClipboardText(clipboardText);
  }
}

bool SessionWorkspace::closeSessionForConnection(const QString& connectionId) {
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
  return closeSessionAt(index).has_value();
}

std::optional<QString> SessionWorkspace::closeSessionAt(int index) {
  if (sessionTabWidget_ == nullptr || index < 0 || index >= sessionTabWidget_->count()) {
    return std::nullopt;
  }

  QWidget* tab = sessionTabWidget_->widget(index);
  if (tab == nullptr || tab == welcomeTab_) {
    return std::nullopt;
  }

  const QString connectionId = tab->property("connection_id").toString();
  if (!connectionId.isEmpty()) {
    sessionGenerationByConnection_[connectionId] = ++sessionGenerationCounter_;
    qInfo().noquote() << "[session conn=" + connectionId + "] close requested generation advanced to"
                      << sessionGenerationByConnection_.value(connectionId);
    auto* session = sessionsByConnection_.take(connectionId);
    if (session != nullptr) {
      session->disconnectSession();
      session->deleteLater();
    }
    controller_->onSessionClosed(connectionId);
    sessionClipboardEnabledByConnection_.remove(connectionId);
    launchInfoByConnection_.remove(connectionId);
    sessionTabsByConnection_.remove(connectionId);
  }

  sessionTabWidget_->removeTab(index);
  tab->deleteLater();
  return connectionId.isEmpty() ? std::nullopt : std::optional<QString>(connectionId);
}

bool SessionWorkspace::isSessionGenerationCurrent(const QString& connectionId, quint64 generation,
                                                  const char* eventName) const {
  if (sessionGenerationByConnection_.value(connectionId, 0) == generation) {
    return true;
  }
  qInfo().noquote() << "[session conn=" + connectionId + " gen=" + QString::number(generation) +
                           "] ignoring stale " + QString::fromUtf8(eventName);
  return false;
}

}  // namespace vaultrdp::ui
