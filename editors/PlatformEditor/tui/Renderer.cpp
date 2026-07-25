// ============================================================================
// PlatformEditor/tui/Renderer.cpp -- see Renderer.h
// ============================================================================
#include "Renderer.h"

#include "core/SchemaPolicy.h"

namespace platedit {

namespace {

const char* kReverseOn  = "\x1b[7m";
const char* kReverseOff = "\x1b[0m";

// Clip or right-pad `s' to exactly `w' visible columns.
std::string fit(const std::string& s, int w)
{
    if (w <= 0) return {};
    std::string t = s;
    if (static_cast<int>(t.size()) > w) {
        t = t.substr(0, w > 1 ? static_cast<std::size_t>(w - 1) : 0) + "~";
    } else {
        t.append(static_cast<std::size_t>(w) - t.size(), ' ');
    }
    return t;
}

std::string treeText(const TreeRow& r)
{
    std::string s;
    s.append(static_cast<std::size_t>(r.depth) * 2, ' ');
    s += r.expandable ? (r.expanded ? "- " : "+ ") : "  ";
    s += r.label;
    return s;
}

std::string propText(const PropRow& r)
{
    std::string key = r.key;
    if (key.size() < 14) key.append(14 - key.size(), ' ');
    key += "  ";                                 // always separate key from value
    std::string val = r.value;
    // A quiet tier hint keeps the pane self-documenting without shouting.
    std::string hint = std::string("  (") + tierName(r.tier) + ")";
    return key + val + hint;
}

} // namespace

std::string renderFrame(const FrameState& s)
{
    const int W = s.width  < 20 ? 20 : s.width;
    const int H = s.height < 8  ? 8  : s.height;
    const int leftW = W * 2 / 5;                 // ~40% for the tree
    const int rightW = W - leftW - 3;            // 3 = " | " separator
    const int bodyRows = H - 4;                  // title + 2 borders + status

    auto hline = [&](char fill) {
        std::string l(1, '+');
        l.append(static_cast<std::size_t>(leftW), fill);
        l += '+';
        l.append(static_cast<std::size_t>(rightW + 2), fill);
        l += '+';
        return l;
    };

    std::string out;
    out += fit(s.title, W); out += '\n';
    out += hline('-');      out += '\n';

    static const std::vector<TreeRow> kNoTree;
    const std::vector<TreeRow>& tree = s.tree ? *s.tree : kNoTree;
    const std::vector<PropRow>* props = s.props;

    for (int i = 0; i < bodyRows; ++i) {
        // left cell
        std::string lcell;
        bool lsel = false;
        if (i < static_cast<int>(tree.size())) {
            lcell = treeText(tree[i]);
            lsel  = (s.active == Pane::Tree && i == s.treeSel);
        }
        // right cell
        std::string rcell;
        bool rsel = false;
        if (props && i < static_cast<int>(props->size())) {
            rcell = propText((*props)[i]);
            rsel  = (s.active == Pane::Props && i == s.propSel);
        }

        std::string lfit = fit(lcell, leftW);
        std::string rfit = fit(rcell, rightW);
        if (s.ansi && lsel) lfit = std::string(kReverseOn) + lfit + kReverseOff;
        if (s.ansi && rsel) rfit = std::string(kReverseOn) + rfit + kReverseOff;
        if (!s.ansi && lsel) lfit = ">" + fit(lcell, leftW - 1);
        if (!s.ansi && rsel) rfit = ">" + fit(rcell, rightW - 1);

        out += '|'; out += lfit; out += " | "; out += rfit; out += '|'; out += '\n';
    }

    out += hline('-'); out += '\n';
    out += fit(s.status, W);
    return out;
}

} // namespace platedit
