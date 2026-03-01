#include "ui/RdpRenderWidget.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

RdpRenderWidget::RdpRenderWidget(QWidget* parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAutoFillBackground(false);
  setMinimumSize(640, 360);
}

void RdpRenderWidget::setFrame(const QImage& frame) {
  frame_ = frame;
  update();
}

QSize RdpRenderWidget::frameSize() const {
  return frame_.size();
}

void RdpRenderWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), QColor(12, 12, 12));

  if (!frame_.isNull()) {
    painter.drawImage(rect(), frame_);
  }
}

void RdpRenderWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  Q_EMIT viewportResized(width(), height());
}

void RdpRenderWidget::keyPressEvent(QKeyEvent* event) {
  Q_EMIT keyInput(event->key(), event->nativeScanCode(), true);
  event->accept();
}

void RdpRenderWidget::keyReleaseEvent(QKeyEvent* event) {
  Q_EMIT keyInput(event->key(), event->nativeScanCode(), false);
  event->accept();
}

void RdpRenderWidget::mouseMoveEvent(QMouseEvent* event) {
  const QPoint p = mapToFrameCoordinates(event->position());
  Q_EMIT mouseMoveInput(p.x(), p.y());
  event->accept();
}

void RdpRenderWidget::mousePressEvent(QMouseEvent* event) {
  setFocus(Qt::MouseFocusReason);
  const QPoint p = mapToFrameCoordinates(event->position());
  Q_EMIT mouseButtonInput(event->button(), true, p.x(), p.y());
  event->accept();
}

void RdpRenderWidget::mouseReleaseEvent(QMouseEvent* event) {
  const QPoint p = mapToFrameCoordinates(event->position());
  Q_EMIT mouseButtonInput(event->button(), false, p.x(), p.y());
  event->accept();
}

void RdpRenderWidget::wheelEvent(QWheelEvent* event) {
  const QPoint p = mapToFrameCoordinates(event->position());
  const QPoint angle = event->angleDelta();
  if (angle.y() != 0) {
    Q_EMIT wheelInput(Qt::Vertical, angle.y(), p.x(), p.y());
  } else if (angle.x() != 0) {
    Q_EMIT wheelInput(Qt::Horizontal, angle.x(), p.x(), p.y());
  }
  event->accept();
}

QPoint RdpRenderWidget::mapToFrameCoordinates(const QPointF& widgetPos) const {
  const QRect widgetRect = rect();
  if (frame_.isNull() || frame_.width() <= 0 || frame_.height() <= 0 || widgetRect.width() <= 0 ||
      widgetRect.height() <= 0) {
    return QPoint(int(widgetPos.x()), int(widgetPos.y()));
  }

  const double sx = static_cast<double>(frame_.width()) / static_cast<double>(widgetRect.width());
  const double sy = static_cast<double>(frame_.height()) / static_cast<double>(widgetRect.height());

  int x = int(widgetPos.x() * sx);
  int y = int(widgetPos.y() * sy);
  x = qBound(0, x, frame_.width() - 1);
  y = qBound(0, y, frame_.height() - 1);
  return QPoint(x, y);
}
