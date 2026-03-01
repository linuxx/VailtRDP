#pragma once

#include <QTreeView>

#include <functional>
#include <optional>

class QDropEvent;
class QModelIndex;
class QString;

namespace vaultrdp::ui {

class FolderTreeView : public QTreeView {
  Q_OBJECT

 public:
  struct DragPayload {
    int itemType = 0;
    QString itemId;
    std::optional<QString> sourceFolderId;
  };

  using DropCallback = std::function<void(const DragPayload&, const std::optional<QString>& destinationFolderId)>;

  explicit FolderTreeView(QWidget* parent = nullptr);
  void setDropCallback(DropCallback callback);

 protected:
  void startDrag(Qt::DropActions supportedActions) override;
  void dropEvent(QDropEvent* event) override;

 private:
  std::optional<QString> destinationFolderFromDrop(const QModelIndex& dropIndex,
                                                   DropIndicatorPosition dropPosition) const;
  static std::optional<QString> parentFolderIdForIndex(const QModelIndex& parentIndex);

  DragPayload dragPayload_;
  std::optional<DropCallback> dropCallback_;
};

}  // namespace vaultrdp::ui

