// ============================================================================
// ev6Translation_struct.h - Translate virtual address for data access (load/store)
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic) / ChatGPT (OpenAI)
//
// Commercial use prohibited without separate license.
// Contact: peert@envysys.com | https://envysys.com
// Documentation: https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================

// ReSharper disable All
#ifndef EV6TRANSLATOR_H
#define EV6TRANSLATOR_H


#include "coreLib/Axp_Attributes_core.h"
#include "coreLib/VA_types.h"
#include "memoryLib/GuestMemory.h"
#include "memoryLib/global_GuestMemory.h"


#include "coreLib/VA_core.h"
#include "pteLib/AlphaPTE_Core.h"
#include "pteLib/alpha_pte_core.h"
#include "coreLib/types_core.h"


#include <QtGlobal>

#include "cpuCoreLib/ReservationManager.h"
#include "exceptionLib/ExceptionFactory.h"
#include "faultLib/FaultDispatcher.h"
#include "faultLib/GlobalFaultDispatcherBank.h"
#include "faultLib/raiseTranslationFault_inl.h"
#include "machineLib/PipeLineSlot_inl.h"
#include "pteLib/global_Ev6TLB_Singleton.h"
#include <QMutexLocker>
#include <QMutex>

#include "Ev6SiliconTypes.h"

struct alignas(64) Ev6Translator
{
    // Injected once at construction - never changes
    CPUIdType				m_cpuId;

    HWPCB* m_hwpcb;         // ptbr, asn, pc
    GuestMemory*			m_guestMemory;   // page walk reads
    Ev6SPAMShardManager*	m_tlb;           // TLB lookup/insert
	FaultDispatcher*		m_fault_dispatcher{ nullptr };
	ReservationManager*		m_reservationManager{ nullptr };
	CPUStateView* m_iprGlobalMaster{ nullptr };

    Ev6Translator(CPUIdType cpuId)
    :   m_cpuId(cpuId)
    ,   m_hwpcb(&globalHWPCBController(cpuId))
    ,   m_tlb(&globalSPAM(cpuId))
    ,	m_guestMemory(&global_GuestMemory())
    ,	m_fault_dispatcher(&globalFaultDispatcher(cpuId))
	,	m_reservationManager(&globalReservationManager())
		, m_iprGlobalMaster(getCPUStateView(cpuId))
    {
		
    }
    ~Ev6Translator() = default;


#pragma region EV6TranslateFastVA

	// ---------------------------------------------------------------------------
	// ev6TranslateFastVA
	//
	// Fast-path VA -> PA translation:
	//
	//  - Uses TLB only (no page walk).
	//  - Returns 'true' on TLB hit with a valid mapping, fills pa_out.
	//  - Returns 'false' on miss or if TLB entry is not usable.
	//  - Does NOT do page walking, permission traps, or fault classification.
	//    That is left to PAL / trap handlers or ev6TranslateFullVA.
	// ---------------------------------------------------------------------------


	AXP_HOT inline TranslationResult ev6TranslateFastVA(
		VAType va,
		AccessKind access,
		Mode_Privilege mode,
		PAType& pa_out,
		AlphaPTE* outPte = nullptr)  noexcept
	{
		SPDLOG_DEBUG("ev6TranslateFastVA VA: 0x{:08x}",va);

		const ASNType asn = static_cast<ASNType>(m_hwpcb->getASN());
		const VAType va_ctl = m_iprGlobalMaster->x->vaCtl.raw();//   // Get directly via global accessor

		// Canonical check
		if (!isCanonicalVA(va, va_ctl))
			return TranslationResult::NonCanonical;

		// Kseg fast path - no TLB, no page walk
		bool isIstream = (access == AccessKind::EXECUTE);
		TranslationResult ksegResult = tryKsegTranslate(va,  mode, isIstream, pa_out);
		if (ksegResult != TranslationResult::NotKseg)
			return ksegResult;   // Success or AccessViolation

		// Map access type to realm
		Realm realm = (access == AccessKind::EXECUTE) ? Realm::I : Realm::D;

		PFNType pfn = 0;
		SC_Type sizeClass = 0;
		AlphaN_S::PermMask perm = {};

		if (!m_tlb->tlbLookup(m_cpuId, realm, va, asn, pfn, perm, sizeClass))
			return TranslationResult::TlbMiss;

		const quint64 offset = extractOffset(va);
		pa_out = (static_cast<PFNType>(pfn) << PAGE_SHIFT) | offset;

		if (outPte) {
			AlphaPTE tmp;
			tmp.setPFN(pfn);
			tmp.setPermMask(perm);
			tmp.setValid(true);
			*outPte = tmp;
		}
		return TranslationResult::Success;
	}

#pragma endregion EV6TranslateFastVA

#pragma region Full Translation (Page Walk + Checks)

	// ---------------------------------------------------------------------------
	// ev6TranslateFullVA
	//
	// Full VA -> PA translation with EV6 semantics:
	//  1) Canonical VA check
	//  2) Page table walk (3-level)
	//  3) PTE valid bit check
	//  4) Permission check
	//  5) TLB fill
	//  6) Return PA and PTE
	// ---------------------------------------------------------------------------

	AXP_HOT inline TranslationResult ev6TranslateFullVA(
		VAType va,
		AccessKind access,
		Mode_Privilege mode,
		PAType& pa_out,
		AlphaPTE& outPte)  noexcept
	{
		// 1. Canonical check
		const VAType va_ctl = m_iprGlobalMaster->x->vaCtl.raw(); // globalIPRHotExt(cpuId).va_ctl;
		if (!isCanonicalVA(va, va_ctl)) {
			return TranslationResult::NonCanonical;
		}

		// 2. Kseg fast path - no TLB, no page walk
		bool isRead = (access == AccessKind::DataRead);
		TranslationResult ksegResult = tryKsegTranslate(va,  mode, isRead, pa_out);
		if (ksegResult != TranslationResult::NotKseg)
			return ksegResult;   // Success or AccessViolation

		// 3. Page walk
		const quint64 ptbr = m_hwpcb->ptbr; //  getPTBR_Active(cpuId);

		auto walkResult = walkPageTable_EV6(va, ptbr, mode, access);

		// 4. Convert walk result to TranslationResult
		if (!walkResult.success) {
			return toTranslationResult(walkResult);
		}

		// 4. Fill TLB
		Realm realm = (access == AccessKind::EXECUTE) ? Realm::I : Realm::D;
		ASNType asn = static_cast<ASNType>(m_hwpcb->getASN()); //  getASN_Active(cpuId);

		// 5. Build PermMask from PTE permission bits
		AlphaN_S::PermMask perm = walkResult.pte.protection8();

		// Extract permission bits from PTE and pack into byte
		// Assuming PermMask encoding (check your alpha_pte_core.h for exact layout):
		//   bit 0: kernel_read  (KRE)
		//   bit 1: kernel_write (KWE)
		//   bit 2: user_read    (URE)
		//   bit 3: user_write   (UWE)
		//   bit 4-7: unused or FOE/FOR/FOW


		m_tlb->tlbInsert(m_cpuId, realm, va, asn, walkResult.pte);

		// 6. Compute PA
		pa_out = (walkResult.pte.pfn() << PAGE_SHIFT) | extractOffset(va);
		outPte = walkResult.pte;

		return TranslationResult::Success;
	}

#pragma endregion Full Translation (Page Walk + Checks)


#pragma region EV6 Walk PageTable 


	enum class WalkStatus {

		Success,
		InvalidPTE,
		PageNotPresent,
		AccessViolation,
		BusError,
		FaultOnWrite,
		FaultOnRead
	};

