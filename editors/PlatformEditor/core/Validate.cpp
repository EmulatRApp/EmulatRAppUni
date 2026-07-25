// ============================================================================
// PlatformEditor/core/Validate.cpp -- manifest validation (see Validate.h)
// ============================================================================

#include "Validate.h"

#include "DeviceCatalog.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace platedit {

const char* severityName(Severity s) {
    switch (s) {
        case Severity::Info: return "info";
        case Severity::Warn: return "warn";
        case Severity::Err:  return "err";
    }
    return "?";
}

namespace {

bool isHexLiteral(const std::string& s) {
    if (s.size() < 3 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return false;
    for (std::size_t i = 2; i < s.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

long long numValue(const std::string& s, long long fallback = -1) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 0);   // accepts 0x.. and decimal
    return (end && *end == '\0') ? v : fallback;
}

std::string memberText(const Node* n, const std::string& key) {
    const Node* m = n ? n->member(key) : nullptr;
    return m ? m->text : std::string();
}

bool startsWith(const std::string& s, const char* p) {
    return s.rfind(p, 0) == 0;
}

struct Collector {
    std::vector<Finding> out;
    void add(Severity sev, const char* id, const Node* node, std::string msg) {
        out.push_back(Finding{sev, id, std::move(msg), node});
    }
};

void checkIic(const Node* iicArr, Collector& c) {
    if (!iicArr || iicArr->kind != NodeKind::Array) return;
    std::map<long long, const Node*> byAddr;
    std::set<std::string> names;
    for (const auto& up : iicArr->children) {
        const Node* n = up.get();
        const std::string name = memberText(n, "name");
        const std::string addr = memberText(n, "address");
        if (!isHexLiteral(addr)) {
            c.add(Severity::Warn, "V-05", n,
                  name + ": address '" + addr + "' is not valid hex");
        } else {
            long long v = numValue(addr);
            if (v & 1)
                c.add(Severity::Err, "V-06", n,
                      name + ": address " + addr +
                      " is odd (emulator rejects manifest)");
            if (byAddr.count(v))
                c.add(Severity::Err, "V-02", n, "duplicate IIC address " + addr);
            byAddr[v] = n;
        }
        if (!name.empty() && !names.insert(name).second)
            c.add(Severity::Warn, "V-03", n,
                  "duplicate name '" + name + "' in iic_devices");
        if (memberText(n, "comment").find("_PROVISIONAL") != std::string::npos)
            c.add(Severity::Info, "V-11", n,
                  name + ": comment contains _PROVISIONAL");
    }
}

void checkStorage(const Node* pci, const std::string& pciName, Collector& c) {
    const Node* chArr = pci->member("channels");
    std::map<long long, const Node*> chByIndex;
    if (chArr && chArr->kind == NodeKind::Array) {
        for (const auto& up : chArr->children) {
            const Node* ch = up.get();
            long long idx = numValue(memberText(ch, "index"), 0);
            if (chByIndex.count(idx))
                c.add(Severity::Err, "V-28", ch,
                      "duplicate channel index " + std::to_string(idx));
            chByIndex[idx] = ch;
        }
    }

    const Node* stArr = pci->member("storage");
    if (!stArr || stArr->kind != NodeKind::Array) return;

    bool notedNoSidecar = false;
    std::set<std::string> cu;
    for (const auto& up : stArr->children) {
        const Node* s = up.get();
        const std::string type = memberText(s, "type");
        long long channel = numValue(memberText(s, "channel"), 0);
        long long unit    = numValue(memberText(s, "unit"), 0);
        std::string ck = std::to_string(channel) + "/" + std::to_string(unit);
        if (!cu.insert(ck).second)
            c.add(Severity::Err, "V-04", s,
                  pciName + ": duplicate (channel " + std::to_string(channel) +
                  ", unit " + std::to_string(unit) + ")");
        if (!startsWith(type, "scsi")) continue;

        // SCSI channel facts: from the channels[] sidecar when present,
        // else the SPEC-SCSIH-001 Sec 4.2 default (single ch0, initiator 7).
        long long initiator = 7;
        std::string width   = "wide";
        auto it = chByIndex.find(channel);
        if (it != chByIndex.end()) {
            initiator = numValue(memberText(it->second, "initiator_id"), 7);
            width     = memberText(it->second, "width");
        } else if (chArr && chArr->kind == NodeKind::Array) {
            c.add(Severity::Err, "V-26", s,
                  "storage channel " + std::to_string(channel) +
                  " has no channels[] entry (dangling FK)");
            continue;
        } else if (!notedNoSidecar) {
            notedNoSidecar = true;
            c.add(Severity::Info, "V-26", s,
                  pciName + ": no channels[] sidecar; assuming single channel"
                  " 0, initiator 7 (SPEC-SCSIH-001 Sec 4.2)");
        }
        if (unit == initiator)
            c.add(Severity::Err, "V-25", s,
                  "target " + std::to_string(unit) +
                  " == initiator_id (the HBA's own id)");
        if (width == "narrow" && unit > 7)
            c.add(Severity::Err, "V-27", s,
                  "narrow bus: target " + std::to_string(unit) + " > 7");
    }
}

void checkPci(const Node* pciArr, const DeviceCatalog* catalog, Collector& c) {
    if (!pciArr || pciArr->kind != NodeKind::Array) return;
    std::set<std::string> bdf, names;
    for (const auto& up : pciArr->children) {
        const Node* p = up.get();
        const std::string name = memberText(p, "name");
        std::string k = memberText(p, "hose") + "/" + memberText(p, "bus") +
                        "/" + memberText(p, "slot") + "/" + memberText(p, "func");
        if (!bdf.insert(k).second)
            c.add(Severity::Err, "V-01", p, name + ": duplicate BDF " + k);
        if (!name.empty() && !names.insert(name).second)
            c.add(Severity::Warn, "V-03", p,
                  "duplicate name '" + name + "' in pci_devices");

        const std::string vendor = memberText(p, "vendor");
        long long vv = isHexLiteral(vendor) ? numValue(vendor) : -1;
        if (vv <= 0 || vv >= 0xffff)
            c.add(Severity::Err, "V-15", p,
                  name + ": invalid vendor " + vendor);

        if (memberText(p, "comment").find("_PROVISIONAL") != std::string::npos)
            c.add(Severity::Info, "V-11", p,
                  name + ": comment contains _PROVISIONAL");

        if (catalog) {
            const std::string model = memberText(p, "model");
            if (!model.empty() && !catalog->isBacking(model)) {
                if (!catalog->find(model)) {
                    c.add(Severity::Info, "V-08", p,
                          name + ": model '" + model + "' unknown to catalog");
                } else {
                    for (const FieldConflict& fc : catalog->conflictsForDevice(p))
                        c.add(Severity::Warn, "V-07", p,
                              name + ": " + fc.field + " " + fc.authored +
                              " diverges from catalog " + fc.catalog +
                              " for model '" + model + "'");
                }
            }
        }

        checkStorage(p, name, c);
    }
}

} // namespace

std::vector<Finding> validateManifest(const Document& doc,
                                      const DeviceCatalog* catalog) {
    Collector c;
    if (!doc.valid()) return c.out;
    const Node* root = doc.root();
    checkIic(root->member("iic_devices"), c);
    checkPci(root->member("pci_devices"), catalog, c);
    return c.out;
}

} // namespace platedit
