#include "ui/RootScope.hpp"

#include <QSet>

namespace vaultrdp::ui {

QHash<QString, QString> buildFolderRootMap(const std::vector<vaultrdp::model::Folder>& folders) {
  QHash<QString, std::optional<QString>> parentById;
  parentById.reserve(static_cast<int>(folders.size()));
  for (const auto& folder : folders) {
    parentById.insert(folder.id, folder.parentId);
  }

  QHash<QString, QString> rootById;
  rootById.reserve(static_cast<int>(folders.size()));

  for (const auto& folder : folders) {
    QString current = folder.id;
    QString last = current;
    QSet<QString> visited;
    while (parentById.contains(current) && parentById.value(current).has_value() &&
           !parentById.value(current).value().trimmed().isEmpty()) {
      if (visited.contains(current)) {
        break;
      }
      visited.insert(current);
      const QString parent = parentById.value(current).value();
      last = parent;
      current = parent;
    }
    rootById.insert(folder.id, last);
  }
  return rootById;
}

std::optional<QString> rootForFolder(const std::optional<QString>& folderId,
                                     const QHash<QString, QString>& folderRootMap) {
  if (!folderId.has_value() || folderId->trimmed().isEmpty()) {
    return std::nullopt;
  }
  const QString root = folderRootMap.value(folderId.value());
  if (root.trimmed().isEmpty()) {
    return std::nullopt;
  }
  return root;
}

}  // namespace vaultrdp::ui
