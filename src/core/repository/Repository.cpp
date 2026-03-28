#include "core/repository/Repository.hpp"

#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "core/DatabaseManager.hpp"
#include "core/repository/RowMappers.hpp"
#include "core/Uuid.hpp"
#include "core/repository/SqlHelpers.hpp"

namespace vaultrdp::core::repository {

Repository::Repository(DatabaseManager* databaseManager) : databaseManager_(databaseManager) {}

std::optional<vaultrdp::model::Folder> Repository::createFolder(
    const QString& name, const std::optional<QString>& parentId, int sortOrder) const {
  if (name.trimmed().isEmpty()) {
    return std::nullopt;
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.isOpen()) {
    qCritical() << "Database is not open for createFolder";
    return std::nullopt;
  }

  vaultrdp::model::Folder folder;
  folder.id = vaultrdp::core::Uuid::v4();
  folder.parentId = parentId;
  folder.name = name.trimmed();
  folder.sortOrder = sortOrder;

  QSqlQuery query(db);
  query.prepare("INSERT INTO folders (id, parent_id, name, sort_order) VALUES (?, ?, ?, ?)");
  query.addBindValue(folder.id);
  query.addBindValue(sql::nullableString(folder.parentId));
  query.addBindValue(folder.name);
  query.addBindValue(folder.sortOrder);

  if (!sql::execOrLog(query, "Failed to insert folder:")) {
    return std::nullopt;
  }

  return folder;
}

bool Repository::renameFolder(const QString& folderId, const QString& newName) const {
  if (folderId.trimmed().isEmpty() || newName.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  QSqlQuery query(db);
  query.prepare("UPDATE folders SET name = ? WHERE id = ?");
  query.addBindValue(newName.trimmed());
  query.addBindValue(folderId);

  if (!sql::execOrLog(query, "Failed to rename folder:")) {
    return false;
  }

  return query.numRowsAffected() > 0;
}

bool Repository::moveFolderToParent(const QString& folderId, const std::optional<QString>& parentId) const {
  if (folderId.trimmed().isEmpty()) {
    return false;
  }
  if (parentId.has_value() && parentId->trimmed().isEmpty()) {
    return false;
  }
  if (parentId.has_value() && parentId.value() == folderId) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.isOpen()) {
    qCritical() << "Database is not open for moveFolderToParent";
    return false;
  }

  if (parentId.has_value()) {
    QSqlQuery subtreeCheck(db);
    subtreeCheck.prepare(
        "WITH RECURSIVE subtree(id) AS ("
        "  SELECT id FROM folders WHERE id = ?"
        "  UNION ALL"
        "  SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id"
        ") "
        "SELECT COUNT(1) FROM subtree WHERE id = ?");
    subtreeCheck.addBindValue(folderId);
    subtreeCheck.addBindValue(parentId.value());
    if (!subtreeCheck.exec() || !subtreeCheck.next()) {
      qCritical() << "Failed to validate folder move target:" << subtreeCheck.lastError().text();
      return false;
    }
    if (subtreeCheck.value(0).toInt() > 0) {
      qCritical() << "Invalid folder move: destination is inside source subtree";
      return false;
    }
  }

  QSqlQuery query(db);
  query.prepare("UPDATE folders SET parent_id = ? WHERE id = ?");
  QVariant parentValue;
  if (parentId.has_value()) {
    parentValue = parentId.value();
  }
  query.addBindValue(parentValue);
  query.addBindValue(folderId);
  if (!sql::execOrLog(query, "Failed to move folder:")) {
    return false;
  }
  return query.numRowsAffected() > 0;
}

bool Repository::deleteFolderRecursive(const QString& folderId) const {
  if (folderId.trimmed().isEmpty()) {
    return false;
  }

  QSqlDatabase db = databaseManager_->database();
  if (!db.transaction()) {
    qCritical() << "Failed to start folder delete transaction:" << db.lastError().text();
    return false;
  }

  QSqlQuery deleteConnections(db);
  deleteConnections.prepare(
      "WITH RECURSIVE subtree(id) AS ("
      "  SELECT ?"
      "  UNION ALL"
      "  SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id"
      ") "
      "DELETE FROM connections WHERE folder_id IN (SELECT id FROM subtree)");
  deleteConnections.addBindValue(folderId);

  if (!sql::execOrLog(deleteConnections, "Failed to delete connections in folder subtree:")) {
    db.rollback();
    return false;
  }

  QSqlQuery clearGatewayFolders(db);
  clearGatewayFolders.prepare(
      "WITH RECURSIVE subtree(id) AS ("
      "  SELECT ?"
      "  UNION ALL"
      "  SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id"
      ") "
      "UPDATE gateways SET folder_id = NULL WHERE folder_id IN (SELECT id FROM subtree)");
  clearGatewayFolders.addBindValue(folderId);

  if (!sql::execOrLog(clearGatewayFolders, "Failed to clear gateway folder bindings in subtree:")) {
    db.rollback();
    return false;
  }

  QSqlQuery clearCredentialFolders(db);
  clearCredentialFolders.prepare(
      "WITH RECURSIVE subtree(id) AS ("
      "  SELECT ?"
      "  UNION ALL"
      "  SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id"
      ") "
      "UPDATE credentials SET folder_id = NULL WHERE folder_id IN (SELECT id FROM subtree)");
  clearCredentialFolders.addBindValue(folderId);

  if (!sql::execOrLog(clearCredentialFolders, "Failed to clear credential folder bindings in subtree:")) {
    db.rollback();
    return false;
  }

  QSqlQuery deleteFolders(db);
  deleteFolders.prepare(
      "WITH RECURSIVE subtree(id) AS ("
      "  SELECT ?"
      "  UNION ALL"
      "  SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id"
      ") "
      "DELETE FROM folders WHERE id IN (SELECT id FROM subtree)");
  deleteFolders.addBindValue(folderId);

  if (!sql::execOrLog(deleteFolders, "Failed to delete folder subtree:")) {
    db.rollback();
    return false;
  }

  if (!db.commit()) {
    qCritical() << "Failed to commit folder delete transaction:" << db.lastError().text();
    db.rollback();
    return false;
  }

  return true;
}

std::vector<vaultrdp::model::Folder> Repository::listFolders() const {
  std::vector<vaultrdp::model::Folder> folders;

  QSqlDatabase db = databaseManager_->database();
  if (!db.isOpen()) {
    qCritical() << "Database is not open for listFolders";
    return folders;
  }

  QSqlQuery query(db);
  query.prepare("SELECT id, parent_id, name, sort_order FROM folders ORDER BY parent_id, sort_order, name");
  if (!sql::execOrLog(query, "Failed to query folders:")) {
    return folders;
  }

  while (query.next()) {
    folders.push_back(rowmap::folderFromQuery(query, 0, 1, 2, 3));
  }

  return folders;
}

}  // namespace vaultrdp::core::repository