	struct WalkResultEV6
	{
		bool success;
		AlphaPTE pte;
		quint64 pte_pa;
		WalkStatus status;

		enum FaultType {
			None,
			TNV,
			FOW,
			FOR_,
			FOE,
			ACV,
			BUS
		} fault;
	};


	/// \brief EV6 Page Table Walker (Layer-2 only)
	/// \param va      The virtual address to translate
	/// \param ptbr    The PTBR (from IPR)
	/// \param mode    Processor mode (Kernel/User/Exec); used for access bits
	/// \param access  Read / Write / Exec
	/// \param read64  Callback that returns 64-bit data from physical memory
	///
	/// \details
	/// This performs a full 3-level EV6 page table walk.
	/// It returns an AlphaPTE and fault information.
	/// TLB refill logic (insert into SPAM) happens ABOVE this layer.
	///
	// FIXED: Proper template syntax

	AXP_HOT inline WalkResultEV6 walkPageTable_EV6(
		VAType va,
		quint64 ptbr,
		Mode_Privilege mode,
		AccessKind access
		) noexcept
	{
		WalkResultEV6 R{};
		R.success = false;
		R.fault = WalkResultEV6::TNV;

		// --------------------------------------------------------
		// 1. Extract indices for 8KB page mode (EV6 default)
		// --------------------------------------------------------
		constexpr quint64 PAGE_SHIFT = 13;  // 8K pages
		constexpr quint64 L3_BITS = 10;
		constexpr quint64 L2_BITS = 12;
		constexpr quint64 L1_BITS = 8;

		const quint64 vpn = va >> PAGE_SHIFT;

		const quint64 idx_l1 = (vpn >> (L2_BITS + L3_BITS)) & ((1ULL << L1_BITS) - 1);
		const quint64 idx_l2 = (vpn >> L3_BITS) & ((1ULL << L2_BITS) - 1);
		const quint64 idx_l3 = vpn & ((1ULL << L3_BITS) - 1);

		// Each level entry is 8 bytes
		constexpr quint64 L1_ENTRY_SIZE = 8;
		constexpr quint64 L2_ENTRY_SIZE = 8;
		constexpr quint64 L3_ENTRY_SIZE = 8;

		// --------------------------------------------------------
		// 2. L1 lookup: PTE pointer = PTBR + idx*8
		// --------------------------------------------------------
		quint64  l1_raw = 0;
		const quint64 l1_pa = ptbr + idx_l1 * L1_ENTRY_SIZE;
		MEM_STATUS memSt1  =  m_guestMemory->read64(l1_pa, l1_raw);

		if (memSt1 != MEM_STATUS::Ok) {
			R.fault = WalkResultEV6::BUS;
			return R;
		}

		if (!(l1_raw & 0x1)) { // Valid bit #0?
			R.fault = WalkResultEV6::TNV;
			return R;
		}

		AlphaPTE l1_pte = AlphaPTE::fromRaw(l1_raw);

		// --------------------------------------------------------
		// 3. L2 lookup
		// --------------------------------------------------------

		quint64  l2_raw = 0;
		const quint64 l2_pa = (l1_pte.pfn() << PAGE_SHIFT) + idx_l2 * L2_ENTRY_SIZE;
		MEM_STATUS memSt2 = m_guestMemory->read64(l2_pa, l2_raw);

		if (memSt2 != MEM_STATUS::Ok) {
			R.fault = WalkResultEV6::BUS;
			return R;
		}

		if (!(l2_raw & 0x1)) {
			R.fault = WalkResultEV6::TNV;
			return R;
		}

		AlphaPTE l2_pte = AlphaPTE::fromRaw(l2_raw);

		// --------------------------------------------------------
		// 4. L3 lookup -> final PTE
		// --------------------------------------------------------
		quint64  l3_raw = 0;
		const quint64 l3_pa = (l2_pte.pfn() << PAGE_SHIFT) + idx_l3 * L3_ENTRY_SIZE;
		MEM_STATUS memSt3 = m_guestMemory->read64(l3_pa, l3_raw);
		if (memSt3 != MEM_STATUS::Ok) {
			R.fault = WalkResultEV6::BUS;
			return R;
		}

		if (!(l3_raw & 0x1)) {
			R.fault = WalkResultEV6::TNV;
			return R;
		}

		AlphaPTE final_pte = AlphaPTE::fromRaw(l3_raw);

		// --------------------------------------------------------
		// 5. Check access rights
		// --------------------------------------------------------

		switch (access) {
		case AccessKind::DataWrite:
			if (final_pte.faultOnWrite()) { R.fault = WalkResultEV6::FOW; return R; }
			break;

		case AccessKind::DataRead:
			if (final_pte.faultOnRead()) { R.fault = WalkResultEV6::FOR_; return R; }
			break;

		case AccessKind::EXECUTE:
			if (final_pte.faultOnExec()) { R.fault = WalkResultEV6::FOE; return R; }
			break;

		default:
			break;
		}


		// (Optional) Check mode-specific rules
		// (depends on your AlphaPTE_Core implementation)

		// --------------------------------------------------------
		// 6. Success
		// --------------------------------------------------------
		R.success = true;
		R.fault = WalkResultEV6::None;
		R.pte = final_pte;
		R.pte_pa = l3_pa;
		return R;
	}


	// ============================================================================
	// 2. WalkStatus - INTERNAL to walkPageTable_EV6 (convert to TranslationResult)
	// ============================================================================
	// Keep WalkStatus internal, but add converter:

	static AXP_HOT inline  TranslationResult toTranslationResult(const WalkResultEV6& walk) {
		if (walk.success) return TranslationResult::Success;

		switch (walk.fault) {
		case WalkResultEV6::TNV:
			return TranslationResult::PageNotPresent;
		case WalkResultEV6::FOW:
			return TranslationResult::FaultOnWrite;
		case WalkResultEV6::FOR_:
			return TranslationResult::FaultOnRead;
		case WalkResultEV6::FOE:
			return TranslationResult::FaultOnExecute;
		case WalkResultEV6::ACV:
			return TranslationResult::AccessViolation;
		case WalkResultEV6::BUS:
			return TranslationResult::BusError;
		default:
			return TranslationResult::PageNotPresent;
		}
	}

#pragma endregion 


#pragma region TLB PTE Format Converters

	// ====================================================================
	// ITB_PTE / DTB_PTE register format <-> canonical AlphaPTE
	//
	// The EV6 hardware register format for ITB_PTE and DTB_PTE has
	// permission bits at DIFFERENT positions than the architectural
	// memory PTE format stored in AlphaPTE.raw.
	//
	// These converters must be used when:
	//   - HW_MTPR writes ITB_PTE/DTB_PTE (register -> AlphaPTE for TLB fill)
	//   - HW_MFPR reads ITB_PTE/DTB_PTE  (AlphaPTE -> register for read-back)
	//
	// DTB_PTE register bit positions:
	//   PFN[51:32]  ASM[34]
	//   URE[12] SRE[11] ERE[10] KRE[9]
	//   UWE[8]  SWE[7]  EWE[6]  KWE[5]
	//   FOW[4]  FOR[3]
	//   (FOE is NOT present in DTB_PTE)
	//
	// ITB_PTE register bit positions: same layout for read enables,
	//   no write enables (I-stream only), no FOW/FOR.
	// ====================================================================

