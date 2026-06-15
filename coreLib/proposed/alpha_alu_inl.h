#ifndef _EMULATRAPPUNI_CORELIB_ALPHA_ALU_INL_H
#define _EMULATRAPPUNI_CORELIB_ALPHA_ALU_INL_H
// ============================================================================
// alpha_alu_inl.h - ============================================================================
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



#include <QtGlobal>

// ============================================================================
// alpha_alu_inl.h
// ----------------------------------------------------------------------------
// Alpha AXP Integer ALU Helpers
// Architecture-accurate overflow, carry, sign, zero, and mask logic
//
// Covers:
//   - ADDL / ADDQ (and /V variants)
//   - SUBL / SUBQ (and /V variants)
//   - 32-bit (L) and 64-bit (Q) operand rules
//   - Sign-extension rules for L and Q operations
//   - Overflow and carry determination
//   - Condition flag helpers
//   - Shift helpers (SLL, SRL, SRA)
// 
// References:
//   - Alpha Architecture Reference Manual (AARM), Integer Instructions
//   - Alpha AXP System Reference Manual (SRM)
// ============================================================================

namespace alpha_alu
{
	using u64 = quint64;
	using s64 = qint64;
	using u32 = quint32;
	using s32 = qint32;

	// ========================================================================
	// Sign Bit Constants
	// ========================================================================
	static constexpr u64 SIGNBIT_64 = 0x8000000000000000ULL;
	static constexpr u32 SIGNBIT_32 = 0x80000000U;

	// ========================================================================
	// SIGN EXTENSION HELPERS
	// ========================================================================
	inline s64 sext32(u32 v) noexcept { return static_cast<s64>(static_cast<s32>(v)); }
	inline s64 sext32(u64 v) noexcept { return static_cast<s64>(static_cast<s32>(v & 0xFFFFFFFFULL)); }



	// For SUBL/ADDL, Alpha uses *32-bit operands* with sign extension.
	inline s64 asL(u64 v) noexcept { return sext32(v); }

	// For SUBQ/ADDQ, Alpha uses *full 64-bit operands*
	inline s64 asQ(u64 v) noexcept { return static_cast<s64>(v); }


	// ========================================================================
	// OVERFLOW CHECKING
	// Same for ADDL/ADDQ except operand width difference.
	// ========================================================================
	inline bool addOverflow(s64 a, s64 b, s64 r) noexcept
	{
		// Overflow when:
		//    adding two positives yields negative
		//    adding two negatives yields positive
		return ((a >= 0 && b >= 0 && r < 0) ||
			(a < 0 && b < 0 && r >= 0));
	}

	// Compact bit-method:
	inline bool addOverflow_fast(u64 a, u64 b, u64 r) noexcept
	{
		return (((a ^ r) & (b ^ r) & SIGNBIT_64) != 0);
	}

	// ========================================================================
	// SUBTRACTION OVERFLOW
	// Same AARM semantics:
	//     overflow = ((a >= 0 && b < 0 && r < 0) ||
	//                 (a <  0 && b >= 0 && r >= 0));
	// But easier: convert a - b to a + (-b)
	// ========================================================================
	inline bool subOverflow(s64 a, s64 b, s64 r) noexcept
	{
		// Same logic as add overflow using a + (-b) = r
		return addOverflow(a, -b, r);
	}

	// ========================================================================
	// UNSIGNED CARRY-OUT
	// Used by some integer ops (CMPLx)
	// ========================================================================
	inline bool addCarry(u64 a, u64 b, u64 r) noexcept
	{
		return (r < a) || (r < b);
	}

	inline bool subBorrow(u64 a, u64 b, u64 r) noexcept
	{
		return a < b;
	}

	// ========================================================================
	// RESULT WIDTH NORMALIZATION (32-bit vs 64-bit)
	// ========================================================================
	inline u64 resultL(s64 r) noexcept
	{
		// ADDL/SUBL produce sign-extended 32-bit result
		return static_cast<u64>(static_cast<s32>(r));
	}

	inline u64 resultQ(s64 r) noexcept
	{
		// ADDQ/SUBQ produce full 64-bit result
		return static_cast<u64>(r);
	}

	// ========================================================================
	// ZERO / NEGATIVE / SIGNBIT
	// ========================================================================
	inline bool isZeroL(u64 v) noexcept { return (static_cast<u32>(v) == 0); }
	inline bool isZeroQ(u64 v) noexcept { return (v == 0); }

	inline bool isNegL(u64 v) noexcept { return (static_cast<s32>(v) < 0); }
	inline bool isNegQ(u64 v) noexcept { return (static_cast<s64>(v) < 0); }

	inline bool signBitL(u64 v) noexcept { return (v & SIGNBIT_32) != 0; }
	inline bool signBitQ(u64 v) noexcept { return (v & SIGNBIT_64) != 0; }

	// ========================================================================
	// COMPARISONS (Arch-accurate)
	// ========================================================================
	inline bool cmpEqL(u64 a, u64 b) noexcept { return static_cast<s32>(a) == static_cast<s32>(b); }
	inline bool cmpEqQ(u64 a, u64 b) noexcept { return static_cast<s64>(a) == static_cast<s64>(b); }

	inline bool cmpLtL(u64 a, u64 b) noexcept { return static_cast<s32>(a) < static_cast<s32>(b); }
	inline bool cmpLtQ(u64 a, u64 b) noexcept { return static_cast<s64>(a) < static_cast<s64>(b); }

	inline bool cmpLeL(u64 a, u64 b) noexcept { return static_cast<s32>(a) <= static_cast<s32>(b); }
	inline bool cmpLeQ(u64 a, u64 b) noexcept { return static_cast<s64>(a) <= static_cast<s64>(b); }

	// ========================================================================
	// SHIFT HELPERS: SLL, SRL, SRA (Alpha rules)
	// Count uses low 6 bits of shift amount (0..63)
	// ========================================================================
	AXP_HOT AXP_ALWAYS_INLINE u64 sll(u64 v, u64 count) noexcept
	{
		return v << (count & 0x3F);
	}

	AXP_HOT AXP_ALWAYS_INLINE u64 srl(u64 v, u64 count) noexcept
	{
		return v >> (count & 0x3F);
	}

	AXP_HOT AXP_ALWAYS_INLINE u64 sra(u64 v, u64 count) noexcept
	{
		return static_cast<u64>(static_cast<s64>(v) >> (count & 0x3F));
	}

	// ========================================================================
	// BITWISE LOGICALS
	// ========================================================================
	inline u64 logicalAnd(u64 a, u64 b) noexcept { return a & b; }
	inline u64 logicalOr(u64 a, u64 b) noexcept { return a | b; }
	inline u64 logicalXor(u64 a, u64 b) noexcept { return a ^ b; }
	inline u64 logicalNand(u64 a, u64 b) noexcept { return ~(a & b); }
	inline u64 logicalNor(u64 a, u64 b) noexcept { return ~(a | b); }

	// ========================================================================
	// CMOV (Conditional Move)
	// Alpha rule: condition computed using signed compare for CMOVxx
	// ========================================================================
	inline u64 cmov(bool cond, u64 src, u64 oldValue) noexcept
	{
		return cond ? src : oldValue;
	}

} // namespace alpha_alu



#endif
