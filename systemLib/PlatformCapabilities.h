// ============================================================================
// PlatformCapabilities.h -- capability-scoped platform behavior gating
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// PURPOSE:
//   A reviewable primitive for scoping a platform-divergent behavior. The MODEL
//   is the key; the model's <model>_platform.json manifest (+ resolved chipset
//   variant) enumerates CAPABILITIES; and a gate tests the CAPABILITY, not the
//   model:
//
//       if (caps.has(PlatCap::SbAli)) { ...ALi south-bridge quirk... }
//
//   NOT `model == "ES40"` (ES45 shares the ALi), NOT the chipset (ES40=Typhoon
//   vs ES45=Titan differ on chipset but share the south-bridge). The chipset
//   axis cannot express a south-bridge divergence; the capability can. See
//   journals/20260705_platform_axis_classification.md for the four-axis model.
//
// WHAT THIS DOES AND DOES NOT DO:
//   Makes SCOPE explicit and reviewable. It does NOT make an individual
//   exception correct -- a gate scoped to the wrong capability is still wrong,
//   just wrong within its stated blast radius. Correctness is established
//   per-platform by boot-to->>> evidence BEFORE a gate is widened. Two separate
//   jobs; keep them separate in review.
//
// FAIL-INERT (both edges):
//   - An unset capability gates nothing (empty bits match nothing).
//   - Capabilities are ENUMERATED POSITIVE features -- there is no `~0`
//     "everything". A platform added later that does not assert a capability
//     does NOT inherit any gate scoped to it. Do-no-harm extended forward.
//   - Latched at Machine construction from the manifest, BEFORE any guest
//     instruction retires, so it is live for every gate by definition.
//     `latched()` lets a too-late latch trip an assert instead of silently
//     gating nothing (the "why didn't my gate fire" trap).
//
// ADOPTION (per journals/20260705_platform_axis_classification.md):
//   Landed INERT -- zero call sites. Most "global fixes under suspicion" are
//   CPU-axis truths (correct on all EV6 platforms); they stay global and use NO
//   gate. This primitive earns its keep at the first honestly capability-
//   divergent fix -- the ALi south-bridge is the strongest near-term candidate.
// ============================================================================

#ifndef SYSTEMLIB_PLATFORM_CAPABILITIES_H
#define SYSTEMLIB_PLATFORM_CAPABILITIES_H

#include <cstdint>
#include <string>

#include "chipsetLib/TsunamiVariant.h"
#include "systemLib/PlatformConfig.h"

namespace systemLib {

// Enumerated positive capabilities. Each names an AXIS of divergence, so a gate
// reads as *why* it is scoped the way it is. Extend as real capability-divergent
// cases appear -- do NOT add a bare per-model bit (a raw model check is the smell
// that the author defaulted to the one platform they were testing).
enum class PlatCap : uint32_t {
    None            = 0,

    // CPU / PALcode axis -- shared by ALL current platforms (all EV6). A fix
    // gated on CpuEv6 is universal; in practice such fixes need NO gate at all
    // (they are just correct). Present so a CPU-axis intent can be stated when a
    // future non-EV6 core makes the axis real.
    CpuEv6          = 1u << 0,

    // Chipset axis (from the resolved ChipsetVariant).
    ChipsetTsunami  = 1u << 1,
    ChipsetTyphoon  = 1u << 2,
    ChipsetTitan    = 1u << 3,

    // South-bridge axis (from the manifest ISA-bridge PCI device model).
    SbCypress       = 1u << 4,   // Cypress CY82C693
    SbAli           = 1u << 5,   // ALi M1543C (ES40/ES45; STAND-IN today)

    // Topology.
    DualPchip       = 1u << 6,   // hose 1 populated (dual-Pchip box)

    // Console-wiring axis (2026-07-08) -- which UART the primary SRM console
    // rides.  Named as its own axis because the DS/ES families cross over on
    // BOTH chipset and south-bridge (DS15/ES45 share them), so neither the
    // Chipset* nor SbAli axis can express "primary console on the second UART
    // (ISA 0x2F8, COM2)" without sweeping in a model that does not use it.
    // Sourced from the DERIVED RUNTIME MODEL (resolve()), not a hardware trait.
    ConsoleUartCom2 = 1u << 7,   // primary console on 0x2F8 (COM2); e.g. ES40 pc264
};

// Derived runtime model identity (2026-07-08) -- the reconciled key from
// Channel A ([System] model) and Channel B (<stem>_platform.json "platform").
// Model-granular so a gate can be scoped to exactly one box even when families
// share a chipset/south-bridge.  Extend as models are brought up; Unknown
// asserts nothing (fail-inert).
enum class ResolvedModel : uint8_t {
    Unknown = 0,
    DS10,
    DS15,
    DS20,
    ES40,
    ES45,
};

class PlatformCapabilities
{
public:
    PlatformCapabilities() noexcept = default;

