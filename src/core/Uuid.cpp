#include "core/Uuid.hpp"

#include <QUuid>

namespace vaultrdp::core {

QString Uuid::v4() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}  // namespace vaultrdp::core
