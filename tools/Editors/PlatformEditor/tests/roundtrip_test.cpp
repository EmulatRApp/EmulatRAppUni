// ============================================================================
// PlatformEditor/tests/roundtrip_test.cpp -- T-01 acceptance gate
// ============================================================================
// Two checks, per PLATFORM_EDITOR_SPEC.md Sections 5.3 and 10:
//
//   1. ROUND-TRIP: parse each manifest, emit with zero edits, require the output
//      to be byte-identical to the input.  (Format-preserving writer, so this is
//      stronger than "diff-clean modulo indentation normalization".)
//
//   2. ONE-LINE EDIT LITMUS: in ds20, change de500_tulip's `slot' (7 -> 9) via
//      the DOM, emit, and require the diff to be exactly one changed line, that
//      the result still parses, and that the new value reads back as 9.
//
// Usage:  roundtrip_test <manifest.json> [<manifest.json> ...]
// Exit:   0 = all pass, 1 = a failure, 2 = usage/IO error.
// ============================================================================

#include "../core/OrderedJson.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace platedit;

namespace {

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string baseName(const std::string& path)
{
    auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Return the 1-based line numbers that differ between a and b (by content).
std::vector<int> diffLines(const std::string& a, const std::string& b)
{
    std::vector<std::string> la, lb;
    std::stringstream sa(a), sb(b);
    std::string line;
    while (std::getline(sa, line)) la.push_back(line);
    while (std::getline(sb, line)) lb.push_back(line);
    std::vector<int> diffs;
    std::size_t n = std::max(la.size(), lb.size());
    for (std::size_t i = 0; i < n; ++i) {
        const std::string* x = i < la.size() ? &la[i] : nullptr;
        const std::string* y = i < lb.size() ? &lb[i] : nullptr;
        if (!x || !y || *x != *y) diffs.push_back(static_cast<int>(i + 1));
    }
    return diffs;
}

int failures = 0;
void check(bool cond, const std::string& what)
{
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++failures;
}

// ---- Check 1: byte-identical round-trip ------------------------------------
void roundTrip(const std::string& path)
{
    std::string src;
    if (!readFile(path, src)) { std::printf("  [FAIL] cannot read %s\n", path.c_str()); ++failures; return; }

    ParseError err;
    Document doc = Document::parse(src, err);
    check(err.ok, baseName(path) + ": parses (" + (err.ok ? "ok" : err.message + " @ " + std::to_string(err.offset)) + ")");
    if (!err.ok) return;

    std::string out = doc.emit();
    bool identical = (out == src);
    check(identical, baseName(path) + ": zero-edit emit is byte-identical");
    if (!identical) {
        auto d = diffLines(src, out);
        std::printf("        (%zu byte in / %zu out; first differing line %s)\n",
                    src.size(), out.size(),
                    d.empty() ? "none" : std::to_string(d.front()).c_str());
    }
}

// ---- Check 2: one-line edit litmus (ds20 de500_tulip slot 7 -> 9) ----------
void oneLineEdit(const std::string& path)
{
    std::string src;
    if (!readFile(path, src)) return;
    ParseError err;
    Document doc = Document::parse(src, err);
    if (!err.ok) return;

    // Navigate $.pci_devices[2].slot  (de500_tulip is the 3rd PCI device).
    Node* root = doc.root();
    Node* pci  = root ? root->member("pci_devices") : nullptr;
    Node* dev  = pci  ? pci->elem(2)                : nullptr;
    Node* slot = dev  ? dev->member("slot")         : nullptr;

    check(slot != nullptr, "ds20: found $.pci_devices[2].slot");
    if (!slot) return;
    check(slot->path() == "$.pci_devices[2].slot", "ds20: canonical path is " + slot->path());

    std::string before = src.substr(slot->begin, slot->end - slot->begin);
    check(before == "7", "ds20: de500_tulip slot reads '7' pre-edit (got '" + before + "')");

    slot->dirty  = true;
    slot->edited = "9";
    std::string out = doc.emit();

    auto d = diffLines(src, out);
    check(d.size() == 1, "ds20: single scalar edit -> exactly one changed line (got " + std::to_string(d.size()) + ")");

    ParseError err2;
    Document doc2 = Document::parse(out, err2);
    check(err2.ok, "ds20: edited output still parses");
    Node* slot2 = err2.ok ? doc2.root()->member("pci_devices")->elem(2)->member("slot") : nullptr;
    std::string after = slot2 ? out.substr(slot2->begin, slot2->end - slot2->begin) : "";
    check(after == "9", "ds20: edited slot reads back '9' (got '" + after + "')");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <manifest.json> [more...]\n", argv[0]);
        return 2;
    }

    std::printf("== T-01 round-trip (byte-identical, zero edits) ==\n");
    std::string ds20;
    for (int i = 1; i < argc; ++i) {
        roundTrip(argv[i]);
        if (baseName(argv[i]).rfind("ds20", 0) == 0) ds20 = argv[i];
    }

    if (!ds20.empty()) {
        std::printf("\n== T-01 one-line-edit litmus (ds20 de500_tulip slot 7->9) ==\n");
        oneLineEdit(ds20);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
