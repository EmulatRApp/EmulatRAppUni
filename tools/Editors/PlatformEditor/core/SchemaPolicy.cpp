// ============================================================================
// PlatformEditor/core/SchemaPolicy.cpp -- see SchemaPolicy.h
// ============================================================================
#include "SchemaPolicy.h"

#include "OrderedJson.h"

namespace platedit {

const char* tierName(Tier t)
{
    switch (t) {
    case Tier::Enum:        return "enum";
    case Tier::OpenEnum:    return "openEnum";
    case Tier::Free:        return "free";
    case Tier::Multiline:   return "multiline";
    case Tier::Int:         return "int";
    case Tier::Hex:         return "hex";
    case Tier::Bool:        return "bool";
    case Tier::Size:        return "size";
    case Tier::Path:        return "path";
    case Tier::Passthrough: return "passthrough";
    }
    return "?";
}

namespace {

bool tierFromString(const std::string& s, Tier& out)
{
    if (s == "enum")        { out = Tier::Enum;        return true; }
    if (s == "openEnum")    { out = Tier::OpenEnum;    return true; }
    if (s == "free")        { out = Tier::Free;        return true; }
    if (s == "multiline")   { out = Tier::Multiline;   return true; }
    if (s == "int")         { out = Tier::Int;         return true; }
    if (s == "hex")         { out = Tier::Hex;         return true; }
    if (s == "bool")        { out = Tier::Bool;        return true; }
    if (s == "size")        { out = Tier::Size;        return true; }
    if (s == "path")        { out = Tier::Path;        return true; }
    if (s == "passthrough") { out = Tier::Passthrough; return true; }
    return false;
}

// Read an optional scalar member's decoded text ("" if absent).
std::string memberText(const Node* obj, const char* key)
{
    const Node* m = obj ? obj->member(key) : nullptr;
    return m ? m->text : std::string{};
}

} // namespace

SchemaPolicy SchemaPolicy::load(const std::string& json, std::string& err)
{
    SchemaPolicy policy;
    err.clear();

    ParseError perr;
    Document doc = Document::parse(json, perr);
    if (!perr.ok) {
        err = "policy JSON parse error: " + perr.message + " @ "
            + std::to_string(perr.offset);
        return policy;
    }
    const Node* root = doc.root();
    if (!root || root->kind != NodeKind::Object) {
        err = "policy root is not an object";
        return policy;
    }

    if (const Node* v = root->member("policy_version"))
        policy.version_ = std::atoi(v->text.c_str());

    // ---- rules ----
    if (const Node* rules = root->member("rules")) {
        if (rules->kind != NodeKind::Array) { err = "'rules' is not an array"; return policy; }
        for (const auto& rn : rules->children) {
            const Node* o = rn.get();
            if (o->kind != NodeKind::Object) continue;

            const std::string pathStr = memberText(o, "path");
            const std::string tierStr = memberText(o, "tier");
            bool pok = false;
            Path pat = Path::parse(pathStr, pok);
            Tier tier;
            if (!pok) { err = "bad rule path: '" + pathStr + "'"; return policy; }
            if (!tierFromString(tierStr, tier)) {
                err = "bad tier '" + tierStr + "' for path '" + pathStr + "'";
                return policy;
            }

            Rule rule;
            rule.pattern        = pat;
            rule.tier           = tier;
            rule.catalogDefault = memberText(o, "catalogDefault");
            if (const Node* ro = o->member("readOnly"))
                rule.readOnly = (ro->text == "true");
            if (const Node* mn = o->member("min"))
                rule.min = std::atoll(mn->text.c_str());
            if (const Node* mx = o->member("max"))
                rule.max = std::atoll(mx->text.c_str());
            if (const Node* vals = o->member("values")) {
                if (vals->kind == NodeKind::Array)
                    for (const auto& ve : vals->children)
                        rule.values.push_back(ve->text);
            }
            policy.rules_.push_back(std::move(rule));
        }
    }

    // ---- containers ----
    if (const Node* conts = root->member("containers")) {
        if (conts->kind == NodeKind::Array) {
            for (const auto& cn : conts->children) {
                const Node* o = cn.get();
                if (o->kind != NodeKind::Object) continue;
                const std::string pathStr = memberText(o, "path");
                bool pok = false;
                Path pat = Path::parse(pathStr, pok);
                if (!pok) { err = "bad container path: '" + pathStr + "'"; return policy; }
                Container c;
                c.pattern     = pat;
                c.labelFormat = memberText(o, "labelFormat");
                policy.containers_.push_back(std::move(c));
            }
        }
    }

    return policy;
}

const Rule* SchemaPolicy::resolve(const std::string& concretePath) const
{
    bool ok = false;
    Path concrete = Path::parse(concretePath, ok);
    if (!ok) return nullptr;

    const Rule* best = nullptr;
    int bestScore = -1, bestLen = -1;
    for (const Rule& r : rules_) {
        if (!r.pattern.matches(concrete)) continue;
        const int score = r.pattern.literalCount();          // longest-literal-prefix wins
        const int len   = static_cast<int>(r.pattern.size());
        if (score > bestScore || (score == bestScore && len > bestLen)) {
            best = &r; bestScore = score; bestLen = len;
        }
    }
    return best;
}

Tier SchemaPolicy::tierFor(const std::string& concretePath) const
{
    const Rule* r = resolve(concretePath);
    return r ? r->tier : Tier::Passthrough;
}

const Container* SchemaPolicy::containerFor(const std::string& concretePath) const
{
    bool ok = false;
    Path concrete = Path::parse(concretePath, ok);
    if (!ok) return nullptr;

    const Container* best = nullptr;
    int bestScore = -1;
    for (const Container& c : containers_) {
        if (!c.pattern.matches(concrete)) continue;
        const int score = c.pattern.literalCount();
        if (score > bestScore) { best = &c; bestScore = score; }
    }
    return best;
}

} // namespace platedit
