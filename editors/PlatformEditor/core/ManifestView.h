// ============================================================================
// PlatformEditor/core/ManifestView.h -- view-model over the manifest DOM
// ============================================================================
// Qt-free (pure std). This is the tree/property presentation logic of Section
// 5.4 and 8, factored out of any UI toolkit so it is unit-testable headless and
// shared by the TUI (and any future frontend):
//
//   - containerLabel() / formatLabel(): synthesize a row label from the policy
//     labelFormat, resolving {tokens} DOM -> catalog -> "" (Section 6.5 / D-012).
//   - TreeView: flatten the DOM into visible rows, CONTAINERS ONLY (Section 5.4:
//     scalars are property-pane fields, not tree rows), with expand/collapse.
//   - propertiesOf(): the scalar fields of a selected container, with tiers.
// ============================================================================

#ifndef PLATEDIT_MANIFESTVIEW_H
#define PLATEDIT_MANIFESTVIEW_H

#include "DeviceCatalog.h"
#include "OrderedJson.h"
#include "SchemaPolicy.h"

#include <set>
#include <string>
#include <vector>

namespace platedit {

// Substitute {token}s in `fmt' using `node's members (DOM first), then the
// catalog default for the node's model, then "" (Section 6.5 order 1-2-3).
std::string formatLabel(const std::string& fmt, const Node* node,
                        const DeviceCatalog* catalog);

// The device moniker of a storage node: the explicit `label' override if set,
// else the media filename minus directory and extension, else "".  This is the
// friendly name shown for disks/CDs (e.g. "OpenVMS_v82" from "Alpha/OpenVMS_v82.iso").
std::string storageMoniker(const Node* storage);

// A display label for a container node: root -> "{platform}", array -> "key [N]",
// storage -> "{moniker}  {address}  {type}", object -> policy labelFormat.
std::string containerLabel(const Node* node, const SchemaPolicy& policy,
                           const DeviceCatalog* catalog);

// One visible tree row.
struct TreeRow {
    const Node* node       = nullptr;
    int         depth      = 0;
    std::string label;
    bool        expandable = false;   // has container children
    bool        expanded   = false;
};

// Flattened container tree with expand/collapse state. The root is always shown
// and starts expanded.
class TreeView {
public:
    TreeView(const Document* doc, const SchemaPolicy* policy,
             const DeviceCatalog* catalog);

    void rebuild();                              // recompute rows from state
    const std::vector<TreeRow>& rows() const { return rows_; }

    void expandAll();
    void collapseAll();                          // root stays expanded
    void setExpanded(const Node* n, bool on);
    void toggle(const Node* n);
    bool isExpanded(const Node* n) const;

    // Container children only (objects/arrays); scalars are excluded (Section 5.4).
    static std::vector<const Node*> containerChildren(const Node* n);

private:
    const Document*       doc_;
    const SchemaPolicy*   policy_;
    const DeviceCatalog*  catalog_;
    std::set<const Node*> expanded_;
    std::vector<TreeRow>  rows_;

    void flatten(const Node* n, int depth);
};

// One property-pane field.
struct PropRow {
    std::string key;
    std::string value;
    Tier        tier = Tier::Passthrough;
    Node*       node = nullptr;        // the scalar node (for editing)
};

// Scalar fields of a container node, in authored order, with resolved tiers.
std::vector<PropRow> propertiesOf(Node* container, const SchemaPolicy& policy);

} // namespace platedit

#endif // PLATEDIT_MANIFESTVIEW_H
