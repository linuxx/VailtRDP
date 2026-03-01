#include "ui/IconTheme.hpp"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QStyle>
#include <QWidget>

namespace vaultrdp::ui {
namespace {

QString g_fontFamily;
bool g_fontInitialized = false;

QIcon fallbackIcon(AppIcon icon) {
  QStyle* style = QApplication::style();
  if (style == nullptr) {
    return QIcon();
  }

  switch (icon) {
    case AppIcon::Vault:
      return style->standardIcon(QStyle::SP_DriveHDIcon);
    case AppIcon::Folder:
    case AppIcon::NewFolder:
      return style->standardIcon(QStyle::SP_DirIcon);
    case AppIcon::Connection:
    case AppIcon::NewConnection:
      return style->standardIcon(QStyle::SP_ComputerIcon);
    case AppIcon::Gateway:
    case AppIcon::NewGateway:
      return style->standardIcon(QStyle::SP_DriveNetIcon);
    case AppIcon::Credential:
    case AppIcon::NewCredential:
      return style->standardIcon(QStyle::SP_FileDialogInfoView);
    case AppIcon::Connect:
      return style->standardIcon(QStyle::SP_MediaPlay);
    case AppIcon::Disconnect:
      return style->standardIcon(QStyle::SP_BrowserStop);
    case AppIcon::Lock:
      return style->standardIcon(QStyle::SP_MessageBoxWarning);
    case AppIcon::Settings:
      return style->standardIcon(QStyle::SP_FileDialogDetailedView);
    case AppIcon::Edit:
      return style->standardIcon(QStyle::SP_FileDialogDetailedView);
    case AppIcon::Duplicate:
      return style->standardIcon(QStyle::SP_FileDialogNewFolder);
    case AppIcon::Rename:
      return style->standardIcon(QStyle::SP_FileDialogContentsView);
    case AppIcon::Delete:
      return style->standardIcon(QStyle::SP_TrashIcon);
  }
  return QIcon();
}

QChar glyphForIcon(AppIcon icon) {
  switch (icon) {
    case AppIcon::Vault:
      return QChar(0xf3c5);  // fa-vault
    case AppIcon::Folder:
    case AppIcon::NewFolder:
      return QChar(0xf07b);  // fa-folder
    case AppIcon::Connection:
    case AppIcon::NewConnection:
      return QChar(0xf390);  // fa-desktop
    case AppIcon::Gateway:
    case AppIcon::NewGateway:
      return QChar(0xf6ff);  // fa-network-wired
    case AppIcon::Credential:
    case AppIcon::NewCredential:
      return QChar(0xf084);  // fa-key
    case AppIcon::Connect:
      return QChar(0xf0c1);  // fa-link
    case AppIcon::Disconnect:
      return QChar(0xf127);  // fa-unlink
    case AppIcon::Lock:
      return QChar(0xf023);  // fa-lock
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

QIcon iconFromGlyph(QChar glyph, const QColor& color) {
  QPixmap pixmap(20, 20);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(color);

  QFont font(g_fontFamily);
  font.setPixelSize(14);
  painter.setFont(font);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(glyph));
  painter.end();

  return QIcon(pixmap);
}

}  // namespace

bool initializeIconTheme() {
  if (g_fontInitialized) {
    return !g_fontFamily.isEmpty();
  }
  g_fontInitialized = true;

  const int fontId = QFontDatabase::addApplicationFont(":/fonts/fa-solid-900.ttf");
  if (fontId < 0) {
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
  if (!initializeIconTheme()) {
    return fallbackIcon(icon);
  }

  QColor color = QApplication::palette().color(QPalette::WindowText);
  if (widget != nullptr) {
    color = widget->palette().color(QPalette::WindowText);
  }

  const QChar glyph = glyphForIcon(icon);
  if (glyph.isNull()) {
    return fallbackIcon(icon);
  }
  return iconFromGlyph(glyph, color);
}

}  // namespace vaultrdp::ui

