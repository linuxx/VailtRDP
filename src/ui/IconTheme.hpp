#pragma once

#include <QIcon>

class QWidget;

namespace vaultrdp::ui {

enum class AppIcon {
  Brand,
  Vault,
  Folder,
  Connection,
  Gateway,
  Credential,
  NewFolder,
  NewConnection,
  NewGateway,
  NewCredential,
  Menu,
  Connect,
  Logoff,
  Disconnect,
  Lock,
  Unlock,
  Settings,
  Edit,
  Duplicate,
  Rename,
  Delete
};

bool initializeIconTheme();
QIcon themedIcon(AppIcon icon, const QWidget* widget = nullptr);
QIcon themedIcon(AppIcon icon, int pixelSize, const QWidget* widget = nullptr);

}  // namespace vaultrdp::ui
