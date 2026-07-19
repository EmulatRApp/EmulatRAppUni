// ============================================================================
// PlatformEditor/tests/catalog_test.cpp -- T-03 acceptance gate
// ============================================================================
// DeviceCatalog loader + conflict detector (Section 7). Central invariants:
//   - lookup keys on model -> identity;
//   - shared identity across models (cypress_isa/cypress_ide) is NOT an error;
//   - duplicate MODEL name IS an error;
//   - V-07 drift detection compares hex NUMERICALLY (0xC693 == 0xc693);
//   - backing (generic/passive) and unknown models raise no V-07.
//
// Usage:  catalog_test <device_catalog.json> [<ds20_platform.json>]
// ============================================================================

#include "../core/DeviceCatalog.h"
#include "../core/OrderedJson.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace platedit;

namespace {

int failures = 0;
void check(bool cond, const std::string& what)
{
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++failures;
}
bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

// Number of conflicts reported for an inline PCI-device object.
std::size_t conflictCount(const DeviceCatalog& cat, const std::string& deviceJson)
{
    ParseError e;
    Document d = Document::parse(deviceJson, e);
    if (!e.ok) { std::printf("  [FAIL] inline device JSON did not parse: %s\n", e.message.c_str()); ++failures; return 999; }
    return cat.conflictsForDevice(d.root()).size();
}

void loaderChecks(const std::string& path)
{
    std::printf("== T-03 DeviceCatalog loader ==\n");
    std::string json;
    if (!readFile(path, json)) { std::printf("  [FAIL] cannot read %s\n", path.c_str()); ++failures; return; }

    std::string err;
    DeviceCatalog cat = DeviceCatalog::load(json, err);
    check(err.empty(), "catalog loads (" + (err.empty() ? "ok" : err) + ")");
    check(cat.version() == 1, "catalog_version == 1");

    const CatalogModel* isa = cat.find("cypress_isa");
    const CatalogModel* ide = cat.find("cypress_ide");
    check(isa && isa->vendor == "0x1080" && isa->device == "0xc693" && isa->classCode == "0x060100",
          "cypress_isa identity + class_code 0x060100");
    check(ide && ide->classCode == "0x010100",
          "cypress_ide differs only by class_code 0x010100 (shared vendor/device)");
    check(isa && ide && isa->vendor == ide->vendor && isa->device == ide->device,
          "shared identity across two models loaded WITHOUT error (no dup-vendor guard)");

    check(cat.isBacking("generic") && cat.isBacking("passive"), "generic/passive are backing models");
    check(!cat.isBacking("cypress_ide"), "cypress_ide is not a backing model");
    check(cat.find("generic") == nullptr, "backing model has no catalogued identity (find -> null)");
    check(cat.find("no_such_model") == nullptr, "unknown model -> null");
}

void duplicateModelIsError()
{
    std::printf("\n== T-03 duplicate MODEL name is an error ==\n");
    const std::string dup =
        "{ \"catalog_version\": 1, \"pci_models\": ["
        "  { \"model\": \"cypress_isa\", \"vendor\": \"0x1080\", \"device\": \"0xc693\" },"
        "  { \"model\": \"cypress_isa\", \"vendor\": \"0x1080\", \"device\": \"0xc693\" }"
        "] }";
    std::string err;
    DeviceCatalog::load(dup, err);
    check(!err.empty(), "duplicate model name rejected (" + err + ")");
}

void conflictChecks(const std::string& catalogPath)
{
    std::printf("\n== T-03 V-07 conflict detector ==\n");
    std::string json; readFile(catalogPath, json);
    std::string err; DeviceCatalog cat = DeviceCatalog::load(json, err);

    // Matches catalog exactly -> no conflict.
    check(conflictCount(cat,
        "{ \"model\": \"cypress_ide\", \"vendor\": \"0x1080\", \"device\": \"0xc693\", \"class_code\": \"0x010100\", \"interrupt_pin\": 0 }") == 0,
        "authored == catalog -> 0 conflicts");

    // Drifted class_code -> exactly one conflict.
    check(conflictCount(cat,
        "{ \"model\": \"cypress_ide\", \"vendor\": \"0x1080\", \"device\": \"0xc693\", \"class_code\": \"0x999999\", \"interrupt_pin\": 0 }") == 1,
        "drifted class_code -> 1 conflict (V-07)");

    // Hex case difference is NOT drift (numeric compare).
    check(conflictCount(cat,
        "{ \"model\": \"cypress_isa\", \"vendor\": \"0x1080\", \"device\": \"0xC693\", \"class_code\": \"0x060100\", \"interrupt_pin\": 0 }") == 0,
        "0xC693 vs 0xc693 is not a conflict (numeric compare)");

    // Backing model -> no defaults -> no V-07 even with a wild vendor.
    check(conflictCount(cat,
        "{ \"model\": \"generic\", \"vendor\": \"0xDEAD\", \"device\": \"0xBEEF\", \"class_code\": \"0x020000\" }") == 0,
        "backing model 'generic' raises no V-07");

    // Unknown model -> V-08 elsewhere, not V-07 here.
    check(conflictCount(cat,
        "{ \"model\": \"mystery_nic\", \"vendor\": \"0x8086\", \"device\": \"0x1229\" }") == 0,
        "unknown model raises no V-07 (that is V-08)");
}

// Optional: the real ds20 manifest should be conflict-free against the catalog.
void realManifestChecks(const std::string& ds20Path, const std::string& catalogPath)
{
    std::printf("\n== T-03 real ds20 manifest vs catalog ==\n");
    std::string cjson; readFile(catalogPath, cjson);
    std::string err; DeviceCatalog cat = DeviceCatalog::load(cjson, err);

    std::string mjson;
    if (!readFile(ds20Path, mjson)) { std::printf("  (skipped: no ds20 manifest)\n"); return; }
    ParseError pe; Document doc = Document::parse(mjson, pe);
    if (!pe.ok) { std::printf("  [FAIL] ds20 parse\n"); ++failures; return; }

    const Node* pci = doc.root()->member("pci_devices");
    check(pci != nullptr, "ds20 has pci_devices");
    if (!pci) return;
    std::size_t total = 0;
    for (const auto& dev : pci->children) total += cat.conflictsForDevice(dev.get()).size();
    check(total == 0, "as-authored ds20 has 0 catalog conflicts (cypress_isa/ide match, de500 is generic)");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <device_catalog.json> [ds20_platform.json]\n", argv[0]); return 2; }

    loaderChecks(argv[1]);
    duplicateModelIsError();
    conflictChecks(argv[1]);
    if (argc >= 3) realManifestChecks(argv[2], argv[1]);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
