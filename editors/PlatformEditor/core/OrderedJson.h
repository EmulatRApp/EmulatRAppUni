// ============================================================================
// PlatformEditor/core/OrderedJson.h -- order- and format-preserving JSON DOM
// ============================================================================
// Project : EmulatR PlatformEditor (manifest authoring/validation tool)
// Spec    : PLATFORM_EDITOR_SPEC.md (SPEC-PLATED-001) -- T-01
//
// Qt-free by design (pure std, C++20).  This is the DOM + I/O layer of Section
// 5.1: it parses a platform manifest into an owned Node tree in AUTHORED ORDER
// and re-emits it while preserving byte-for-byte formatting except where a
// scalar was explicitly edited.
//
// Key-order preservation (Section 5.3) is MANDATORY: this parser is hand-rolled
// recursive descent and never routes through a sorting JSON map.
//
// REFINEMENT to Section 5.3 (flagged for sign-off): rather than a generative
// pretty-printer (2-space indent), the writer is FORMAT-PRESERVING.  Each value
// node records its source byte span; emit() copies the original source and
// splices in new text only for `dirty' scalars.  Consequences:
//   - zero edits  -> output is byte-identical to input (stronger than the
//                    spec's "diff-clean modulo indentation normalization");
//   - one edit    -> exactly the edited token changes -> a one-line diff, which
//                    is what the Section 10 litmus ("edit one slot -> one line")
//                    actually requires on these non-canonical hand-authored files.
// Indentation normalization (tab -> 2 spaces) is therefore a separate, opt-in
// pass, not something the writer does implicitly.
// ============================================================================

#ifndef PLATEDIT_ORDEREDJSON_H
#define PLATEDIT_ORDEREDJSON_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace platedit {

enum class NodeKind { Object, Array, String, Number, Bool, Null };

// One value in the manifest.  Objects and arrays own their children in authored
// order.  [begin,end) is the byte span of this value's token(s) in the source:
// for a scalar, the literal itself; for a container, from '{'/'[' to the byte
// just past the matching '}'/']'.  Whitespace, commas, colons and keys live in
// the *gaps* between spans and are reproduced verbatim by emit().
struct Node {
    NodeKind    kind       = NodeKind::Null;
    std::string key;                 // decoded object-member key; "" for array elems / root
    std::string text;                // decoded scalar value ("" for containers): for String
                                     // the unescaped content; for Number/Bool/Null the literal
    std::size_t begin      = 0;      // byte offset of this value in the source
    std::size_t end        = 0;      // one past the last byte of this value
    Node*       parent     = nullptr;
    std::vector<std::unique_ptr<Node>> children;   // AUTHORED ORDER (members / elements)
    int         originIndex = 0;     // ordinal among siblings (array index; stable id)

    bool        dirty      = false;  // scalar was edited -> emit `edited' instead of source
    std::string edited;              // replacement token (must be valid JSON) when dirty

    // Structural editing (T-08): a container whose child LIST changed is
    // `structDirty'; emit() REGENERATES that container's text (2-space
    // indent), while clean descendants still copy their source spans.  A node
    // with begin==end==0 and a parent is SYNTHETIC (created by the editor,
    // no source span); its token is generated from kind/text.
    bool structDirty = false;

    bool isContainer() const noexcept {
        return kind == NodeKind::Object || kind == NodeKind::Array;
    }
    bool isSynthetic() const noexcept {
        return begin == 0 && end == 0 && parent != nullptr;
    }

    // ---- structural mutation (marks this container structDirty) ------------
    // Create a detached node (scalar text = decoded value; containers empty).
    static std::unique_ptr<Node> make(NodeKind kind, std::string key,
                                      std::string text = "");
    // Insert `child' at `at' (npos = append); returns the raw pointer.
    Node* insertChild(std::unique_ptr<Node> child,
                      std::size_t at = static_cast<std::size_t>(-1));
    // Remove the child at index `i' (true on success).
    bool  removeChildAt(std::size_t i);
    // Index of a direct child, or -1.
    int   indexOfChild(const Node* c) const;

    // Canonical path from the root, e.g. "$.pci_devices[2].slot" (Section 6.1).
    std::string path() const;

    // Child lookups (return nullptr if absent / wrong kind).  Convenience for
    // tests and, later, the model layer.
    Node*       member(const std::string& key);
    const Node* member(const std::string& key) const;
    Node*       elem(std::size_t index);
    const Node* elem(std::size_t index) const;
};

// Deep copy of a subtree (detached: parent=nullptr).  Source spans are kept,
// so a clone emitted within the SAME Document reuses the original bytes.
std::unique_ptr<Node> cloneNode(const Node& n);

// Parse outcome.  On failure `ok' is false and the tree is empty.
struct ParseError {
    bool        ok      = true;
    std::string message;
    std::size_t offset  = 0;         // byte offset where parsing failed
};

// A parsed manifest: owns the source buffer and the root node.
class Document {
public:
    // Parse `source' (moved in).  On error returns an empty Document and fills
    // `err'; never throws to the caller.
    static Document parse(std::string source, ParseError& err);

    const Node* root()   const noexcept { return root_.get(); }
    Node*       root()         noexcept { return root_.get(); }
    const std::string& source() const noexcept { return source_; }

    // Format-preserving serialization (see header comment).  With no dirty
    // nodes this returns source() byte-for-byte.
    std::string emit() const;

    bool valid() const noexcept { return root_ != nullptr; }

private:
    std::string           source_;
    std::unique_ptr<Node> root_;
};

} // namespace platedit

#endif // PLATEDIT_ORDEREDJSON_H
