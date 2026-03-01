#include "ui/FolderTreeView.hpp"

#include <QDropEvent>
#include <QModelIndex>
#include <QString>

#include "ui/TreeItemRoles.hpp"

namespace vaultrdp::ui {

FolderTreeView::FolderTreeView(QWidget* parent) : QTreeView(parent) {}

void FolderTreeView::setDropCallback(DropCallback callback) {
  dropCallback_ = std::move(callback);
}

void FolderTreeView::startDrag(Qt::DropActions supportedActions) {
  dragPayload_ = DragPayload();
  const QModelIndex index = currentIndex();
  if (index.isValid()) {
    dragPayload_.itemType = index.data(kItemTypeRole).toInt();
    dragPayload_.itemId = index.data(kItemIdRole).toString().trimmed();
    dragPayload_.sourceFolderId = parentFolderIdForIndex(index.parent());
  }
  if (dragPayload_.itemId.isEmpty()) {
    return;
  }
  QTreeView::startDrag(supportedActions);
}

void FolderTreeView::dropEvent(QDropEvent* event) {
  DragPayload payload = dragPayload_;
  dragPayload_ = DragPayload();

  const QModelIndex dropIndex = indexAt(event->position().toPoint());
  const auto dropPos = dropIndicatorPosition();

  if (!dropCallback_.has_value() || payload.itemId.isEmpty()) {
    event->ignore();
    return;
  }

  const std::optional<QString> destinationFolderId = destinationFolderFromDrop(dropIndex, dropPos);
  dropCallback_.value()(payload, destinationFolderId);
  event->acceptProposedAction();
}

std::optional<QString> FolderTreeView::destinationFolderFromDrop(const QModelIndex& dropIndex,
                                                                 DropIndicatorPosition dropPosition) const {
  if (dropPosition == OnViewport) {
    return std::nullopt;
  }

  if (dropPosition == OnItem && dropIndex.isValid() && dropIndex.data(kItemTypeRole).toInt() == kItemTypeFolder) {
    const QString folderId = dropIndex.data(kItemIdRole).toString().trimmed();
    if (!folderId.isEmpty()) {
      return folderId;
    }
    return std::nullopt;
  }

  return parentFolderIdForIndex(dropIndex.parent());
}

std::optional<QString> FolderTreeView::parentFolderIdForIndex(const QModelIndex& parentIndex) {
  if (!parentIndex.isValid()) {
    return std::nullopt;
  }
  if (parentIndex.data(kItemTypeRole).toInt() != kItemTypeFolder) {
    return std::nullopt;
  }
  const QString id = parentIndex.data(kItemIdRole).toString().trimmed();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  return id;
}

}  // namespace vaultrdp::ui
