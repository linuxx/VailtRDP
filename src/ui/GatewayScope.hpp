#pragma once

#include <QString>

#include <optional>
#include <utility>
#include <vector>

#include "core/model/Entities.hpp"

namespace vaultrdp::ui {

bool gatewayVisibleForFolder(const vaultrdp::model::Gateway& gateway, const std::optional<QString>& folderId);
bool gatewayVisibleForFolder(const vaultrdp::model::Gateway& gateway, const std::optional<QString>& folderId,
                             const std::vector<vaultrdp::model::Folder>& folders);

std::vector<std::pair<QString, QString>> gatewayOptionsForFolder(
    const std::vector<vaultrdp::model::Gateway>& gateways, const std::optional<QString>& folderId,
    const std::vector<vaultrdp::model::Folder>& folders,
    const std::optional<QString>& forceIncludeGatewayId = std::nullopt);

}  // namespace vaultrdp::ui
