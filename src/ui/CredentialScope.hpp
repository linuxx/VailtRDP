#pragma once

#include <QString>

#include <optional>
#include <utility>
#include <vector>

#include "core/model/Entities.hpp"

namespace vaultrdp::ui {

bool credentialVisibleForFolder(const vaultrdp::model::Credential& credential,
                                const std::optional<QString>& folderId);
bool credentialVisibleForFolder(const vaultrdp::model::Credential& credential,
                                const std::optional<QString>& folderId,
                                const std::vector<vaultrdp::model::Folder>& folders);

std::vector<std::pair<QString, QString>> credentialOptionsForFolder(
    const std::vector<vaultrdp::model::Credential>& credentials, const std::optional<QString>& folderId,
    const std::vector<vaultrdp::model::Folder>& folders,
    const std::optional<QString>& forceIncludeCredentialId = std::nullopt);

}  // namespace vaultrdp::ui