	static inline AlphaPTE fromDtbPteRegister(quint64 raw) noexcept
	{
		AlphaPTE p(0);

		// PFN: PA[43:13] but bits [15:0] contain permissions/control fields.
		// Clear the overlap zone before extracting PFN.
		// PA[15:13] are architecturally zero (page alignment).
		const quint64 paClean = raw & ~0xFFFFULL;       // zero bits [15:0]
		const PFNType pfn = static_cast<PFNType>(
			(paClean >> 13) & ((1ULL << 28) - 1));       // 28-bit PFN
		p.setPfn(pfn);

		// GH[1:0] at bits [6:5]
		p.setGH(static_cast<quint8>((raw >> 5) & 0x3));

		// ASM at bit [4]
		if ((raw >> 4) & 1) p.setAsm(true);

		// Read permissions: KRE[8] ERE[9] SRE[10] URE[11]
		if ((raw >> 8) & 1) p.insert<AlphaPTE::PTE_BIT_KRE, 1>(1);
		if ((raw >> 9) & 1) p.insert<AlphaPTE::PTE_BIT_ERE, 1>(1);
		if ((raw >> 10) & 1) p.insert<AlphaPTE::PTE_BIT_SRE, 1>(1);
		if ((raw >> 11) & 1) p.insert<AlphaPTE::PTE_BIT_URE, 1>(1);

		// Write permissions: KWE[12] EWE[13] SWE[14] UWE[15]
		if ((raw >> 12) & 1) p.insert<AlphaPTE::PTE_BIT_KWE, 1>(1);
		if ((raw >> 13) & 1) p.insert<AlphaPTE::PTE_BIT_EWE, 1>(1);
		if ((raw >> 14) & 1) p.insert<AlphaPTE::PTE_BIT_SWE, 1>(1);
		if ((raw >> 15) & 1) p.insert<AlphaPTE::PTE_BIT_UWE, 1>(1);

		// FOW[2] FOR[1]
		if ((raw >> 2) & 1) p.insert<AlphaPTE::PTE_BIT_FOW, 1>(1);
		if ((raw >> 1) & 1) p.insert<AlphaPTE::PTE_BIT_FOR, 1>(1);

		if (pfn != 0) p.setValid(true);

		return p;
	}



	static inline quint64 toDtbPteRegister(const AlphaPTE& p) noexcept
	{
		quint64 raw = 0;

		// PFN: PA[43:16] -- shift PFN left by 13 (page shift), clear overlap zone
		raw |= (static_cast<quint64>(p.pfn()) << 13) & ~0xFFFFULL;

		// ASM at bit [4]
		if (p.bitASM()) raw |= (1ULL << 4);

		// GH[1:0] at bits [6:5]
		raw |= (static_cast<quint64>(p.gh()) << 5);

		// Read permissions: KRE[8] ERE[9] SRE[10] URE[11]
		if (p.bitKRE()) raw |= (1ULL << 8);
		if (p.bitERE()) raw |= (1ULL << 9);
		if (p.bitSRE()) raw |= (1ULL << 10);
		if (p.bitURE()) raw |= (1ULL << 11);

		// Write permissions: KWE[12] EWE[13] SWE[14] UWE[15]
		if (p.bitKWE()) raw |= (1ULL << 12);
		if (p.bitEWE()) raw |= (1ULL << 13);
		if (p.bitSWE()) raw |= (1ULL << 14);
		if (p.bitUWE()) raw |= (1ULL << 15);

		// FOW[2] FOR[1]
		if (p.bitFOW()) raw |= (1ULL << 2);
		if (p.bitFOR()) raw |= (1ULL << 1);

		return raw;
	}

	static inline AlphaPTE fromItbPteRegister(quint64 raw) noexcept
	{
		AlphaPTE p(0);

		// PFN: PA[43:13], clear overlap zone [12:0]
		const quint64 paClean = raw & ~0x1FFFULL;
		const PFNType pfn = static_cast<PFNType>(
			(paClean >> 13) & ((1ULL << 28) - 1));  // 28-bit PFN
		p.setPfn(pfn);

		// GH[1:0] at bits [6:5]
		p.setGH(static_cast<quint8>((raw >> 5) & 0x3));

		// ASM at bit [4]
		if ((raw >> 4) & 1) p.setAsm(true);

		// Read permissions only (I-stream):
		// KRE[9] ERE[10] SRE[11] URE[12]
		if ((raw >> 9) & 1) p.insert<AlphaPTE::PTE_BIT_KRE, 1>(1);
		if ((raw >> 10) & 1) p.insert<AlphaPTE::PTE_BIT_ERE, 1>(1);
		if ((raw >> 11) & 1) p.insert<AlphaPTE::PTE_BIT_SRE, 1>(1);
		if ((raw >> 12) & 1) p.insert<AlphaPTE::PTE_BIT_URE, 1>(1);

		// No write permissions, no FOW/FOR for ITB

		if (pfn != 0) p.setValid(true);

		return p;
	}

	static inline quint64 toItbPteRegister(const AlphaPTE& p) noexcept
	{
		quint64 raw = 0;

		// PFN: PA[43:13], clear overlap zone
		raw |= (static_cast<quint64>(p.pfn()) << 13) & ~0x1FFFULL;

		// GH[1:0] at bits [6:5]
		raw |= (static_cast<quint64>(p.gh()) << 5);

		// ASM at bit [4]
		if (p.bitASM()) raw |= (1ULL << 4);

		// Read permissions: KRE[9] ERE[10] SRE[11] URE[12]
		if (p.bitKRE()) raw |= (1ULL << 9);
		if (p.bitERE()) raw |= (1ULL << 10);
		if (p.bitSRE()) raw |= (1ULL << 11);
		if (p.bitURE()) raw |= (1ULL << 12);

		return raw;
	}

#pragma endregion TLB PTE Format Converters
#pragma region SPAM Translation Helpers




	// ============================================================================
 // translateVA_Data ( Architecturally Correct Alpha AXP Behavior)
 // ============================================================================
 // ASCII ONLY / UTF-8 (no BOM)
 // Header-only inline implementation
 //
 // References:
 //  - Alpha AXP System Reference Manual (V6, 1994)
 //      * Chapter 5: Memory Management / Translation Buffers
 //      * Chapter 6: PALcode Environment
 //  - TLB semantics, ASN handling, KSEG behavior, VA_CTL interpretation
 //
 // This version fixes:
 //   x KSEG ordering (before VA_CTL)
 //   x Variable page size PFN shift (CRITICAL BUG FIX)
 //   x Proper PAL physical mapping semantics
 //   x Correct DTB fault generation path
 //   x Clean separation of translation stages
 // ============================================================================

