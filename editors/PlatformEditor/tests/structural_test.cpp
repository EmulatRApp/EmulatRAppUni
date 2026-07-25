// ============================================================================
// PlatformEditor/tests/structural_test.cpp -- T-08 structural-writer gate
// ============================================================================
// Exercises Node::insertChild / removeChildAt / cloneNode + the regenerating
// emit path:
//   1. add a synthetic storage row to a controller  -> reparse sees it
//   2. duplicate an existing row                    -> reparse sees the copy
//   3. delete a row                                 -> reparse no longer sees it
//   4. untouched top-level sections keep their source bytes verbatim
// Usage: structural_test <manifest.json>
// ============================================================================

#include "core/OrderedJson.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace platedit;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static std::string slurp(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s manifest.json\n", argv[0]); return 2; }
    std::string src = slurp(argv[1]);
    if (src.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }

    ParseError err;
    Document doc = Document::parse(src, err);
    check(err.ok && doc.valid(), "manifest parses");
    if (!doc.valid()) return 1;

    Node* pciArr = doc.root()->member("pci_devices");
    check(pciArr && pciArr->kind == NodeKind::Array, "pci_devices found");
    if (!pciArr) return 1;

    // Find a controller with a storage[] array (any will do).
    Node* ctrl = nullptr;
    Node* storage = nullptr;
    for (auto& up : pciArr->children) {
        if (Node* s = up->member("storage")) { ctrl = up.get(); storage = s; break; }
    }
    check(ctrl != nullptr, "a storage-bearing controller exists");
    if (!ctrl) return 1;
    const std::size_t before = storage->children.size();

    // 1. synthetic add: a new disk row.
    auto row = Node::make(NodeKind::Object, "");
    row->insertChild(Node::make(NodeKind::Number, "channel", "0"));
    row->insertChild(Node::make(NodeKind::Number, "unit", "9"));
    row->insertChild(Node::make(NodeKind::Number, "lun", "0"));
    row->insertChild(Node::make(NodeKind::String, "type", "scsi_disk"));
    row->insertChild(Node::make(NodeKind::String, "model", "EMULATR VIRTUAL DISK"));
    row->insertChild(Node::make(NodeKind::String, "media", "Alpha/dka_t9.img"));
    row->insertChild(Node::make(NodeKind::String, "media_kind", "image"));
    storage->insertChild(std::move(row));

    // 2. duplicate the first row.
    auto dup = cloneNode(*storage->children.front());
    storage->insertChild(std::move(dup), 1);

    // 3. delete the (now) second-to-last original row: index before-1+? keep
    //    simple -- remove index 2 (an original row after the dup insert).
    check(storage->removeChildAt(2), "removeChildAt succeeds");

    const std::size_t after = storage->children.size();
    check(after == before + 1, "net count: +add +dup -del == +1");

    std::string out = doc.emit();

    // 4. untouched sections keep verbatim bytes.
    const Node* iic = doc.root()->member("iic_devices");
    if (iic) {
        std::string iicSrc = src.substr(iic->begin, iic->end - iic->begin);
        check(out.find(iicSrc) != std::string::npos,
              "untouched iic_devices bytes appear verbatim in output");
    }

    ParseError err2;
    Document doc2 = Document::parse(out, err2);
    check(err2.ok && doc2.valid(), "edited output reparses");
    if (doc2.valid()) {
        const Node* p2 = doc2.root()->member("pci_devices");
        const Node* s2 = nullptr;
        for (const auto& up : p2->children)
            if ((s2 = up->member("storage")) != nullptr && up->member("name") &&
                up->member("name")->text == ctrl->member("name")->text) break;
        check(s2 && s2->children.size() == after, "reparsed storage count matches");
        bool sawNew = false;
        if (s2)
            for (const auto& r : s2->children)
                if (r->member("media") && r->member("media")->text == "Alpha/dka_t9.img")
                    sawNew = true;
        check(sawNew, "synthetic row round-trips (media Alpha/dka_t9.img)");
    }

    std::printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
