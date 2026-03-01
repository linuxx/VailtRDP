#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class RdpRenderWidget;

namespace vaultrdp::protocols {
enum class SessionState : int;
}

namespace vaultrdp::ui {

class SessionTabContent : public QWidget {
  Q_OBJECT

 public:
  SessionTabContent(QString connectionId, QWidget* parent = nullptr);

  QString connectionId() const;
  void setFrame(const QImage& frame);
  QSize viewportSize() const;
  void setSessionState(vaultrdp::protocols::SessionState state);
  void setErrorText(const QString& message);

 Q_SIGNALS:
  void reconnectRequested(const QString& connectionId);
  void keyInput(int qtKey, quint32 nativeScanCode, bool pressed);
  void mouseMoveInput(int x, int y);
  void mouseButtonInput(Qt::MouseButton button, bool pressed, int x, int y);
  void wheelInput(Qt::Orientation orientation, int delta, int x, int y);
  void viewportResizeRequested(int width, int height);

 private:
 void updateBannerVisibility();

  QString connectionId_;
  QLabel* bannerLabel_;
  QPushButton* reconnectButton_;
  RdpRenderWidget* renderHost_;
  vaultrdp::protocols::SessionState state_;
};

}  // namespace vaultrdp::ui
