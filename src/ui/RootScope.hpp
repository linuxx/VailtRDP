#pragma once

#include <QHash>
#include <QString>

#include <optional>
#include <vector>

#include "core/model/Entities.hpp"

namespace vaultrdp::ui {

QHash<QString, QString> buildFolderRootMap(const std::vector<vaultrdp::model::Folder>& folders);
std::optional<QString> rootForFolder(const std::optional<QString>& folderId,
                                     const QHash<QString, QString>& folderRootMap);

}  // namespace vaultrdp::ui
