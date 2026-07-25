// ============================================================================
// PlatformEditor/core/Path.h -- canonical path parsing + matching (PathMatcher)
// ============================================================================
// Spec: PLATFORM_EDITOR_SPEC.md Section 6.2 (policy is keyed by PATH, never by
// leaf name -- e.g. iic_devices[*].size is `int' while bars[*].size is `hex').
//
// Grammar (Section 6.2):
//     $                       root
//     .key                    object member (alnum + '_')
//     [*]                     any array index (wildcard)
//     [N]                     a specific array index
// e.g.  $.pci_devices[*].bars[*].size          (a policy PATTERN)
//       $.pci_devices[2].bars[0].size          (a concrete node path)
//
// A pattern matches a concrete path segment-for-segment; `[*]' matches any index.
// On multiple matches the policy picks the most specific by literalCount()
// (Section 6.2: "longest-literal-prefix wins on conflict").
// ============================================================================

#ifndef PLATEDIT_PATH_H
#define PLATEDIT_PATH_H

#include <string>
#include <vector>

namespace platedit {

struct PathSeg {
    bool        isIndex  = false;   // false = object key; true = array index
    std::string key;                // when !isIndex
    bool        wildcard = false;   // when isIndex: true = [*]
    int         index    = 0;       // when isIndex && !wildcard
};

class Path {
public:
    // Parse a path/pattern string. On malformed input returns an empty Path and
    // sets ok=false.
    static Path parse(const std::string& s, bool& ok);

    // Does this (a PATTERN) match `concrete' (a wildcard-free node path)?
    bool matches(const Path& concrete) const;

    // Specificity: number of literal segments (keys + fixed indices; wildcards do
    // not count). Higher = more specific.
    int literalCount() const;

    std::size_t size()  const { return segs_.size(); }
    bool        empty() const { return segs_.empty(); }
    const std::vector<PathSeg>& segments() const { return segs_; }

private:
    std::vector<PathSeg> segs_;     // includes the root as segs_[0] (key = "$")
};

} // namespace platedit

#endif // PLATEDIT_PATH_H