	AXP_HOT AXP_ALWAYS_INLINE
		TranslationResult translateVA_Data(
			quint64 va,
			quint64 pc,
			bool isWrite,
			/*out*/ quint64& pa,
			quint8 opcode) noexcept
	{
		// ========================================================================
		// CHECK 1: PAL mode (physical addressing, PC[0] semantics not applied here)
		// ========================================================================
		if (m_iprGlobalMaster->isInPalMode()) {
			// PAL uses physical addressing directly
			pa = va;
			return TranslationResult::Success;
		}

		// Current privilege mode
		const Mode_Privilege mode =
			static_cast<Mode_Privilege>(m_hwpcb->getCM());

		// ========================================================================
		// CHECK 2: KSEG (must precede VA_CTL handling)
		// ========================================================================
		PAType kseg_pa;
		TranslationResult ksegResult =
			tryKsegTranslate(va, mode, /*isIstream*/ false, kseg_pa);

		if (ksegResult == TranslationResult::Success) {
			pa = kseg_pa;
			return TranslationResult::Success;
		}

		if (ksegResult == TranslationResult::AccessViolation) {
			return ksegResult;
		}

		// ========================================================================
		// CHECK 3: VA_CTL - Physical Mode (bit[1] == 0)
		// ========================================================================
		const quint64 vaCtl = m_iprGlobalMaster->x->vaCtl.raw();
		const bool physicalMode = ((vaCtl & 0x2ULL) == 0);

		if (physicalMode) {
			// Identity mapping
			pa = va;
			return TranslationResult::Success;
		}

		// ========================================================================
		// CHECK 4: DTB lookup
		// ========================================================================
		const ASNType asn =
			static_cast<ASNType>(m_hwpcb->getASN());

		PFNType pfn;
		AlphaN_S::PermMask perm;
		SC_Type sizeClass;
		const AlphaPTE* pte = nullptr;

		if (!m_tlb->tlbLookup(
			m_cpuId,
			Realm::D,
			va,
			asn,
			pfn,
			perm,
			sizeClass,
			&pte))
		{
			// ------------------------------
			// DTB MISS
			// ------------------------------
			PendingEvent ev =
				makeDTBMissSingleEvent(
					m_cpuId,
					va,
					asn,
					pc,
					isWrite,
					opcode);

			m_fault_dispatcher->setPendingEvent(ev);

			// Update MM_STAT (implementation-defined but required by PALcode)
			m_iprGlobalMaster->x->mm_stat = ev.mm_stat;

			return TranslationResult::DlbMiss;
		}

		// ========================================================================
		// CHECK 5: Access Permissions (includes FOR / FOW)
		// ========================================================================
		bool allowed = false;

		if (isWrite) {
			// Includes FOW (fault-on-write)
			allowed = pte->canWrite(mode);
		}
		else {
			// Includes FOR (fault-on-read)
			allowed = pte->canRead(mode);
		}

		if (!allowed) {
			// ------------------------------
			// Permission fault classification
			// ------------------------------
			if (isWrite && pte->bitFOW()) {
				PendingEvent ev =
					makeFaultOnWriteEvent(m_cpuId, va, opcode);
				m_fault_dispatcher->setPendingEvent(ev);
				return TranslationResult::FaultOnWrite;
			}
			else if (!isWrite && pte->bitFOR()) {
				PendingEvent ev =
					makeFaultOnReadEvent(m_cpuId, va, opcode);
				m_fault_dispatcher->setPendingEvent(ev);
				return TranslationResult::FaultOnRead;
			}
			else {
				PendingEvent ev =
					makeDTBAccessViolationEvent(
						m_cpuId,
						va,
						isWrite,
						opcode);

				m_fault_dispatcher->setPendingEvent(ev);
				return TranslationResult::AccessViolation;
			}
		}

		// ========================================================================
		// CHECK 6: Construct Physical Address (VARIABLE PAGE SIZE FIX)
		// ========================================================================
		const quint64 pageShift =
			PageSizeHelpers::pageShift(sizeClass);

		const quint64 pageMask =
			(1ULL << pageShift) - 1ULL;

		// CRITICAL: shift must match page size (NOT fixed 13!)
		pa = (static_cast<quint64>(pfn) << pageShift)
			| (va & pageMask);

		return TranslationResult::Success;
	}



	// ============================================================================
	// Alpha AXP - FINAL Translation Functions (Aligned + Instruction)
	// ============================================================================
	// ASCII ONLY / UTF-8 (no BOM)
	// Header-only implementations
	//
	// References:
	//  - Alpha AXP System Reference Manual (V6, 1994)
	//      * §2: Data alignment rules
	//      * §4.2.x: Load/Store alignment requirements
	//      * §5: Memory system + translation buffers
	//
	// KEY ARCHITECTURAL RULES:
	//  - Alignment is checked on VA (before translation)
	//  - Physical address alignment is guaranteed by construction
	//  - Unaligned accesses raise exception (no silent fixup in hardware)
	//  - Instruction fetch MUST be naturally aligned (longword = 4 bytes)
	//
	// DESIGN DECISION:
	//  - We DO NOT assert() — we generate architectural traps
	//  - Assertions are only for emulator invariants, not ISA-visible faults
	// ============================================================================



	// ============================================================================
	// DATA ACCESS WITH ALIGNMENT
	// ============================================================================
	AXP_HOT AXP_ALWAYS_INLINE
		TranslationResult translateVA_WithAlignment(
			quint64 va,
			quint64 pc,
			quint8  accessSize,   // 1,2,4,8
			bool    isWrite,
			/*out*/ quint64& pa,
			quint8  opcode) noexcept
	{
		// ------------------------------------------------------------------------
		// CHECK 0: Alignment (performed on VA, BEFORE translation)
		//
		// ASA Rule:
		//   "Unaligned references cause an exception"
		//   Alignment requirement = access size
		// ------------------------------------------------------------------------
		const quint64 alignMask = static_cast<quint64>(accessSize) - 1ULL;

		if ((va & alignMask) != 0ULL) {
			// ------------------------------------------------------------
			// UNALIGNED ACCESS FAULT
			// ------------------------------------------------------------
			PendingEvent ev =
				makeUnalignedEvent(
					m_cpuId,
					va,
					isWrite,
					opcode);

			m_fault_dispatcher->setPendingEvent(ev);

			return TranslationResult::Unaligned;
		}

		// ------------------------------------------------------------------------
		// Proceed with normal data translation
		// ------------------------------------------------------------------------
		TranslationResult result =
			translateVA_Data(va, pc, isWrite, pa, opcode);

		if (result != TranslationResult::Success)
			return result;

		// ------------------------------------------------------------------------
		// OPTIONAL DEBUG SAFETY CHECK (not architectural)
		// Ensures PFN alignment + offset composition is correct
		// ------------------------------------------------------------------------
#ifdef _DEBUG
		if ((pa & alignMask) != 0ULL) {
			// This indicates a BUG in translation logic (should never happen)
			// Do NOT raise architectural fault — this is emulator invariant
			Q_ASSERT(false && "PA misalignment indicates PFN/pageShift bug");
		}
#endif

		return TranslationResult::Success;
	}



