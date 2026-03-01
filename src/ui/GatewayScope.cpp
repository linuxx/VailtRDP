#include "ui/GatewayScope.hpp"

#include "ui/RootScope.hpp"

namespace vaultrdp::ui {

bool gatewayVisibleForFolder(const vaultrdp::model::Gateway& gateway, const std::optional<QString>& folderId) {
  return gatewayVisibleForFolder(gateway, folderId, {});
}

bool gatewayVisibleForFolder(const vaultrdp::model::Gateway& gateway, const std::optional<QString>& folderId,
                             const std::vector<vaultrdp::model::Folder>& folders) {
  if (gateway.allowAnyFolder) {
    return true;
  }
  const auto rootMap = buildFolderRootMap(folders);
  const auto selectedRoot = rootForFolder(folderId, rootMap);
  const auto gatewayRoot = rootForFolder(gateway.folderId, rootMap);
  return selectedRoot.has_value() && gatewayRoot.has_value() && selectedRoot.value() == gatewayRoot.value();
}

std::vector<std::pair<QString, QString>> gatewayOptionsForFolder(
    const std::vector<vaultrdp::model::Gateway>& gateways, const std::optional<QString>& folderId,
    const std::vector<vaultrdp::model::Folder>& folders,
    const std::optional<QString>& forceIncludeGatewayId) {
  std::vector<std::pair<QString, QString>> out;
  out.reserve(gateways.size());
  for (const auto& gateway : gateways) {
    const bool forceInclude = forceIncludeGatewayId.has_value() && gateway.id == forceIncludeGatewayId.value();
    if (forceInclude || gatewayVisibleForFolder(gateway, folderId, folders)) {
      out.emplace_back(gateway.id, gateway.name);
    }
  }
  return out;
}

}  // namespace vaultrdp::ui
