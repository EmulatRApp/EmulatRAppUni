// ============================================================================
// PlatformEditor/core/Path.cpp -- see Path.h
// ============================================================================
#include "Path.h"

#include <cctype>

namespace platedit {

Path Path::parse(const std::string& s, bool& ok)
{
    ok = false;
    Path path;
    std::size_t i = 0;

    if (i >= s.size() || s[i] != '$') return Path{};
    { PathSeg root; root.isIndex = false; root.key = "$"; path.segs_.push_back(root); }
    ++i;

    while (i < s.size()) {
        char c = s[i];
        if (c == '.') {
            ++i;
            std::size_t start = i;
            while (i < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
                ++i;
            if (i == start) return Path{};          // empty key
            PathSeg seg; seg.isIndex = false; seg.key = s.substr(start, i - start);
            path.segs_.push_back(seg);
        } else if (c == '[') {
            ++i;
            PathSeg seg; seg.isIndex = true;
            if (i < s.size() && s[i] == '*') { seg.wildcard = true; ++i; }
            else {
                std::size_t start = i;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                if (i == start) return Path{};       // '[' not followed by * or digits
                seg.wildcard = false;
                seg.index    = std::stoi(s.substr(start, i - start));
            }
            if (i >= s.size() || s[i] != ']') return Path{};
            ++i;
            path.segs_.push_back(seg);
        } else {
            return Path{};                           // stray character
        }
    }

    ok = true;
    return path;
}

bool Path::matches(const Path& concrete) const
{
    if (segs_.size() != concrete.segs_.size()) return false;
    for (std::size_t k = 0; k < segs_.size(); ++k) {
        const PathSeg& a = segs_[k];              // pattern
        const PathSeg& b = concrete.segs_[k];     // concrete
        if (a.isIndex != b.isIndex) return false;
        if (!a.isIndex) {
            if (a.key != b.key) return false;
        } else if (!a.wildcard) {                 // fixed index in pattern
            if (b.wildcard || a.index != b.index) return false;
        }
        // a.wildcard index matches any concrete index
    }
    return true;
}

int Path::literalCount() const
{
    int n = 0;
    for (const PathSeg& s : segs_) {
        if (!s.isIndex) ++n;                       // keys (and root) are literal
        else if (!s.wildcard) ++n;                 // fixed indices are literal
    }
    return n;
}

} // namespace platedit
