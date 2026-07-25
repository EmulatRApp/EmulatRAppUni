// ============================================================================
// PlatformEditor/tests/schema_test.cpp -- T-02 acceptance gate
// ============================================================================
// Exercises PathMatcher + SchemaPolicy against the real platform_schema.json,
// with the size-collision case called out explicitly (Section 6.2 / D-002):
// the same leaf name `size' resolves to three different tiers by path.
//
// Usage:  schema_test <platform_schema.json>
// ============================================================================

#include "../core/SchemaPolicy.h"

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

std::string tierOf(const SchemaPolicy& p, const std::string& path)
{
    return tierName(p.tierFor(path));
}

// ---- PathMatcher unit checks (no policy file needed) -----------------------
void pathChecks()
{
    std::printf("== T-02 PathMatcher ==\n");
    bool ok = false;
    Path pat = Path::parse("$.pci_devices[*].bars[*].size", ok);
    check(ok, "pattern parses");

    Path c1 = Path::parse("$.pci_devices[2].bars[0].size", ok);
    check(ok && pat.matches(c1), "wildcard pattern matches concrete indices");

    Path c2 = Path::parse("$.pci_devices[2].bars[0].kind", ok);
    check(ok && !pat.matches(c2), "different leaf does not match");

    Path c3 = Path::parse("$.iic_devices[0].size", ok);
    check(ok && !pat.matches(c3), "different container does not match (size collision safety)");

    bool bad = true;
    Path::parse("pci_devices[*]", bad);              // missing root $
    check(!bad, "malformed path (no root) rejected");

    Path fixed = Path::parse("$.pci_devices[2].slot", ok);
    Path wild  = Path::parse("$.pci_devices[*].slot", ok);
    check(fixed.literalCount() > wild.literalCount(),
          "fixed index is more specific than wildcard (longest-literal-prefix)");
}

// ---- longest-literal-prefix conflict resolution ----------------------------
void conflictChecks()
{
    std::printf("\n== T-02 conflict resolution (longest-literal-prefix) ==\n");
    const std::string json =
        "{ \"policy_version\": 1, \"rules\": ["
        "  { \"path\": \"$.pci_devices[*].slot\", \"tier\": \"int\" },"
        "  { \"path\": \"$.pci_devices[2].slot\", \"tier\": \"hex\" }"
        "] }";
    std::string err;
    SchemaPolicy p = SchemaPolicy::load(json, err);
    check(err.empty(), "inline policy loads (" + (err.empty() ? "ok" : err) + ")");
    check(tierOf(p, "$.pci_devices[2].slot") == "hex",
          "specific [2] rule wins over [*] for index 2");
    check(tierOf(p, "$.pci_devices[1].slot") == "int",
          "wildcard rule applies to index 1");
}

// ---- real policy file, incl. the size collision ----------------------------
void policyChecks(const std::string& path)
{
    std::printf("\n== T-02 platform_schema.json ==\n");
    std::string json;
    if (!readFile(path, json)) { std::printf("  [FAIL] cannot read %s\n", path.c_str()); ++failures; return; }

    std::string err;
    SchemaPolicy p = SchemaPolicy::load(json, err);
    check(err.empty(), "policy loads (" + (err.empty() ? "ok" : err) + ")");
    check(p.policyVersion() == 1, "policy_version == 1");

    // The size collision (D-002): one leaf, three tiers, resolved by path.
    check(tierOf(p, "$.iic_devices[0].size")             == "int",  "iic_devices[*].size -> int");
    check(tierOf(p, "$.pci_devices[2].bars[0].size")     == "hex",  "bars[*].size -> hex");
    check(tierOf(p, "$.pci_devices[1].storage[0].size")  == "size", "storage[*].size -> size");

    // PCI identity is editable (T-00), not derived.
    check(tierOf(p, "$.pci_devices[0].vendor")     == "hex", "vendor -> hex (editable, not derived)");
    check(tierOf(p, "$.pci_devices[0].class_code") == "hex", "class_code -> hex");
    check(tierOf(p, "$.pci_devices[0].model")      == "openEnum", "model -> openEnum (not a closed catalog key)");
    check(tierOf(p, "$.pci_devices[0].option_rom") == "bool", "option_rom -> bool");
    check(tierOf(p, "$.pci_devices[0].bars[0].kind") == "enum", "bars[*].kind -> enum");

    // Enum membership and bounds carried through.
    const Rule* cls = p.resolve("$.iic_devices[3].class");
    bool hasLed = false;
    if (cls) for (const auto& v : cls->values) if (v == "led") hasLed = true;
    check(cls && cls->tier == Tier::Enum && hasLed, "iic class enum includes 'led' (T-00 addition)");

    const Rule* mk = p.resolve("$.pci_devices[1].storage[0].media_kind");
    bool hasHost = false;
    if (mk) for (const auto& v : mk->values) if (v == "host") hasHost = true;
    check(mk && hasHost, "media_kind enum includes 'host' (T-00 addition)");

    const Rule* slot = p.resolve("$.pci_devices[0].slot");
    check(slot && slot->min && *slot->min == 0 && slot->max && *slot->max == 31,
          "slot int bounds 0..31 loaded");

    const Rule* stsize = p.resolve("$.pci_devices[1].storage[0].size");
    check(stsize && stsize->min && *stsize->min == 512, "storage size min 512 loaded");

    const Rule* mver = p.resolve("$.manifest_version");
    check(mver && mver->readOnly, "manifest_version is readOnly");

    const Rule* ven = p.resolve("$.pci_devices[0].vendor");
    check(ven && !ven->catalogDefault.empty(), "vendor carries a catalogDefault autofill source");

    // Passthrough (P-2): unknown key is not matched.
    check(p.resolve("$.pci_devices[0].totally_unknown") == nullptr,
          "unknown key -> passthrough (P-2)");
    check(tierOf(p, "$.pci_devices[0].totally_unknown") == "passthrough",
          "unknown key tier is passthrough");

    // Containers / labelFormat.
    const Container* pciC = p.containerFor("$.pci_devices[2]");
    check(pciC && pciC->labelFormat.find("{name}") != std::string::npos,
          "pci_devices[*] container labelFormat resolves");
    const Container* iicC = p.containerFor("$.iic_devices[7]");
    check(iicC && iicC->labelFormat.find("{address}") != std::string::npos,
          "iic_devices[*] container labelFormat resolves");
}

} // namespace

int main(int argc, char** argv)
{
    pathChecks();
    conflictChecks();
    if (argc >= 2) policyChecks(argv[1]);
    else { std::printf("\n(skipped platform_schema.json checks: no path given)\n"); }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
