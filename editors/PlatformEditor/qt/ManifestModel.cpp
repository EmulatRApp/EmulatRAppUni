// ============================================================================
// PlatformEditor/qt/ManifestModel.cpp -- see ManifestModel.h
// ============================================================================

#include "ManifestModel.h"

#include <QBrush>
#include <QColor>

namespace platedit {

ManifestModel::ManifestModel(QObject* parent) : QAbstractItemModel(parent) {}

void ManifestModel::setSources(Document* doc, const SchemaPolicy* policy,
                               const DeviceCatalog* catalog) {
    beginResetModel();
    doc_     = doc;
    policy_  = policy;
    catalog_ = catalog;
    flags_.clear();
    endResetModel();
}

void ManifestModel::setFindings(const std::vector<Finding>& findings) {
    flags_.clear();
    for (const Finding& f : findings) {
        if (!f.node) continue;
        auto it = flags_.find(f.node);
        if (it == flags_.end() || f.sev > it->second) flags_[f.node] = f.sev;
    }
    if (doc_ && doc_->valid())
        emit dataChanged(index(0, 0), index(0, 0),
                         {Qt::ForegroundRole, Qt::DisplayRole});
}

std::vector<const Node*> ManifestModel::kidsOf(const Node* n) const {
    return TreeView::containerChildren(n);
}

int ManifestModel::rowOf(const Node* n) const {
    if (!n || !n->parent) return 0;
    auto sibs = kidsOf(n->parent);
    for (std::size_t i = 0; i < sibs.size(); ++i)
        if (sibs[i] == n) return static_cast<int>(i);
    return 0;
}

QModelIndex ManifestModel::index(int row, int column,
                                 const QModelIndex& parent) const {
    if (!doc_ || !doc_->valid() || column != 0) return {};
    if (!parent.isValid()) {
        // single top-level row: the manifest root
        if (row != 0) return {};
        return createIndex(0, 0, const_cast<Node*>(doc_->root()));
    }
    const Node* p = static_cast<const Node*>(parent.internalPointer());
    auto kids = kidsOf(p);
    if (row < 0 || static_cast<std::size_t>(row) >= kids.size()) return {};
    return createIndex(row, 0, const_cast<Node*>(kids[static_cast<std::size_t>(row)]));
}

QModelIndex ManifestModel::parent(const QModelIndex& child) const {
    if (!child.isValid() || !doc_ || !doc_->valid()) return {};
    const Node* n = static_cast<const Node*>(child.internalPointer());
    if (!n || n == doc_->root()) return {};
    const Node* p = n->parent;
    if (!p) return {};
    if (p == doc_->root()) return createIndex(0, 0, const_cast<Node*>(p));
    return createIndex(rowOf(p), 0, const_cast<Node*>(p));
}

int ManifestModel::rowCount(const QModelIndex& parent) const {
    if (!doc_ || !doc_->valid()) return 0;
    if (!parent.isValid()) return 1;                       // the root row
    const Node* p = static_cast<const Node*>(parent.internalPointer());
    return static_cast<int>(kidsOf(p).size());
}

int ManifestModel::columnCount(const QModelIndex&) const { return 1; }

QVariant ManifestModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !doc_ || !policy_) return {};
    const Node* n = static_cast<const Node*>(index.internalPointer());
    switch (role) {
        case Qt::DisplayRole:
            return QString::fromStdString(
                containerLabel(n, *policy_, catalog_));
        case Qt::ToolTipRole:
            return QString::fromStdString(n->path());
        case Qt::ForegroundRole: {
            auto it = flags_.find(n);
            if (it == flags_.end()) return {};
            switch (it->second) {
                case Severity::Err:  return QBrush(QColor(0xf8, 0x51, 0x49));
                case Severity::Warn: return QBrush(QColor(0xd2, 0x99, 0x22));
                case Severity::Info: return QBrush(QColor(0x58, 0xa6, 0xff));
            }
            return {};
        }
        default: return {};
    }
}

Qt::ItemFlags ManifestModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

Node* ManifestModel::nodeFromIndex(const QModelIndex& index) const {
    if (!index.isValid()) return nullptr;
    return static_cast<Node*>(index.internalPointer());
}

QModelIndex ManifestModel::indexFromNode(const Node* node) const {
    if (!node || !doc_ || !doc_->valid()) return {};
    // Scalars are not rows; navigate to the owning container.
    while (node && !node->isContainer()) node = node->parent;
    if (!node) return {};
    if (node == doc_->root()) return createIndex(0, 0, const_cast<Node*>(node));
    return createIndex(rowOf(node), 0, const_cast<Node*>(node));
}

void ManifestModel::refreshNode(const Node* node) {
    for (const Node* n = node; n; n = n->parent) {
        if (!n->isContainer()) continue;
        QModelIndex ix = indexFromNode(n);
        if (ix.isValid()) emit dataChanged(ix, ix, {Qt::DisplayRole});
    }
}

} // namespace platedit
