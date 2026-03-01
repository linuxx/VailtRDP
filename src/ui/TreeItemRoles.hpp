#pragma once

#include <Qt>

namespace vaultrdp::ui {

inline constexpr int kItemTypeRole = Qt::UserRole + 100;
inline constexpr int kItemIdRole = Qt::UserRole + 101;
inline constexpr int kItemFolderIdRole = Qt::UserRole + 102;

inline constexpr int kItemTypeFolder = 1;
inline constexpr int kItemTypeConnection = 2;
inline constexpr int kItemTypeVaultRoot = 3;
inline constexpr int kItemTypeGateway = 4;
inline constexpr int kItemTypeCredential = 5;

}  // namespace vaultrdp::ui
