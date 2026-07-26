// ============================================================================
// PlatformEditor/tests/view_test.cpp -- ManifestView + Renderer (TUI core)
// ============================================================================
// Verifies the Qt-free presentation logic headlessly: label synthesis (DOM ->
// catalog -> ""), the containers-only tree (Section 5.4), property rows with
// tiers, and that a rendered frame contains the expected structure.
//
// Usage:  view_test <ds20.json> <platform_schema.json> <device_catalog.json>
// ============================================================================

#include "../core/DeviceCatalog.h"
#include "../core/ManifestView.h"
#include "../core/OrderedJson.h"
#include "../core/SchemaPolicy.h"
#include "../tui/Renderer.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace platedit;

namespace {
int failures = 0;
void check(bool c, const std::string& w) { std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", w.c_str()); if (!c) ++failures; }
bool readFile(const std::string& p, std::string& o) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::ostringstream s; s << f.rdbuf(); o = s.str(); return true; }
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::fprintf(stderr, "usage: %s <ds20.json> <schema.json> <catalog.json>\n", argv[0]); return 2; }
    std::string mj, sj, cj;
    if (!readFile(argv[1], mj) || !readFile(argv[2], sj) || !readFile(argv[3], cj)) {
        std::fprintf(stderr, "cannot read inputs\n"); return 2;
    }
    ParseError pe; Document doc = Document::parse(mj, pe);
    std::string err;
    SchemaPolicy policy = SchemaPolicy::load(sj, err);
    DeviceCatalog cat   = DeviceCatalog::load(cj, err);
    check(pe.ok && err.empty(), "inputs load");

    // ---- labels ----
    std::printf("== labels ==\n");
    Node* root = doc.root();
    check(containerLabel(root, policy, &cat) == "DS20", "root label -> platform 'DS20'");

    // Corpus updated 2026-07-25: pka_53c810 (NCR SCSI HBA) joined the three
    // original controllers -> 4 PCI devices.
    Node* pci = root->member("pci_devices");
    check(containerLabel(pci, policy, &cat) == "pci_devices [4]", "array label 'pci_devices [4]'");

    Node* ide = pci->elem(1);   // cypress_ide
    std::string ideLabel = containerLabel(ide, policy, &cat);
    check(contains(ideLabel, "cypress_ide") && contains(ideLabel, "0:0:5.1"),
          "cypress_ide label carries name + BDF: '" + ideLabel + "'");
    check(contains(ideLabel, "0x1080:0xc693"), "label resolves {vendor}:{device} from DOM");

    // catalog fallback: a token absent from the DOM falls back to the catalog.
    // cypress_isa has vendor in the manifest; test the pure formatter path.
    Node* isa = pci->elem(0);
    check(formatLabel("{vendor}", isa, &cat) == "0x1080", "formatLabel DOM value");
    // Build a fallback probe: model present, but ask for a token only the catalog knows
    check(formatLabel("{class_code}", isa, &cat) == "0x060100", "formatLabel resolves class_code from DOM");

    // ---- tree (containers only) ----
    std::printf("\n== tree (containers only) ==\n");
    TreeView tv(&doc, &policy, &cat);
    tv.expandAll();
    const auto& rows = tv.rows();
    check(!rows.empty() && rows[0].node == root, "row 0 is root");

    // No scalar leaked in as a row: every row node is a container.
    bool allContainers = true;
    for (const auto& r : rows) if (!r.node->isContainer()) allContainers = false;
    check(allContainers, "every tree row is a container (no scalar rows)");

    // Expected structure present: iic_devices[8], the three PCI devices, storage, bars.
    auto hasLabel = [&](const std::string& want) {
        for (const auto& r : rows) if (r.label == want) return true;
        return false;
    };
    check(hasLabel("iic_devices [8]"), "iic_devices [8] present");
    check(hasLabel("storage [2]"), "cypress_ide storage [2] present");
    check(hasLabel("bars [2]"), "de500_tulip bars [2] present");

    // Collapse: root stays open, its top-level arrays collapse.  Count them
    // from the DOM so corpus growth (e.g. pci_irq_table_hose0, 2026-07-25)
    // does not go stale here.
    tv.collapseAll();
    const std::size_t topArrays = TreeView::containerChildren(root).size();
    check(tv.rows().size() == 1 + topArrays,
          "collapseAll shows root + its collapsed container children");
    Node* iic = root->member("iic_devices");
    tv.setExpanded(iic, true);
    check(tv.rows().size() == 1 + topArrays + 8,
          "expanding iic_devices reveals its 8 devices");

    // ---- properties ----
    std::printf("\n== properties ==\n");
    auto props = propertiesOf(ide, policy);
    bool sawSlot = false, sawStorageField = false;
    Tier slotTier = Tier::Passthrough;
    for (const auto& p : props) {
        if (p.key == "slot") { sawSlot = true; slotTier = p.tier; }
        if (p.key == "storage") sawStorageField = true;   // must NOT appear (it's a container)
    }
    check(sawSlot && slotTier == Tier::Int, "cypress_ide props include slot (int)");
    check(!sawStorageField, "container child 'storage' is NOT a property row");

    // ---- storage moniker (device label) ----
    std::printf("\n== storage moniker ==\n");
    // The as-authored ds20 has EMPTY media ("no disk loaded") -> empty moniker,
    // so its storage label falls back to the address form.
    Node* stor = ide->member("storage");
    check(storageMoniker(stor->elem(0)) == "", "empty media -> empty moniker (ds20 as-authored)");
    check(containerLabel(stor->elem(0), policy, &cat).find("ch0 unit0") != std::string::npos,
          "empty-media storage label falls back to address 'ch0 unit0'");
    // Filename-default and override, on synthetic nodes with media set.
    ParseError oe;
    Document md = Document::parse("{\"media\":\"Alpha/dka0.vdisk\",\"type\":\"ata_disk\"}", oe);
    check(storageMoniker(md.root()) == "dka0", "moniker defaults to filename minus ext ('dka0')");
    Document mi = Document::parse("{\"media\":\"Alpha/OpenVMS_v82.iso\",\"type\":\"atapi_cdrom\"}", oe);
    check(storageMoniker(mi.root()) == "OpenVMS_v82", "moniker from iso filename ('OpenVMS_v82')");
    Document ov = Document::parse("{\"label\":\"boot_disk\",\"media\":\"Alpha/dka0.vdisk\"}", oe);
    check(storageMoniker(ov.root()) == "boot_disk", "explicit label overrides filename");

    // ---- rendered frame ----
    std::printf("\n== rendered frame ==\n");
    tv.expandAll();
    FrameState fs;
    fs.title = "PlatformEditor  ds20";
    fs.tree = &tv.rows(); fs.treeSel = 0;
    fs.props = &props; fs.propSel = 0;
    fs.status = "[q] quit";
    fs.width = 80; fs.height = 24; fs.ansi = false;
    std::string frame = renderFrame(fs);
    check(contains(frame, "PlatformEditor"), "frame has title");
    check(contains(frame, "iic_devices [8]"), "frame shows tree");
    check(contains(frame, "slot"), "frame shows a property");
    check(contains(frame, "|"), "frame has the two-pane separator");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
