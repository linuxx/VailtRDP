#pragma once

#include <QString>
#include <QWidget>

#include <optional>

namespace vaultrdp::ui {

struct CredentialPromptResult {
  std::optional<QString> username;
  std::optional<QString> domain;
  std::optional<QString> password;
};

bool promptForCredentials(QWidget* parent, const std::optional<QString>& suggestedUsername,
                          const std::optional<QString>& suggestedDomain, bool forGateway,
                          CredentialPromptResult* resultOut);

}  // namespace vaultrdp::ui

