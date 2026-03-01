#pragma once

#include <QImage>
#include <QWidget>

class RdpRenderWidget : public QWidget {
  Q_OBJECT

 public:
  explicit RdpRenderWidget(QWidget* parent = nullptr);

  void setFrame(const QImage& frame);
  QSize frameSize() const;

 Q_SIGNALS:
  void keyInput(int qtKey, quint32 nativeScanCode, bool pressed);
  void mouseMoveInput(int x, int y);
  void mouseButtonInput(Qt::MouseButton button, bool pressed, int x, int y);
  void wheelInput(Qt::Orientation orientation, int delta, int x, int y);
  void viewportResized(int width, int height);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  QPoint mapToFrameCoordinates(const QPointF& widgetPos) const;
  QImage frame_;
};
