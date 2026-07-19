// ============================================================================
// PlatformEditor/core/SchemaPolicy.h -- data-driven field policy (P-1)
// ============================================================================
// Spec: PLATFORM_EDITOR_SPEC.md Sections 6.3-6.5. Widget selection, enumerations
// and read/write policy live in an external policy file (platform_schema.json),
// not in C++ -- adding a manifest field means editing JSON, not recompiling.
//
// Policy is keyed by PATH (Path.h). resolve() returns the most specific matching
// rule, or nullptr for a path the policy does not mention (P-2 passthrough: the
// field is still editable, just with a type-inferred generic widget).
//
// The `derived' tier is intentionally absent -- retired by the T-00 evidence
// (Section 6.1): PCI identity/BARs are editable inputs, not catalog-owned echoes.
// ============================================================================

#ifndef PLATEDIT_SCHEMAPOLICY_H
#define PLATEDIT_SCHEMAPOLICY_H

#include "Path.h"

#include <optional>
#include <string>
#include <vector>

namespace platedit {

enum class Tier {
    Enum,        // closed set              -> locked combo
    OpenEnum,    // known set + free entry   -> editable combo
    Free,        // free text                -> line edit
    Multiline,   // prose                    -> plain text edit
    Int,         // bounded integer          -> spin box
    Hex,         // validated hex scalar     -> line edit + validator
    Bool,        // boolean flag             -> check box
    Size,        // byte capacity + K/M/G/T  -> line edit + validator
    Path,        // filesystem reference     -> line edit + browse
    Passthrough  // unknown to policy (P-2)  -> type-inferred, editable
};

const char* tierName(Tier t);

struct Rule {
    platedit::Path           pattern;
    Tier                     tier   = Tier::Passthrough;
    std::vector<std::string> values;          // enum / openEnum members
    std::optional<long long> min;             // int / size lower bound
    std::optional<long long> max;             // int upper bound
    std::string              catalogDefault;  // optional "catalog:..." autofill source
    bool                     readOnly = false;// e.g. manifest_version (display only)
};

struct Container {
    platedit::Path pattern;
    std::string    labelFormat;               // e.g. "{name}  {address}  {class}"
};

class SchemaPolicy {
public:
    // Load from a policy-file JSON string. On error returns an empty policy and
    // sets `err'. Never throws.
    static SchemaPolicy load(const std::string& json, std::string& err);

    int policyVersion() const { return version_; }
    bool empty() const { return rules_.empty() && containers_.empty(); }

    // Most specific rule matching `concretePath', or nullptr (P-2 passthrough).
    const Rule* resolve(const std::string& concretePath) const;

    // Convenience: tier for a path (Passthrough when unmatched).
    Tier tierFor(const std::string& concretePath) const;

    // Container descriptor whose pattern matches `concretePath', or nullptr.
    const Container* containerFor(const std::string& concretePath) const;

    const std::vector<Rule>&      rules()      const { return rules_; }
    const std::vector<Container>& containers() const { return containers_; }

private:
    int                    version_ = 0;
    std::vector<Rule>      rules_;
    std::vector<Container> containers_;
};

} // namespace platedit

#endif // PLATEDIT_SCHEMAPOLICY_H
