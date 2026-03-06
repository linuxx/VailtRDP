#include "ui/SessionTabContent.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "protocols/ISession.hpp"
#include "ui/RdpRenderWidget.hpp"

namespace vaultrdp::ui {

SessionTabContent::SessionTabContent(QString connectionId, QWidget* parent)
    : QWidget(parent),
      connectionId_(std::move(connectionId)),
      bannerLabel_(new QLabel(this)),
      reconnectButton_(new QPushButton("Reconnect", this)),
      renderHost_(new RdpRenderWidget(this)),
      state_(vaultrdp::protocols::SessionState::Initialized),
      transientStatusText_() {
  auto* layout = new QVBoxLayout(this);

  auto* bannerRow = new QHBoxLayout();
  bannerRow->addWidget(bannerLabel_);
  bannerRow->addStretch();
  bannerRow->addWidget(reconnectButton_);

  bannerLabel_->setStyleSheet("color: #9A3412; font-weight: 600;");
  renderHost_->setMinimumSize(480, 260);
  renderHost_->setStyleSheet("background-color: #111111; border: 1px solid #333333;");

  layout->addLayout(bannerRow);
  layout->addWidget(renderHost_, 1);

  connect(reconnectButton_, &QPushButton::clicked, [this]() {
    Q_EMIT reconnectRequested(connectionId_);
  });
  connect(renderHost_, &RdpRenderWidget::keyInput, this, &SessionTabContent::keyInput);
  connect(renderHost_, &RdpRenderWidget::windowsKeyReleaseRequested, this,
          &SessionTabContent::windowsKeyReleaseRequested);
  connect(renderHost_, &RdpRenderWidget::modifierResetRequested, this,
          &SessionTabContent::modifierResetRequested);
  connect(renderHost_, &RdpRenderWidget::mouseMoveInput, this, &SessionTabContent::mouseMoveInput);
  connect(renderHost_, &RdpRenderWidget::mouseButtonInput, this, &SessionTabContent::mouseButtonInput);
  connect(renderHost_, &RdpRenderWidget::wheelInput, this, &SessionTabContent::wheelInput);
  connect(renderHost_, &RdpRenderWidget::viewportResized, this, &SessionTabContent::viewportResizeRequested);

  setSessionState(state_);
}

QString SessionTabContent::connectionId() const {
  return connectionId_;
}

void SessionTabContent::setFrame(const QImage& frame) {
  renderHost_->setFrame(frame);
}

QSize SessionTabContent::viewportSize() const {
  return renderHost_->size();
}

void SessionTabContent::setSessionState(vaultrdp::protocols::SessionState state) {
  state_ = state;
  switch (state_) {
    case vaultrdp::protocols::SessionState::Initialized:
      bannerLabel_->setText("");
      break;
    case vaultrdp::protocols::SessionState::Connecting:
      bannerLabel_->setText(transientStatusText_);
      break;
    case vaultrdp::protocols::SessionState::Connected:
      transientStatusText_.clear();
      bannerLabel_->setText("");
      break;
    case vaultrdp::protocols::SessionState::Disconnected:
      transientStatusText_.clear();
      if (bannerLabel_->text().isEmpty()) {
        bannerLabel_->setText("Session disconnected.");
      }
      break;
    case vaultrdp::protocols::SessionState::Error:
      transientStatusText_.clear();
      if (bannerLabel_->text().isEmpty()) {
        bannerLabel_->setText("Connection error.");
      }
      break;
  }

  updateBannerVisibility();
}

void SessionTabContent::setErrorText(const QString& message) {
  if (message.trimmed().isEmpty()) {
    return;
  }
  transientStatusText_.clear();
  bannerLabel_->setText(message);
  updateBannerVisibility();
}

void SessionTabContent::setTransientStatusText(const QString& message) {
  transientStatusText_ = message.trimmed();
  if (state_ == vaultrdp::protocols::SessionState::Connecting) {
    bannerLabel_->setText(transientStatusText_);
  }
  updateBannerVisibility();
}

void SessionTabContent::updateBannerVisibility() {
  const bool showStatus =
      state_ == vaultrdp::protocols::SessionState::Connecting && !transientStatusText_.isEmpty();
  const bool showBanner = showStatus || state_ == vaultrdp::protocols::SessionState::Error ||
                          state_ == vaultrdp::protocols::SessionState::Disconnected;
  bannerLabel_->setVisible(showBanner);
  reconnectButton_->setVisible(state_ == vaultrdp::protocols::SessionState::Error ||
                               state_ == vaultrdp::protocols::SessionState::Disconnected);
}

}  // namespace vaultrdp::ui
