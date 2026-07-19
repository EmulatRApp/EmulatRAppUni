// ============================================================================
// PlatformEditor/core/DeviceCatalog.cpp -- see DeviceCatalog.h
// ============================================================================
#include "DeviceCatalog.h"

#include "OrderedJson.h"

#include <cctype>
#include <set>

namespace platedit {

namespace {

std::string memberText(const Node* obj, const char* key)
{
    const Node* m = obj ? obj->member(key) : nullptr;
    return m ? m->text : std::string{};
}

// Parse a hex ("0x..") or decimal integer. Returns false on non-numeric input.
bool parseIntLoose(const std::string& s, long long& out)
{
    std::size_t i = 0, n = s.size();
    while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    std::size_t j = n;
    while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;
    if (i >= j) return false;
    std::string t = s.substr(i, j - i);
    try {
        std::size_t pos = 0;
        if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
            out = std::stoll(t.substr(2), &pos, 16), pos += 2;
        else
            out = std::stoll(t, &pos, 10);
        return pos == t.size();
    } catch (...) {
        return false;
    }
}

// Value equality: numeric when both sides parse as integers (so "0xC693" ==
// "0xc693" and "0x1080" == "4224"), else exact string compare.
bool sameValue(const std::string& a, const std::string& b)
{
    long long x = 0, y = 0;
    if (parseIntLoose(a, x) && parseIntLoose(b, y)) return x == y;
    return a == b;
}

} // namespace

DeviceCatalog DeviceCatalog::load(const std::string& json, std::string& err)
{
    DeviceCatalog cat;
    err.clear();

    ParseError perr;
    Document doc = Document::parse(json, perr);
    if (!perr.ok) {
        err = "catalog JSON parse error: " + perr.message + " @ "
            + std::to_string(perr.offset);
        return cat;
    }
    const Node* root = doc.root();
    if (!root || root->kind != NodeKind::Object) {
        err = "catalog root is not an object";
        return cat;
    }

    if (const Node* v = root->member("catalog_version"))
        cat.version_ = std::atoi(v->text.c_str());

    // ---- pci_models: real silicon; MODEL is the key (must be unique) ----
    std::set<std::string> seen;
    if (const Node* models = root->member("pci_models")) {
        if (models->kind != NodeKind::Array) { err = "'pci_models' is not an array"; return cat; }
        for (const auto& mn : models->children) {
            const Node* o = mn.get();
            if (o->kind != NodeKind::Object) continue;

            CatalogModel m;
            m.model        = memberText(o, "model");
            if (m.model.empty()) { err = "catalog entry with empty 'model'"; return cat; }
            if (!seen.insert(m.model).second) {
                err = "duplicate model '" + m.model + "' (model is the lookup key)";
                return cat;
            }
            m.vendor       = memberText(o, "vendor");
            m.device       = memberText(o, "device");
            m.classCode    = memberText(o, "class_code");
            m.interruptPin = memberText(o, "interrupt_pin");
            m.comment      = memberText(o, "comment");

            if (const Node* bars = o->member("bars")) {
                if (bars->kind == NodeKind::Array)
                    for (const auto& bn : bars->children) {
                        const Node* b = bn.get();
                        if (b->kind != NodeKind::Object) continue;
                        CatalogBar bar;
                        bar.index    = std::atoi(memberText(b, "index").c_str());
                        bar.kind     = memberText(b, "kind");
                        bar.size     = memberText(b, "size");
                        bar.prefetch = (memberText(b, "prefetch") == "true");
                        m.bars.push_back(bar);
                    }
            }
            if (const Node* sup = o->member("supports")) {
                if (sup->kind == NodeKind::Array)
                    for (const auto& sn : sup->children)
                        m.supports.push_back(sn->text);
            }
            cat.models_.push_back(std::move(m));
        }
    }

    // NOTE (Section 7): deliberately NO duplicate vendor/device check -- shared
    // identity across models (cypress_isa/cypress_ide) is correct, not an error.

    // ---- backing_models: keywords with no identity defaults ----
    if (const Node* back = root->member("backing_models")) {
        if (back->kind == NodeKind::Array)
            for (const auto& bn : back->children)
                cat.backing_.push_back(bn->text);
    }
    if (const Node* plats = root->member("platforms")) {
        if (plats->kind == NodeKind::Array)
            for (const auto& pn : plats->children)
                cat.platforms_.push_back(pn->text);
    }

    return cat;
}

const CatalogModel* DeviceCatalog::find(const std::string& model) const
{
    for (const CatalogModel& m : models_)
        if (m.model == model) return &m;
    return nullptr;
}

bool DeviceCatalog::isBacking(const std::string& model) const
{
    for (const std::string& b : backing_)
        if (b == model) return true;
    return false;
}

std::string DeviceCatalog::defaultFor(const std::string& model,
                                      const std::string& field) const
{
    const CatalogModel* m = find(model);
    if (!m) return {};
    if (field == "vendor")        return m->vendor;
    if (field == "device")        return m->device;
    if (field == "class_code")    return m->classCode;
    if (field == "interrupt_pin") return m->interruptPin;
    return {};
}

std::vector<FieldConflict>
DeviceCatalog::conflictsForDevice(const Node* pciDevice) const
{
    std::vector<FieldConflict> out;
    if (!pciDevice) return out;

    const std::string model = memberText(pciDevice, "model");
    if (isBacking(model)) return out;               // backing: no defaults, no V-07
    const CatalogModel* m = find(model);
    if (!m) return out;                             // unknown model: V-08, not V-07

    static const char* kFields[] = { "vendor", "device", "class_code", "interrupt_pin" };
    for (const char* f : kFields) {
        const std::string authored = memberText(pciDevice, f);
        const std::string def      = defaultFor(model, f);
        if (authored.empty() || def.empty()) continue;
        if (!sameValue(authored, def))
            out.push_back(FieldConflict{ f, authored, def });
    }
    return out;
}

} // namespace platedit
