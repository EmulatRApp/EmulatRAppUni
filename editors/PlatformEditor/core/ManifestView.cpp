// ============================================================================
// PlatformEditor/core/ManifestView.cpp -- see ManifestView.h
// ============================================================================
#include "ManifestView.h"

namespace platedit {

namespace {
std::string memberText(const Node* n, const char* key)
{
    const Node* m = n ? n->member(key) : nullptr;
    return m ? m->text : std::string{};
}
bool isStorageElem(const Node* n)
{
    return n && n->parent && n->parent->kind == NodeKind::Array
        && n->parent->key == "storage";
}
} // namespace

// ---- labels ----------------------------------------------------------------

std::string storageMoniker(const Node* storage)
{
    if (!storage) return {};
    const std::string lbl = memberText(storage, "label");   // 1. explicit override
    if (!lbl.empty()) return lbl;
    std::string media = memberText(storage, "media");       // 2. filename minus ext
    if (!media.empty()) {
        std::size_t slash = media.find_last_of("/\\");
        if (slash != std::string::npos) media = media.substr(slash + 1);
        std::size_t dot = media.find_last_of('.');
        if (dot != std::string::npos && dot > 0) media = media.substr(0, dot);
        return media;
    }
    return {};                                              // 3. none
}

std::string formatLabel(const std::string& fmt, const Node* node,
                        const DeviceCatalog* catalog)
{
    std::string out;
    std::size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] == '{') {
            std::size_t j = fmt.find('}', i);
            if (j == std::string::npos) { out += fmt.substr(i); break; }
            const std::string token = fmt.substr(i + 1, j - i - 1);

            std::string val;
            const Node* m = node ? node->member(token) : nullptr;   // 1. DOM
            if (m && !m->text.empty()) {
                val = m->text;
            } else if (catalog && node) {                            // 2. catalog
                const Node* mdl = node->member("model");
                val = catalog->defaultFor(mdl ? mdl->text : std::string{}, token);
            }
            out += val;                                              // 3. else ""
            i = j + 1;
        } else {
            out += fmt[i++];
        }
    }
    return out;
}

std::string containerLabel(const Node* node, const SchemaPolicy& policy,
                           const DeviceCatalog* catalog)
{
    if (!node) return {};

    if (node->parent == nullptr) {                       // root
        if (const Container* c = policy.containerFor("$"))
            if (!c->labelFormat.empty())
                return formatLabel(c->labelFormat, node, catalog);
        const Node* p = node->member("platform");
        return p ? p->text : std::string("manifest");
    }

    if (node->kind == NodeKind::Array)                    // "key [N]"
        return node->key + " [" + std::to_string(node->children.size()) + "]";

    if (isStorageElem(node)) {                            // moniker + address + type
        const std::string mon  = storageMoniker(node);
        const std::string type = memberText(node, "type");
        const bool scsi = type.rfind("scsi", 0) == 0;
        const std::string addr = scsi
            ? "tgt" + memberText(node, "unit") + " lun" + memberText(node, "lun")
            : "ch"  + memberText(node, "channel") + " unit" + memberText(node, "unit");
        if (mon.empty()) return addr + "  " + type;
        return mon + "  " + addr + "  " + type;
    }

    // object (array element or nested member): policy labelFormat, else fallback
    if (const Container* c = policy.containerFor(node->path()))
        if (!c->labelFormat.empty())
            return formatLabel(c->labelFormat, node, catalog);
    if (!node->key.empty()) return node->key;
    if (const Node* n = node->member("name")) return n->text;
    return node->path();
}

// ---- tree ------------------------------------------------------------------

std::vector<const Node*> TreeView::containerChildren(const Node* n)
{
    std::vector<const Node*> out;
    if (!n) return out;
    for (const auto& c : n->children)
        if (c->isContainer()) out.push_back(c.get());
    return out;
}

TreeView::TreeView(const Document* doc, const SchemaPolicy* policy,
                   const DeviceCatalog* catalog)
    : doc_(doc), policy_(policy), catalog_(catalog)
{
    if (doc_ && doc_->root()) expanded_.insert(doc_->root());   // root open by default
    rebuild();
}

bool TreeView::isExpanded(const Node* n) const
{
    return expanded_.count(n) != 0;
}

void TreeView::setExpanded(const Node* n, bool on)
{
    if (on) expanded_.insert(n);
    else    expanded_.erase(n);
    rebuild();
}

void TreeView::toggle(const Node* n)
{
    setExpanded(n, !isExpanded(n));
}

void TreeView::expandAll()
{
    std::vector<const Node*> stack;
    if (doc_ && doc_->root()) stack.push_back(doc_->root());
    while (!stack.empty()) {
        const Node* n = stack.back(); stack.pop_back();
        expanded_.insert(n);
        for (const Node* c : containerChildren(n)) stack.push_back(c);
    }
    rebuild();
}

void TreeView::collapseAll()
{
    expanded_.clear();
    if (doc_ && doc_->root()) expanded_.insert(doc_->root());
    rebuild();
}

void TreeView::flatten(const Node* n, int depth)
{
    std::vector<const Node*> kids = containerChildren(n);
    TreeRow row;
    row.node       = n;
    row.depth      = depth;
    row.label      = policy_ ? containerLabel(n, *policy_, catalog_) : std::string{};
    row.expandable = !kids.empty();
    row.expanded   = isExpanded(n);
    rows_.push_back(row);

    if (row.expanded)
        for (const Node* c : kids) flatten(c, depth + 1);
}

void TreeView::rebuild()
{
    rows_.clear();
    if (doc_ && doc_->root()) flatten(doc_->root(), 0);
}

// ---- property pane ---------------------------------------------------------

std::vector<PropRow> propertiesOf(Node* container, const SchemaPolicy& policy)
{
    std::vector<PropRow> out;
    if (!container) return out;
    for (auto& c : container->children) {
        if (c->isContainer()) continue;                  // scalars only
        PropRow r;
        r.key   = c->key;
        r.value = c->text;
        r.tier  = policy.tierFor(c->path());
        r.node  = c.get();
        out.push_back(r);
    }
    return out;
}

} // namespace platedit
