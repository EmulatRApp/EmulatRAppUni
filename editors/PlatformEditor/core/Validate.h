// ============================================================================
// PlatformEditor/core/Validate.h -- manifest validation (Section 9 V-rules)
// ============================================================================
// Qt-free (pure std).  Implements the live-validation rule set the web mockup
// prototyped in JS (webui/mockup.html) as a core module shared by every
// frontend (TUI, Qt, future web-over-HTTP).  Rules operate on the parsed DOM;
// findings carry the offending Node* so a frontend can navigate on click.
//
// Rule ids follow the mockup / SPEC-PLATED-001 Section 9 numbering:
//   V-01 err  duplicate PCI BDF (hose/bus/slot/func)
//   V-02 err  duplicate IIC address
//   V-03 warn duplicate name within an array
//   V-04 err  duplicate storage (channel,unit) on one controller
//   V-05 warn IIC address not valid hex
//   V-06 err  IIC address odd (emulator rejects the manifest)
//   V-07 warn authored PCI identity diverges from catalog default for model
//   V-08 info model unknown to catalog (informational, not an error)
//   V-11 info comment contains _PROVISIONAL
//   V-15 err  PCI vendor invalid (not hex / 0x0000 / 0xffff)
//   V-25 err  SCSI storage unit == channel initiator_id
//   V-26 err  SCSI storage channel has no channels[] entry (dangling FK);
//        info when the controller has no channels[] sidecar at all (default
//        single-channel assumption, SPEC-SCSIH-001 Sec 4.2)
//   V-27 err  narrow SCSI bus with target id > 7
//   V-28 err  duplicate channels[] index
// ============================================================================

#ifndef PLATEDIT_VALIDATE_H
#define PLATEDIT_VALIDATE_H

#include "OrderedJson.h"

#include <string>
#include <vector>

namespace platedit {

class DeviceCatalog;

enum class Severity { Info, Warn, Err };

const char* severityName(Severity s);

struct Finding {
    Severity    sev  = Severity::Info;
    std::string id;                    // "V-01" ...
    std::string message;
    const Node* node = nullptr;        // offending node (may be null)
};

// Run all rules over a parsed manifest.  `catalog' is optional (null skips
// V-07/V-08).  Never throws.
std::vector<Finding> validateManifest(const Document& doc,
                                      const DeviceCatalog* catalog);

} // namespace platedit

#endif // PLATEDIT_VALIDATE_H
