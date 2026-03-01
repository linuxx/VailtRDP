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
      state_(vaultrdp::protocols::SessionState::Initialized) {
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
      bannerLabel_->setText("");
      break;
    case vaultrdp::protocols::SessionState::Connected:
      bannerLabel_->setText("");
      break;
    case vaultrdp::protocols::SessionState::Disconnected:
      if (bannerLabel_->text().isEmpty()) {
        bannerLabel_->setText("Session disconnected.");
      }
      break;
    case vaultrdp::protocols::SessionState::Error:
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
  bannerLabel_->setText(message);
  updateBannerVisibility();
}

void SessionTabContent::updateBannerVisibility() {
  const bool showBanner =
      state_ == vaultrdp::protocols::SessionState::Error || state_ == vaultrdp::protocols::SessionState::Disconnected;
  bannerLabel_->setVisible(showBanner);
  reconnectButton_->setVisible(showBanner);
}

}  // namespace vaultrdp::ui
