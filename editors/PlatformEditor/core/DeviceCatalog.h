// ============================================================================
// PlatformEditor/core/DeviceCatalog.h -- optional authoring aid (Section 7)
// ============================================================================
// Spec: PLATFORM_EDITOR_SPEC.md Section 7 (rewritten by T-00). The catalog is
// NOT a source of truth -- the emulator reads identity from the manifest, not
// from here. It has two jobs:
//   1. Autofill  -- prefill editable identity fields for a known `model'.
//   2. Drift warning (V-07) -- flag an authored value that disagrees with the
//      catalog default for its model.
//
// Two rules from Section 7, both enforced here:
//   - Lookup keys on MODEL -> identity, never identity -> model. The reverse is
//     not a function: cypress_isa and cypress_ide deliberately share
//     0x1080:0xc693 (one multifunction part, func0 vs func1).
//   - Therefore NO duplicate vendor/device guard (it would false-positive on
//     every multifunction chip). Duplicate MODEL names, however, are an error --
//     model is the lookup key.
// ============================================================================

#ifndef PLATEDIT_DEVICECATALOG_H
#define PLATEDIT_DEVICECATALOG_H

#include <string>
#include <vector>

namespace platedit {

struct Node;   // OrderedJson.h

// A BAR default carried by a catalogued model (empty for fixed-port devices).
struct CatalogBar {
    int         index    = 0;
    std::string kind;             // "io" | "mem"
    std::string size;             // hex aperture, e.g. "0x80"
    bool        prefetch = false;
};

// One catalogued silicon model. Identity strings are DEFAULTS, not truth; "" =
// the catalog offers no default for that field.
struct CatalogModel {
    std::string              model;
    std::string              vendor;        // "0x1080" or ""
    std::string              device;        // "0xc693" or ""
    std::string              classCode;     // "0x060100" or ""
    std::string              interruptPin;  // "0" or ""
    std::vector<CatalogBar>  bars;
    std::vector<std::string> supports;      // e.g. ["storage"]
    std::string              comment;       // field-level help
};

// A single drift finding for V-07.
struct FieldConflict {
    std::string field;      // manifest key: "vendor" | "device" | "class_code" | "interrupt_pin"
    std::string authored;   // value in the manifest
    std::string catalog;    // catalog default it disagrees with
};

class DeviceCatalog {
public:
    // Load from a catalog-file JSON string. On error returns an empty catalog and
    // sets `err' (e.g. duplicate model name). Never throws.
    static DeviceCatalog load(const std::string& json, std::string& err);

    int  version() const { return version_; }
    bool empty()   const { return models_.empty(); }

    // Catalogued model by name, or nullptr (unknown OR a backing model).
    const CatalogModel* find(const std::string& model) const;

    // Is `model' a backing keyword (generic/passive)? These carry no identity
    // defaults, so they get no autofill and never raise V-07.
    bool isBacking(const std::string& model) const;

    const std::vector<std::string>& platforms() const { return platforms_; }
    const std::vector<CatalogModel>& models()   const { return models_; }

    // The catalog default for a scalar identity field of `model', or "".
    std::string defaultFor(const std::string& model, const std::string& field) const;

    // V-07 conflict detector: compare a manifest PCI-device node's authored
    // identity fields against the catalog default for its `model'. Returns [] for
    // a backing or unknown model (unknown is V-08, not V-07). Hex/decimal values
    // compare NUMERICALLY, so "0xC693" vs "0xc693" is not a conflict.
    std::vector<FieldConflict> conflictsForDevice(const Node* pciDevice) const;

private:
    int                      version_ = 0;
    std::vector<CatalogModel> models_;      // real silicon (pci_models)
    std::vector<std::string> backing_;      // ["generic","passive"]
    std::vector<std::string> platforms_;
};

} // namespace platedit

#endif // PLATEDIT_DEVICECATALOG_H
