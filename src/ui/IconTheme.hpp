#pragma once

#include <QIcon>

class QWidget;

namespace vaultrdp::ui {

enum class AppIcon {
  Vault,
  Folder,
  Connection,
  Gateway,
  Credential,
  NewFolder,
  NewConnection,
  NewGateway,
  NewCredential,
  Connect,
  Disconnect,
  Lock,
  Settings,
  Edit,
  Duplicate,
  Rename,
  Delete
};

bool initializeIconTheme();
QIcon themedIcon(AppIcon icon, const QWidget* widget = nullptr);

}  // namespace vaultrdp::ui