	// ============================================================================
	// INSTRUCTION FETCH TRANSLATION (ITB PATH)
	// ============================================================================
	AXP_HOT AXP_ALWAYS_INLINE
		TranslationResult translateVA_Instruction(
			quint64 va,
			/*out*/ quint64& pa) noexcept
	{
		// ------------------------------------------------------------------------
		// CHECK 0: Instruction alignment (Alpha requires 4-byte alignment)
		//
		// ASA Rule:
		//   Instructions are longword-aligned (4 bytes)
		// ------------------------------------------------------------------------
		if ((va & 0x3ULL) != 0ULL) {
			PendingEvent ev =
				makeUnalignedEvent(
					m_cpuId,
					va,
					/*isWrite*/ false,
					/*opcode*/ 0 /* unknown at fetch */);

			m_fault_dispatcher->setPendingEvent(ev);

			return TranslationResult::Unaligned;
		}

		// ------------------------------------------------------------------------
		// CHECK 1: PAL mode (physical addressing, clear PC[0])
		// ------------------------------------------------------------------------
		if (m_iprGlobalMaster->isInPalMode()) {
			pa = va & ~0x1ULL;   // PAL-mode PC[0] semantics
			return TranslationResult::Success;
		}

		const Mode_Privilege mode =
			static_cast<Mode_Privilege>(m_hwpcb->getCM());

		// ------------------------------------------------------------------------
		// CHECK 2: KSEG (must precede VA_CTL)
		// ------------------------------------------------------------------------
		PAType kseg_pa;
		TranslationResult ksegResult =
			tryKsegTranslate(va, mode, /*isIstream*/ true, kseg_pa);

		if (ksegResult == TranslationResult::Success) {
			pa = kseg_pa;
			return TranslationResult::Success;
		}

		if (ksegResult == TranslationResult::AccessViolation) {
			return ksegResult;
		}

		// ------------------------------------------------------------------------
		// CHECK 3: VA_CTL physical mode
		// ------------------------------------------------------------------------
		const quint64 vaCtl = m_iprGlobalMaster->x->vaCtl.raw();
		const bool physicalMode = ((vaCtl & 0x2ULL) == 0ULL);

		if (physicalMode) {
			pa = va;
			return TranslationResult::Success;
		}

		// ------------------------------------------------------------------------
		// CHECK 4: ITB lookup
		// ------------------------------------------------------------------------
		const ASNType asn =
			static_cast<ASNType>(m_hwpcb->getASN());

		PFNType pfn;
		AlphaN_S::PermMask perm;
		SC_Type sizeClass;
		const AlphaPTE* pte = nullptr;

		if (!m_tlb->tlbLookup(
			m_cpuId,
			Realm::I,
			va,
			asn,
			pfn,
			perm,
			sizeClass,
			&pte))
		{
			// ------------------------------
			// ITB MISS
			// ------------------------------
			PendingEvent ev =
				makeITBMissEvent(m_cpuId, va);

			m_fault_dispatcher->setPendingEvent(ev);

			return TranslationResult::TlbMiss;
		}

		// ------------------------------------------------------------------------
		// CHECK 5: Execute permission (includes FOE)
		// ------------------------------------------------------------------------
		if (!pte->canExecute(mode)) {

			if (pte->bitFOE()) {
				PendingEvent ev =
					makeFaultOnExecuteEvent(m_cpuId, va);
				m_fault_dispatcher->setPendingEvent(ev);
				return TranslationResult::FaultOnExecute;
			}
			else {
				PendingEvent ev =
					makeITBAccessViolationEvent(m_cpuId, va);
				m_fault_dispatcher->setPendingEvent(ev);
				return TranslationResult::AccessViolation;
			}
		}

		// ------------------------------------------------------------------------
		// CHECK 6: Construct Physical Address (variable page size)
		// ------------------------------------------------------------------------
		const quint64 pageShift =
			PageSizeHelpers::pageShift(sizeClass);

		const quint64 pageMask =
			(1ULL << pageShift) - 1ULL;

		pa = (static_cast<quint64>(pfn) << pageShift)
			| (va & pageMask);

#ifdef _DEBUG
		// Instruction fetch must remain aligned
		if ((pa & 0x3ULL) != 0ULL) {
			Q_ASSERT(false && "Instruction PA misalignment indicates PFN bug");
		}
#endif

		return TranslationResult::Success;
	}

	// ============================================================================
	// Convenience Wrappers
	// ============================================================================

	/**
	 * @brief Translate for load operation (read-only)
	 */
	AXP_HOT AXP_ALWAYS_INLINE TranslationResult translateVA_Load(

			quint64 va,
			quint64 pc,
			/*out*/ quint64& pa, quint8 opcode) noexcept
	{
		return translateVA_Data( va, pc,  false, pa, opcode);
	}

	/**
	 * @brief Translate for store operation (write)
	 */
	AXP_HOT AXP_ALWAYS_INLINE 	TranslationResult translateVA_Store(
			quint64 va,
			quint64 pc,
			/*out*/ quint64& pa, quint8 opcode) noexcept
	{
		return translateVA_Data( va, pc,  true, pa, opcode);
	}

	/**
	 * @brief Translate for aligned quadword load (LDQ)
	 */
	AXP_HOT AXP_ALWAYS_INLINE TranslationResult translateVA_LDQ(
			quint64 va,
			quint64 pc,
			/*out*/ quint64& pa, quint8 opcode) noexcept
	{
		return translateVA_WithAlignment( va, pc, 8,  false, pa, opcode);
	}

	/**
	 * @brief Translate for aligned quadword store (STQ)
	 */
	AXP_HOT AXP_ALWAYS_INLINE TranslationResult translateVA_STQ(
			quint64 va,
			quint64 pc,
			/*out*/ quint64& pa, quint8 opcode) noexcept
	{
		return translateVA_WithAlignment( va, pc, 8,  true, pa, opcode);
	}

	/**
	 * @brief Translate for aligned longword load (LDL)
	 */
	AXP_HOT AXP_ALWAYS_INLINE
		TranslationResult translateVA_LDL(
			quint64 va,
			quint64 pc,
			/*out*/ quint64& pa, quint8 opcode) noexcept
	{
		return translateVA_WithAlignment( va, pc, 4,  false, pa, opcode);
	}

	/**
	 * @brief Translate for aligned longword store (STL)
	 */
	AXP_HOT AXP_ALWAYS_INLINE	TranslationResult translateVA_STL(
			quint64 va,
			quint64 pc,
			/*out*/ quint64& pa, quint8 opcode) noexcept
	{
		return translateVA_WithAlignment( va, pc, 4,  true, pa, opcode);
	}

	// ============================================================================
	// Stack Operation Helpers (for CHMx, exceptions, etc.)
	// ============================================================================

	/**
	 * @brief Translate and push quadword to stack
	 *
	 * Combines translation, permission check, alignment, and write.
	 * Used by CHMx, exception handlers, CALL_PAL, etc.
	 *
	 * @param cpuId CPU identifier
	 * @param sp Stack pointer (will be decremented by 8)
	 * @param value Value to push
	 * @param mode Privilege mode of target stack
	 * @return true if successful, false if exception queued
	 */
	AXP_HOT AXP_ALWAYS_INLINE	bool pushStack(
			quint64& sp,
			quint64 value,
			quint64 pc,
			PrivilegeLevel mode, quint8 opcode) noexcept
	{
		// Pre-decrement stack pointer
		sp -= 8;

		quint64 pa;
		TranslationResult result = translateVA_STQ(sp, pc,  pa, opcode);

		if (result != TranslationResult::Success) {
			// Exception already queued by translateVA_STQ
			return false;
		}

		// Write to physical address
	MEM_STATUS mStatus =	m_guestMemory->write64(pa, value);

		return true;
	}

	/**
	 * @brief Translate and pop quadword from stack
	 *
	 * @param cpuId CPU identifier
	 * @param sp Stack pointer (will be incremented by 8)
	 * @param value [out] Value popped
	 * @param mode Privilege mode of source stack
	 * @return true if successful, false if exception queued
	 */
	AXP_HOT AXP_ALWAYS_INLINE	bool popStack(
				quint64& sp,
			/*out*/ quint64& value,
			quint64 pc,
			PrivilegeLevel mode, quint8 opcode) noexcept
	{
		quint64 pa;
		TranslationResult result = translateVA_LDQ( sp, pc,  pa, opcode);

		if (result != TranslationResult::Success) {
			// Exception already queued by translateVA_LDQ
			return false;
		}

		// Read from physical address
		MEM_STATUS mStatus = m_guestMemory->read64(pa, value);

		// Post-increment stack pointer
		sp += 8;

		return true;
	}

	static AXP_HOT AXP_ALWAYS_INLINE bool checkAlignment(quint64 addr, quint8 size) noexcept
	{
		if (size == 0)
			return true;
		return (addr & (static_cast<quint64>(size) - 1ULL)) == 0ULL;
	}


	// In MBox or as a shared helper
	AXP_HOT AXP_ALWAYS_INLINE bool translateLoadAddress(
		PipelineSlot& slot,
		quint64 va,
		quint64& pa,
		MemoryAccessType accessType,
		const char* instrName) noexcept
	{
		debugLog(QString("[%1] Translating VA: 0x%2")
			.arg(instrName)
			.arg(va, 16, 16, QChar('0')));

		// ================================================================
		// Use centralized translation helper from PTE library
		// ================================================================

		bool bAccessType = (accessType == MemoryAccessType::WRITE);
		TranslationResult tr = translateVA_Data( va, slot.di.pc, bAccessType, pa, extractOpcode(slot.di.rawBits()));

		if (tr != TranslationResult::Success) {
			debugLog(QString("[%1]  TRANSLATION FAILED: %2")
				.arg(instrName)
				.arg(static_cast<int>(tr)));

			slot.faultPending = true;
			slot.trapCode = mapDTranslationFault(tr);
			slot.faultVA = va;
			return false;
		}

		debugLog(QString("[%1]  Translation: VA 0x%2 -> PA 0x%3")
			.arg(instrName)
			.arg(va, 16, 16, QChar('0'))
			.arg(pa, 16, 16, QChar('0')));

		return true;
	}


