// ============================================================================
// PlatformEditor/qt/ManifestModel.h -- QAbstractItemModel over the core DOM
// ============================================================================
// D-026: this model ADAPTS the existing std `Node' tree from OrderedJson; it
// never parses or serializes JSON, so key-order/format preservation (D-003)
// holds by construction -- the core writer is untouched.
//
// Section 5.4 row policy: CONTAINERS ONLY are tree rows (objects/arrays);
// scalars are property-pane fields.  Labels come from the shared core
// view-model (containerLabel), so the Qt tree, the TUI, and the web mockup
// agree on presentation.
// ============================================================================

#ifndef PLATEDIT_QT_MANIFESTMODEL_H
#define PLATEDIT_QT_MANIFESTMODEL_H

#include "core/DeviceCatalog.h"
#include "core/ManifestView.h"
#include "core/OrderedJson.h"
#include "core/SchemaPolicy.h"
#include "core/Validate.h"

#include <QAbstractItemModel>

#include <map>
#include <vector>

namespace platedit {

class ManifestModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit ManifestModel(QObject* parent = nullptr);

    // Non-owning: the document/policy/catalog outlive the model (MainWindow
    // owns them).  Passing a null doc clears the model.
    void setSources(Document* doc, const SchemaPolicy* policy,
                    const DeviceCatalog* catalog);

    // Per-node severity flags from the last validation run (drives row tint).
    void setFindings(const std::vector<Finding>& findings);

    // QAbstractItemModel
    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int         rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int         columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant    data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Node <-> index mapping (issues-pane click-through, selection restore).
    Node*       nodeFromIndex(const QModelIndex& index) const;
    QModelIndex indexFromNode(const Node* node) const;

    // A field of `node' changed: refresh its row label (and its ancestors',
    // whose labels may embed the field via {tokens}).
    void refreshNode(const Node* node);

private:
    Document*                       doc_     = nullptr;
    const SchemaPolicy*             policy_  = nullptr;
    const DeviceCatalog*            catalog_ = nullptr;
    std::map<const Node*, Severity> flags_;

    std::vector<const Node*> kidsOf(const Node* n) const;
    int rowOf(const Node* n) const;
};

} // namespace platedit

#endif // PLATEDIT_QT_MANIFESTMODEL_H
