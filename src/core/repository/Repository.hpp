#pragma once

#include <QString>

#include <optional>
#include <vector>

#include "core/model/Entities.hpp"

class DatabaseManager;

namespace vaultrdp::core::repository {

class Repository {
 public:
  explicit Repository(DatabaseManager* databaseManager);

  std::optional<vaultrdp::model::Folder> createFolder(const QString& name, const std::optional<QString>& parentId, int sortOrder = 0) const;
  bool renameFolder(const QString& folderId, const QString& newName) const;
  bool moveFolderToParent(const QString& folderId, const std::optional<QString>& parentId) const;
  bool deleteFolderRecursive(const QString& folderId) const;
  std::vector<vaultrdp::model::Folder> listFolders() const;

 private:
  DatabaseManager* databaseManager_;
};

}  // namespace vaultrdp::core::repository