    // Derive the capability set from the resolved model identity. `model` is the
    // KEY; the chipset variant + device manifest are the VALUE source.
    static PlatformCapabilities derive(std::string const&         model,
                                       ChipsetVariant variant,
                                       DeviceManifest const&      manifest) noexcept
    {
        uint32_t bits = 0;

        // CPU axis: every current EmulatR platform is EV6.
        bits |= static_cast<uint32_t>(PlatCap::CpuEv6);

        // Chipset axis.
        switch (variant) {
        case ChipsetVariant::Tsunami: bits |= u(PlatCap::ChipsetTsunami); break;
        case ChipsetVariant::Typhoon: bits |= u(PlatCap::ChipsetTyphoon); break;
        case ChipsetVariant::Titan:   bits |= u(PlatCap::ChipsetTitan);   break;
        default: break;   // Unknown -> assert nothing (fail-inert)
        }

        // South-bridge axis + topology, from the manifest device inventory.
        for (auto const& d : manifest.pci) {
            // ISA-bridge class == 0x0601xx.
            if ((d.classCode >> 8) == 0x0601u) {
                if (contains(d.modelName, "ali"))     bits |= u(PlatCap::SbAli);
                if (contains(d.modelName, "cypress")) bits |= u(PlatCap::SbCypress);
            }
            if (d.hose > 0) bits |= u(PlatCap::DualPchip);
        }

        // Console-wiring axis (2026-07-08): the DERIVED RUNTIME MODEL -- not a
        // shared hardware trait -- decides which UART the primary console rides.
        // Only models CONFIRMED to bind console to 0x2F8 set the bit; ES45/DS15
        // are excluded until their console wiring is confirmed (see task: why
        // ES40 boots on COM2).  This is the reserved model hook, and is NOT a
        // per-model bit -- ConsoleUartCom2 names the axis, resolve() supplies
        // the model-granular value.
        switch (resolve(model, manifest.platform)) {
        case ResolvedModel::ES40: bits |= u(PlatCap::ConsoleUartCom2); break;
        default: break;   // Unknown/other -- assert nothing (fail-inert)
        }

        PlatformCapabilities c;
        c.m_bits    = bits;
        c.m_latched = true;
        return c;
    }

    // The ONLY accessor -- never let a raw `& model` comparison into a call site.
    // Tests membership with `!= 0` (never `== cap`), so a derived multi-bit mask
    // works correctly.
    bool has(PlatCap cap) const noexcept
    {
        return (m_bits & static_cast<uint32_t>(cap)) != 0;
    }

    // True once derive() has run. A gate that reads capabilities before the latch
    // should assert this -- a too-late latch then trips a check instead of
    // silently gating nothing.
    bool     latched() const noexcept { return m_latched; }
    uint32_t bits()    const noexcept { return m_bits; }

private:
    static constexpr uint32_t u(PlatCap c) noexcept { return static_cast<uint32_t>(c); }
    static bool contains(std::string const& hay, char const* needle) noexcept
    {
        return hay.find(needle) != std::string::npos;
    }

    // Map the reconciled model identity to a ResolvedModel.  manifestPlatform
    // (Channel B, the firmware-badged identity) wins when present; iniModel
    // (Channel A) is the fallback.  Token match, not exact-equal, tolerates
    // decoration like "AlphaServer ES40".  More specific tokens are tested
    // first so a substring probe cannot alias a shorter model name.
    static ResolvedModel resolve(std::string const& iniModel,
                                 std::string const& manifestPlatform) noexcept
    {
        std::string key = manifestPlatform.empty() ? iniModel : manifestPlatform;
        for (char& ch : key) if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
        auto tok = [&](char const* t) noexcept { return key.find(t) != std::string::npos; };
        if (tok("ES45")) return ResolvedModel::ES45;
        if (tok("ES40")) return ResolvedModel::ES40;
        if (tok("DS15")) return ResolvedModel::DS15;
        if (tok("DS20")) return ResolvedModel::DS20;
        if (tok("DS10")) return ResolvedModel::DS10;
        return ResolvedModel::Unknown;
    }

    uint32_t m_bits    = 0;      // 0 = matches nothing (fail-inert)
    bool     m_latched = false;
};

} // namespace systemLib

#endif // SYSTEMLIB_PLATFORM_CAPABILITIES_H