	AXP_HOT AXP_ALWAYS_INLINE void contextSwitch() const noexcept
	{
		// Break this CPU's reservation on context switch
		m_reservationManager->breakReservation(m_cpuId);
	}
	AXP_HOT AXP_ALWAYS_INLINE void contextSwitch(CPUIdType cpuId) const noexcept
	{
		// Break this CPU's reservation on context switch
		m_reservationManager->breakReservation(cpuId);
	}

#pragma endregion SPAM Translation Helpers


#pragma region Pal Memory Helpers

	// ============================================================================
	// VIRTUAL MEMORY READ/WRITE HELPERS
	// ============================================================================

	/**
	 * @brief Read single byte from virtual address.
	 */
	AXP_HOT  inline  MEM_STATUS readVirtualByteFromVA( quint64 va, quint8& byte, quint8 opcode = 0x00)  noexcept
	{
		quint64 paOut;
		AlphaPTE pte{};

		TranslationResult tr = ev6TranslateFastVA(
			va,
			AccessKind::DataRead,  // <- READ for byte read
			static_cast<Mode_Privilege>(m_hwpcb->getCM()),
			paOut,
			&pte
		);

		if (tr != TranslationResult::Success) {
			raiseTranslationFault(m_cpuId, va, tr, m_fault_dispatcher, opcode);
			return MEM_STATUS::TlbMiss;
		}

		//GuestMemory& guestMem = global_GuestMemory();
		return m_guestMemory->read8(paOut, byte);
	}

	/**
	 * @brief Write single byte to virtual address.
	 */
	AXP_HOT  inline  MEM_STATUS writeVirtualByte(quint64 va, quint8 byte,  quint8 opcode = 0x00) noexcept
	{
		quint64 paOut;
		AlphaPTE pte{};

		TranslationResult tr = ev6TranslateFastVA(
			va,
			AccessKind::DataWrite,
			static_cast<Mode_Privilege>(m_hwpcb->getCM()),
			paOut,
			&pte
		);

		if (tr != TranslationResult::Success) {
			raiseTranslationFault(m_cpuId, va, tr, m_fault_dispatcher,opcode);
			return MEM_STATUS::TlbMiss;
		}
		return m_guestMemory->write8(paOut, byte);
	}

	/**
	 * @brief Read word (16-bit) from virtual address.
	 */
	AXP_HOT  inline  MEM_STATUS readVirtualWord(quint64 va, quint16& word, quint8 opcode = 0x00) noexcept
	{
		quint64 paOut;
		AlphaPTE pte{};

		TranslationResult tr = ev6TranslateFastVA(
			va,
			AccessKind::DataRead,
			static_cast<Mode_Privilege>(m_hwpcb->getCM()),
			paOut,
			&pte
		);

		if (tr != TranslationResult::Success) {
			raiseTranslationFault(m_cpuId, va, tr, m_fault_dispatcher,opcode);
			return MEM_STATUS::TlbMiss;
		}
		return m_guestMemory->read16(paOut, word);
	}

	/**
	 * @brief Read longword (32-bit) from virtual address.
	 */
	AXP_HOT  inline  MEM_STATUS readVirtualLongword( quint64 va, quint32& lw, quint8 opcode = 0x00)  noexcept
	{
		quint64 paOut;
		AlphaPTE pte{};

		TranslationResult tr = ev6TranslateFastVA(
			va,
			AccessKind::DataRead,
			static_cast<Mode_Privilege>(m_hwpcb->getCM()),
			paOut,
			&pte
		);

		if (tr != TranslationResult::Success) {
			raiseTranslationFault(m_cpuId, va, tr, m_fault_dispatcher, opcode);
			return MEM_STATUS::TlbMiss;
		}
		return m_guestMemory->read32(paOut, lw);
	}

	/**
	 * @brief Read quadword (64-bit) from virtual address.
	 */
	AXP_HOT  inline  MEM_STATUS readVirtualQuadword( quint64 va, quint64& qw, quint8 opcode = 0x00)  noexcept
	{
		quint64 paOut;
		AlphaPTE pte{};

		TranslationResult tr = ev6TranslateFastVA(
			va,
			AccessKind::DataRead,
			static_cast<Mode_Privilege>(m_hwpcb->getCM()),
			paOut,
			&pte
		);

		if (tr != TranslationResult::Success) {
			raiseTranslationFault(m_cpuId, va, tr, m_fault_dispatcher,opcode);
			return MEM_STATUS::TlbMiss;
		}
		return m_guestMemory->read64(paOut, qw);
	}

	/**
	 * @brief Write quadword (64-bit) to virtual address.
	 */
	AXP_HOT  inline   MEM_STATUS writeVirtualQuadword(quint64 va, quint64 qw, quint8 opcode) noexcept
	{
		quint64 paOut;
		AlphaPTE pte{};

		TranslationResult tr = ev6TranslateFastVA(
			va,
			AccessKind::DataWrite,
			static_cast<Mode_Privilege>(m_hwpcb->getCM()),
			paOut,
			&pte
		);

		if (tr != TranslationResult::Success) {
			raiseTranslationFault(m_cpuId, va, tr, m_fault_dispatcher,opcode);
			return MEM_STATUS::TlbMiss;
		}
		return m_guestMemory->write64(paOut, qw);
	}

	// ============================================================================
	// BULK READ/WRITE HELPERS
	// ============================================================================

	/**
	 * @brief Read string from virtual memory (null-terminated or max length).
	 */
	AXP_HOT  inline   quint64 readVirtualString( quint64 va, quint8* buffer, quint64 maxLen, quint8 opcode = 0x00) noexcept
	{
		if (!buffer || maxLen == 0) {
			return 0;
		}

		quint64 bytesRead = 0;

		for (quint64 i = 0; i < maxLen; i++) {
			quint8 ch;
			if (readVirtualByteFromVA( va + i, ch, opcode) != MEM_STATUS::Ok) {
				break;  // Fault
			}

			buffer[i] = ch;
			bytesRead++;

			if (ch == 0) {
				break;  // Null terminator
			}
		}

		return bytesRead;
	}

	/**
	 * @brief Write buffer to virtual memory.
	 */
	AXP_HOT  inline  quint64 writeVirtualBuffer( quint64 va, const quint8* buffer, quint64 length) noexcept
	{
		if (!buffer || length == 0) {
			return 0;
		}

		quint64 bytesWritten = 0;

		for (quint64 i = 0; i < length; i++) {
			if (writeVirtualByte( va + i, buffer[i]) != MEM_STATUS::Ok) {
				break;  // Fault
			}
			bytesWritten++;
		}

		return bytesWritten;
	}

	// ============================================================================
	// ZERO-COPY VALIDATION (CORRECTED)
	// ============================================================================

