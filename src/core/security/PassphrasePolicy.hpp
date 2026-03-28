#pragma once

#include <QString>
#include <QStringList>

namespace vaultrdp::core::security {

class PassphrasePolicy {
 public:
  static bool isValid(const QString& passphrase, QStringList* violations = nullptr);
};

}  // namespace vaultrdp::core::security
