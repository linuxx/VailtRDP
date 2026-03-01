#include "ui/CredentialScope.hpp"

#include "ui/RootScope.hpp"

namespace vaultrdp::ui {

bool credentialVisibleForFolder(const vaultrdp::model::Credential& credential,
                                const std::optional<QString>& folderId) {
  return credentialVisibleForFolder(credential, folderId, {});
}

bool credentialVisibleForFolder(const vaultrdp::model::Credential& credential,
                                const std::optional<QString>& folderId,
                                const std::vector<vaultrdp::model::Folder>& folders) {
  if (credential.allowAnyFolder) {
    return true;
  }
  const auto rootMap = buildFolderRootMap(folders);
  const auto selectedRoot = rootForFolder(folderId, rootMap);
  const auto credentialRoot = rootForFolder(credential.folderId, rootMap);
  return selectedRoot.has_value() && credentialRoot.has_value() && selectedRoot.value() == credentialRoot.value();
}

std::vector<std::pair<QString, QString>> credentialOptionsForFolder(
    const std::vector<vaultrdp::model::Credential>& credentials, const std::optional<QString>& folderId,
    const std::vector<vaultrdp::model::Folder>& folders,
    const std::optional<QString>& forceIncludeCredentialId) {
  std::vector<std::pair<QString, QString>> out;
  out.reserve(credentials.size());
  for (const auto& credential : credentials) {
    const bool forceInclude = forceIncludeCredentialId.has_value() && credential.id == forceIncludeCredentialId.value();
    if (forceInclude || credentialVisibleForFolder(credential, folderId, folders)) {
      out.emplace_back(credential.id, credential.name);
    }
  }
  return out;
}

}  // namespace vaultrdp::ui
