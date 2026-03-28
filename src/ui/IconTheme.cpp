#include "ui/IconTheme.hpp"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QLoggingCategory>
#include <QPainter>
#include <QWidget>

namespace vaultrdp::ui {
namespace {

QString g_fontFamily;
bool g_fontInitialized = false;

QChar glyphForIcon(AppIcon icon) {
  switch (icon) {
    case AppIcon::Brand:
      return QChar(0xf3ed);  // fa-shield-halved
    case AppIcon::Vault:
      return QChar(0xf1c0);  // fa-database
    case AppIcon::Folder:
    case AppIcon::NewFolder:
      return QChar(0xf07b);  // fa-folder
    case AppIcon::Connection:
    case AppIcon::NewConnection:
      return QChar(0xf390);  // fa-desktop
    case AppIcon::Gateway:
    case AppIcon::NewGateway:
      return QChar(0xf3ed);  // fa-shield-halved
    case AppIcon::Credential:
      return QChar(0xf084);  // fa-key
    case AppIcon::NewCredential:
      return QChar(0xf2bb);  // fa-address-card
    case AppIcon::Menu:
      return QChar(0xf0c9);  // fa-bars
    case AppIcon::Connect:
      return QChar(0xf04b);  // fa-play
    case AppIcon::Logoff:
      return QChar(0xf2f5);  // fa-right-from-bracket / sign-out-alt
    case AppIcon::Disconnect:
      return QChar(0xf127);  // fa-unlink
    case AppIcon::Lock:
      return QChar(0xf023);  // fa-lock
    case AppIcon::Unlock:
      return QChar(0xf3c1);  // fa-lock-open
    case AppIcon::Settings:
      return QChar(0xf013);  // fa-gear
    case AppIcon::Edit:
      return QChar(0xf044);  // fa-pen-to-square
    case AppIcon::Duplicate:
      return QChar(0xf24d);  // fa-clone
    case AppIcon::Rename:
      return QChar(0xf303);  // fa-pencil
    case AppIcon::Delete:
      return QChar(0xf2ed);  // fa-trash-can
  }
  return QChar();
}

QIcon iconFromGlyph(QChar glyph, const QColor& color, int pixelSize = 24) {
  const int iconPx = qMax(16, pixelSize);
  QPixmap pixmap(iconPx, iconPx);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(color);

  QFont font(g_fontFamily);
  font.setPixelSize(qMax(12, static_cast<int>(iconPx * 0.72)));
  painter.setFont(font);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(glyph));
  painter.end();

  return QIcon(pixmap);
}

QColor colorForIcon(AppIcon icon, const QWidget* widget) {
  QColor base = QApplication::palette().color(QPalette::WindowText);
  if (widget != nullptr) {
    base = widget->palette().color(QPalette::WindowText);
  }
  switch (icon) {
    case AppIcon::Brand:
      return QColor(66, 141, 255);
    case AppIcon::Vault:
    case AppIcon::Folder:
    case AppIcon::NewFolder:
      return QColor(233, 187, 72);
    case AppIcon::Connection:
    case AppIcon::NewConnection:
    case AppIcon::Connect:
      return QColor(66, 141, 255);
    case AppIcon::Logoff:
      return QColor(224, 120, 64);
    case AppIcon::Gateway:
    case AppIcon::NewGateway:
      return QColor(83, 205, 143);
    case AppIcon::Credential:
      return QColor(239, 185, 65);
    case AppIcon::NewCredential:
      return QColor(239, 185, 65);
    case AppIcon::Menu: {
      Q_UNUSED(widget);
      const QColor bg = QApplication::palette().color(QPalette::Window);
      return (bg.lightness() < 128) ? QColor(245, 248, 255) : QColor(58, 68, 84);
    }
    case AppIcon::Lock:
      return QColor(233, 187, 72);
    case AppIcon::Unlock:
      return QColor(233, 187, 72);
    case AppIcon::Delete:
      return QColor(220, 95, 95);
    default:
      return base;
  }
}

}  // namespace

bool initializeIconTheme() {
  if (g_fontInitialized) {
    return !g_fontFamily.isEmpty();
  }
  g_fontInitialized = true;

  int fontId = QFontDatabase::addApplicationFont(":/fonts/fa-solid-900.ttf");
  if (fontId < 0) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("../resources/fonts/fa-solid-900.ttf"),
        QDir(appDir).filePath("../../resources/fonts/fa-solid-900.ttf"),
        "/home/cbeagle/Apps/VaultRDP/resources/fonts/fa-solid-900.ttf",
    };
    for (const QString& candidate : candidates) {
      if (!QFileInfo::exists(candidate)) {
        continue;
      }
      fontId = QFontDatabase::addApplicationFont(candidate);
      if (fontId >= 0) {
        break;
      }
    }
  }
  if (fontId < 0) {
    qWarning() << "[icons] failed to load Font Awesome font";
    return false;
  }
  const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
  if (families.isEmpty()) {
    return false;
  }
  g_fontFamily = families.first();
  return true;
}

QIcon themedIcon(AppIcon icon, const QWidget* widget) {
  const QColor color = colorForIcon(icon, widget);
  if (!initializeIconTheme()) {
    Q_UNUSED(icon);
    Q_UNUSED(color);
    return QIcon();
  }

  const QChar glyph = glyphForIcon(icon);
  if (glyph.isNull()) {
    qWarning() << "[icons] unknown glyph mapping for icon";
    return QIcon();
  }
  return iconFromGlyph(glyph, color, 24);
}

QIcon themedIcon(AppIcon icon, int pixelSize, const QWidget* widget) {
  const QColor color = colorForIcon(icon, widget);
  if (!initializeIconTheme()) {
    Q_UNUSED(icon);
    Q_UNUSED(color);
    return QIcon();
  }

  const QChar glyph = glyphForIcon(icon);
  if (glyph.isNull()) {
    qWarning() << "[icons] unknown glyph mapping for icon";
    return QIcon();
  }
  return iconFromGlyph(glyph, color, pixelSize);
}

}  // namespace vaultrdp::ui
