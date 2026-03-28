#include "core/security/PassphrasePolicy.hpp"

#include <QRegularExpression>

namespace vaultrdp::core::security {

bool PassphrasePolicy::isValid(const QString& passphrase, QStringList* violations) {
  QStringList localViolations;

  if (!passphrase.contains(QRegularExpression("[A-Z]"))) {
    localViolations.push_back("Must include at least one uppercase letter");
  }
  if (!passphrase.contains(QRegularExpression("[a-z]"))) {
    localViolations.push_back("Must include at least one lowercase letter");
  }
  if (!passphrase.contains(QRegularExpression("[0-9]"))) {
    localViolations.push_back("Must include at least one number");
  }

  if (violations != nullptr) {
    *violations = localViolations;
  }

  return localViolations.isEmpty();
}

}  // namespace vaultrdp::core::security