	/**
	 * @brief Check if virtual address range is readable.
	 *
	 * Validates TLB translation without actually reading memory.
	 */
	AXP_HOT  inline  bool isVirtualRangeReadable( quint64 va, quint64 length)  noexcept
	{
		// Check pages (8KB aligned)
		constexpr quint64 PAGE_SIZE_inl = 8192;
		quint64 startPage = va & ~(PAGE_SIZE_inl - 1);
		quint64 endPage = (va + length - 1) & ~(PAGE_SIZE_inl - 1);

		for (quint64 page = startPage; page <= endPage; page += PAGE_SIZE_inl) {
			quint64 paOut;
			AlphaPTE pte{};

			// Try to translate this page
			TranslationResult tr = ev6TranslateFastVA(
				page,
				AccessKind::DataRead,
				static_cast<Mode_Privilege>(m_hwpcb->getCM()),
				paOut,
				&pte
			);

			if (tr != TranslationResult::Success) {
				return false;  // Page not readable
			}

			// Optional: Check page permissions
			if (!pte.isValid() || !pte.canRead()) {
				return false;
			}
		}

		return true;  // All pages readable
	}

	/**
	 * @brief Check if virtual address range is writable.
	 */
	AXP_HOT  inline  bool isVirtualRangeWritable(quint64 va, quint64 length)  noexcept
	{
		constexpr quint64 PAGE_SIZE = 8192;
		quint64 startPage = va & ~(PAGE_SIZE - 1);
		quint64 endPage = (va + length - 1) & ~(PAGE_SIZE - 1);

		for (quint64 page = startPage; page <= endPage; page += PAGE_SIZE) {
			quint64 paOut;
			AlphaPTE pte{};

			TranslationResult tr = ev6TranslateFastVA(
				page,
				AccessKind::DataWrite,
				static_cast<Mode_Privilege>(m_hwpcb->getCM()),
				paOut,
				&pte
			);

			if (tr != TranslationResult::Success) {
				return false;
			}

			// Check write permissions
			if (!pte.isValid() || !pte.canWrite()) {
				return false;
			}
		}

		return true;
	}

#pragma endregion Pal Memory Helpers


#pragma region TLB Helpers

	static constexpr unsigned DTB_TAG_ASN_BITS = 8;
	static constexpr quint64 DTB_TAG_ASN_MASK = (1ULL << DTB_TAG_ASN_BITS) - 1;
	/**
 * @brief Extract Virtual Page Number from TLB tag
 * @param tag TLB_TAG value (from DTB_TAG or ITB_TAG IPR)
 * @return VPN (Virtual Page Number)
 */
	static AXP_HOT AXP_ALWAYS_INLINE quint64 extractVPNFromTLBTag(quint64 tag)  noexcept
	{
		// VPN is everything above ASN
		return tag >> DTB_TAG_ASN_BITS;
	}

	/**
	 * @brief Extract virtual address from TLB tag
	 * @param tag TLB_TAG value (from DTB_TAG or ITB_TAG IPR)
	 * @return Page-aligned virtual address
	 */
	static AXP_HOT AXP_ALWAYS_INLINE  quint64 extractVAFromTLBTag(quint64 vpn,
		const quint8 sizeClass,
		quint64 originalVA)  noexcept
	{
		const quint64 shift = PageSizeHelpers::pageShift(sizeClass);
		const quint64 pageOffsetMask = (1ULL << shift) - 1;
		return (vpn << shift) | (originalVA & pageOffsetMask);
	}

	/**
	 * @brief Extract virtual address from TLB tag
	 * @param tag TLB_TAG value (from DTB_TAG or ITB_TAG IPR)
	 * @return Page-aligned virtual address
	 */
	static AXP_HOT AXP_ALWAYS_INLINE quint64 extractVAFromTLBTag(quint64 tag)  noexcept {
		const quint64 vpnMask = 0x0000FFFFFFFFE000ULL;  // Bits [47:13] only
		return tag & vpnMask;
	}

	/**
	 * @brief Extract ASN from TLB tag
	 * @param tag TLB_TAG value (from DTB_TAG or ITB_TAG IPR)
	 * @return Address Space Number (bits 12:5)
	 */
	static AXP_HOT AXP_ALWAYS_INLINE ASNType extractASNFromTLBTag(quint64 tag)  noexcept {
		return (tag >> 5) & 0xFF;  // Bits [12:5]
	}

	static AXP_HOT AXP_ALWAYS_INLINE  PFNType extractPFNFromPTE(quint64 pteRaw)  noexcept
	{
		// Canonical Alpha memory PTE: PFN in bits 63..32
		// Mask to the width you actually implement (28 bits here)
		return static_cast<PFNType>((pteRaw >> 32) & ((1ULL << 28) - 1));
	}


	// ========================================================================
	// Private Helpers - IPR Field Extraction
	// ========================================================================

	static AXP_HOT AXP_ALWAYS_INLINE VAType extractVA_fromTag(quint64 tagValue) noexcept
	{
		return tagValue & ~0x1FFFULL;  // Clear lower 13 bits
	}

	static AXP_HOT AXP_ALWAYS_INLINE ASNType extractASN_fromTag(quint64 tagValue) noexcept
	{
		// TODO: Verify ASN location in your TAG format
		return static_cast<ASNType>(tagValue & 0xFF);
	}


	static AXP_HOT AXP_ALWAYS_INLINE  SC_Type extractSizeClassFromPTE(const AlphaPTE& pte)   noexcept
	{
		return pte.gh();
	}

#pragma endregion TLB Helpers


#pragma region kSeg Helper Functions


	/*
	 * GH hints in SPAM handle large pages in the virtual address space; superpages handle direct physical mappings for kernel code/data.
	 */
	// ============================================================================
	// Kseg (Kernel Superpage) Detection & Translation
	// ============================================================================
	//
	// Alpha Virtual Address Space Segments (top 2 VA bits):
	//
	//   VA[segHi:segLo] == 00    seg0   (mapped via page tables, user+kernel)
	//   VA[segHi:segLo] == 01    INVALID (access violation trap)
	//   VA[segHi:segLo] == 10    kseg   (direct physical map, kernel only)
	//   VA[segHi:segLo] == 11    seg1   (mapped via page tables, kernel only)
	//
	// Segment bit positions depend on VA size configured in I_CTL[VA_48]:
	//   I_CTL[VA_48] = 0  43-bit VA  segment bits = VA[42:41]
	//   I_CTL[VA_48] = 1  48-bit VA  segment bits = VA[47:46]
	//
	// Kseg identity-maps virtual to physical with no TLB or page walk:
	//   PA = VA[43:0]   (EV6 physical address size = 44 bits)
	//
	// Kseg is kernel-only. User-mode access to kseg  access violation.
	//
	// Reference: Alpha Architecture Reference Manual, Section 5.3.2
	//            21264/EV6 Hardware Reference Manual, Section 5.2.2
	// ============================================================================

	/// @brief Extract the 2-bit segment selector from a virtual address.
	///
	/// @param va     Full 64-bit virtual address (sign-extended)
	/// @param va_ctl Cached I_CTL register value (bit 1 = VA_48)
	/// @return Segment selector: 0=seg0, 1=invalid, 2=kseg, 3=seg1

	AXP_HOT AXP_ALWAYS_INLINE
		quint8 extractSegment(VAType va, VAType va_ctl) noexcept
	{
		// I_CTL bit 1: VA_48 mode select
		//   0  43-bit VA, segment bits at [42:41]
		//   1  48-bit VA, segment bits at [47:46]
		const int segShift = (va_ctl & 0x2) ? 46 : 41;
		return static_cast<quint8>((va >> segShift) & 0x3);
	}

	/// @brief Test whether a VA falls in kseg (kernel direct-mapped superpage).
	///
	/// @param va     Full 64-bit virtual address
	/// @param va_ctl Cached I_CTL register value
	/// @return true if VA is in kseg segment

