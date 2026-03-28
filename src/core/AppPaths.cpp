#include "core/AppPaths.hpp"

#include <QDir>
#include <QStandardPaths>

namespace vaultrdp::core {

QString stateDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/VaultRDP";
}

bool ensureStateDirectory() {
  return QDir().mkpath(stateDirectory());
}

}  // namespace vaultrdp::core