	AXP_HOT AXP_ALWAYS_INLINE
		bool isKseg(VAType va, VAType va_ctl) noexcept
	{
		return extractSegment(va, va_ctl) == 2;   // segment 10 binary
	}

	/// @brief Convert a kseg virtual address to physical address.
	///
	/// Identity mapping: PA = VA[43:0]
	/// EV6 physical address is 44 bits wide.
	///
	/// @param va  Kseg virtual address (caller must verify isKseg first)
	/// @return Physical address (44-bit)

	AXP_HOT AXP_ALWAYS_INLINE
		PAType ksegToPhysical(VAType va) noexcept
	{
		constexpr quint64 EV6_PA_MASK = (1ULL << 44) - 1;   // 0x00000FFFFFFFFFFF
		return static_cast<PAType>(va & EV6_PA_MASK);
	}

	/// @brief Fast-path kseg translation for use in ev6TranslateFullVA.
	///
	/// Call BEFORE page table walk. If VA is in kseg and mode is kernel,
	/// returns Success with pa_out set - no TLB lookup, no page walk.
	///
	/// If VA is not kseg, returns NotKseg so caller continues to page walk.
	/// If VA is kseg but mode is not kernel, returns AccessViolation.
	///
	/// @param va      Full 64-bit virtual address
	/// @param va_ctl  Cached I_CTL register value
	/// @param mode    Current processor mode (kernel/exec/super/user)
	/// @param pa_out  [out] Physical address if kseg hit
	/// @return Success if kseg translated, NotKseg if not kseg, 
	///         AccessViolation if kseg from non-kernel mode

	AXP_HOT AXP_ALWAYS_INLINE
		TranslationResult tryKsegTranslate(
			VAType          va,
			Mode_Privilege  mode,
			bool            isIstream,
			PAType& pa_out) noexcept
	{
		// Superpages are kernel-only
		if (mode != Mode_Privilege::Kernel)
			return TranslationResult::NotKseg;

		// Select SPE source: I_CTL for fetch, M_CTL for data
		const quint8 spe = isIstream
			? m_iprGlobalMaster->x->getI_SPE()
			: m_iprGlobalMaster->x->getM_SPE();

		// SPE[2]: VA[47:46] = 2 (0b10)
		// Maps VA[43:13] -> PA[43:13], VA[45:44] ignored
		if ((spe & 0x4) && ((va >> 46) & 0x3) == 0x2)
		{
			pa_out = va & 0x00000FFFFFFFE000ULL;  // PA[43:13] = VA[43:13]
			return TranslationResult::Success;
		}

		// SPE[1]: VA[47:41] = 0x7E (1111110)
		// Maps VA[40:13] -> PA[40:13], PA[43:41] = SEXT(PA[40])
		if ((spe & 0x2) && ((va >> 41) & 0x7F) == 0x7E)
		{
			const quint64 base = va & 0x000001FFFFFFE000ULL;  // VA[40:13]
			// Sign-extend bit 40 through [43:41]
			if (base & (1ULL << 40))
				pa_out = base | 0x00000E0000000000ULL;  // set PA[43:41]
			else
				pa_out = base;
			return TranslationResult::Success;
		}

		// SPE[0]: VA[47:30] = 0x3FFFE
		// Maps VA[29:13] -> PA[29:13], PA[43:30] = 0
		if ((spe & 0x1) && ((va >> 30) & 0x3FFFF) == 0x3FFFE)
		{
			pa_out = va & 0x000000003FFFE000ULL;  // PA[29:13] = VA[29:13], upper cleared
			return TranslationResult::Success;
		}

		return TranslationResult::NotKseg;  // no superpage match
	}
#pragma endregion kSeg Helper Functions




#pragma region PalAtomics 


	// ------------------------------------------------------------------------
	// Lock striping: 4096 locks keeps contention low
	// Uses quadword-granular locking for atomicity
	// ------------------------------------------------------------------------
	static constexpr quint32 kLockStripeCount = 4096;

	struct LockStripes final
	{
		std::array<QMutex, kLockStripeCount> locks;

		AXP_HOT AXP_FLATTEN QMutex& lockForPA(quint64 pa) noexcept
		{
			// Hash: drop low 3 bits (quadword aligned), then mix
			const quint64 q = (pa >> 3);
			const quint32 idx = static_cast<quint32>(
				(q ^ (q >> 11) ^ (q >> 23)) & (kLockStripeCount - 1));
			return locks[idx];
		}
	};

	// Meyers singleton (thread-safe in C++11+)
	static AXP_HOT inline  LockStripes& globalLockStripes() noexcept
	{
		static LockStripes stripes;
		return stripes;
	}

	// ------------------------------------------------------------------------
	// GuestMemory Bridge Functions
	// ------------------------------------------------------------------------

	/**
	 * @brief Read quadword from physical address via GuestMemory
	 *
	 * GuestMemory automatically routes to RAM or MMIO as appropriate.
	 *
	 * @param guestMem GuestMemory instance
	 * @param pa Physical address (must be 8-byte aligned)
	 * @param outValue [out] Value read
	 * @return true on success, false on error
	 */
	/*AXP_FLATTEN MEM_STATUS guestMemoryReadPA_Quad(
		GuestMemory* guestMem,
		quint64 pa,
		quint64& outValue) noexcept
	{
		if (!guestMem) {
			return MEM_STATUS::OutOfRange;
		}

		// GuestMemory::readPA handles RAM vs MMIO routing
		return guestMem->readPA(pa, &outValue, sizeof(quint64));
	}*/

	/**
	 * @brief Write quadword to physical address via GuestMemory
	 *
	 * GuestMemory automatically routes to RAM or MMIO as appropriate.
	 *
	 * @param guestMem GuestMemory instance
	 * @param pa Physical address (must be 8-byte aligned)
	 * @param value Value to write
	 * @return true on success, false on error
	 */
	AXP_HOT  AXP_ALWAYS_INLINE  MEM_STATUS guestMemoryWritePA_Quad(
		quint64 pa,
		quint64 value) const noexcept
	{
		if (!m_guestMemory) {
			return MEM_STATUS::OutOfRange;
		}

		// GuestMemory::writePA handles RAM vs MMIO routing
		return m_guestMemory->write64(pa, value);
	}

	// ------------------------------------------------------------------------
	// atomicExchangePA_Quad - Main Entry Point
	// ------------------------------------------------------------------------
	/**
	 * @brief Atomic exchange at physical address
	 *
	 * Performs atomic read-modify-write:
	 *   1. Read old value from [pa]
	 *   2. Write new value to [pa]
	 *   3. Return old value
	 *
	 * Atomicity provided by lock striping across 4096 mutexes.
	 * GuestMemory handles RAM vs MMIO routing transparently.
	 *
	 * @param guestMem GuestMemory instance
	 * @param pa Physical address (must be 8-byte aligned)
	 * @param newValue Value to write
	 * @param oldValue [out] Previous value at address
	 * @return true on success, false on alignment error or bus error
	 *
	 * Thread-safe: uses lock striping to prevent concurrent access to same PA.
	 */
	AXP_HOT AXP_ALWAYS_INLINE bool atomicExchangePA_Quad(
		quint64 pa,
		quint64 newValue,
		quint64& oldValue) const noexcept
	{
		if ((pa & 0x7ULL) != 0 || !m_guestMemory)
			return false;

		QMutexLocker locker(&globalLockStripes().lockForPA(pa));

		quint64 tmpOld = 0;
		if (m_guestMemory->read64(pa, tmpOld) != MEM_STATUS::Ok)
			return false;

		if (m_guestMemory->write64(pa, newValue) != MEM_STATUS::Ok)
			return false;

		oldValue = tmpOld;
		return true;
	}

#pragma endregion PalAtomics
};

#endif
