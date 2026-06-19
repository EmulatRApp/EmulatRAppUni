// ============================================================================
// alpha_int_helpers_inl.h - ============================================================================
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

#ifndef ALPHA_INT_HELPERS_INL_H__
#define ALPHA_INT_HELPERS_INL_H__

#include <QtGlobal>

#include "coreLib/axp_attributes_core.h"

// ============================================================================
// Alpha_INT::Status  -  Integer overflow / error status
// ============================================================================


// ============================================================================
// IntStatus - Fixed Version
// ============================================================================

struct IntStatus {
	bool overflow;
	bool divideByZero;
	bool unalignedAccess;
	bool reservedOperand;
	bool floatingPointException;

	// Initialize ALL members
	IntStatus()
		: overflow(false)
		, divideByZero(false)
		, unalignedAccess(false)
		, reservedOperand(false)
		, floatingPointException(false)
	{
	}

	void setOverflow() { overflow = true; }
	// Individual error checks
	bool hasOverflow() const { return overflow; }
	bool hasDivideByZero() const { return divideByZero; }
	bool hasUnalignedAccess() const { return unalignedAccess; }
	bool hasReservedOperand() const { return reservedOperand; }
	bool hasFloatingPointException() const { return floatingPointException; }

	// Check ALL error conditions
	constexpr bool hasError() const noexcept {
		return overflow || divideByZero || unalignedAccess ||
			reservedOperand || floatingPointException;
	}

	// Helper: Clear all errors
	void clear() noexcept {
		overflow = false;
		divideByZero = false;
		unalignedAccess = false;
		reservedOperand = false;
		floatingPointException = false;
	}
};


	// ============================================================================
	// Alpha_INT::Core  -  Low-level integer arithmetic operations
	// Pure operations: no traps, no CPU access, no side effects.
	// ============================================================================
	/*	using INT_STATUS = Alpha_INT::Status::IntStatus;*/

	// ------------------------------------------------------------------------
	// 32-bit Signed Operations  (ADDL, SUBL, MULL)
	// ------------------------------------------------------------------------
	AXP_HOT AXP_ALWAYS_INLINE qint32 addL(qint32 a, qint32 b, IntStatus& status) noexcept {
		qint32 result = a + b;

		if ((a > 0 && b > 0 && result < 0) ||
			(a < 0 && b < 0 && result > 0)) {
			status.overflow = true;
		}
		return result;
	}

AXP_HOT AXP_ALWAYS_INLINE qint32 subL(qint32 a, qint32 b, IntStatus& status) noexcept {
		qint32 result = a - b;

		if ((a > 0 && b < 0 && result < 0) ||
			(a < 0 && b > 0 && result > 0)) {
			status.overflow = true;
		}
		return result;
	}

AXP_HOT AXP_ALWAYS_INLINE qint32 mulL(qint32 a, qint32 b, IntStatus& status) noexcept {
		qint64 wide = static_cast<qint64>(a) * static_cast<qint64>(b);
		qint32 result = static_cast<qint32>(wide);

		if (wide != static_cast<qint64>(result)) {
			status.overflow = true;
		}
		return result;
	}

	// ------------------------------------------------------------------------
	// 64-bit Signed Operations  (ADDQ, SUBQ, MULQ)
	// ------------------------------------------------------------------------
AXP_HOT AXP_ALWAYS_INLINE qint64 addQ(qint64 a, qint64 b, IntStatus& status) noexcept {
		qint64 result = a + b;

		if ((a > 0 && b > 0 && result < 0) ||
			(a < 0 && b < 0 && result > 0)) {
			status.overflow = true;
		}
		return result;
	}

AXP_HOT AXP_ALWAYS_INLINE qint64 subQ(qint64 a, qint64 b, IntStatus& status) noexcept {
		qint64 result = a - b;

		if ((a > 0 && b < 0 && result < 0) ||
			(a < 0 && b > 0 && result > 0)) {
			status.overflow = true;
		}
		return result;
	}

AXP_HOT AXP_ALWAYS_INLINE qint64 mulQ(qint64 a, qint64 b, IntStatus& status) noexcept {
		qint64 result = a * b;

		if (a != 0 && b != 0) {
			if ((a == -1 && b == std::numeric_limits<qint64>::min()) ||
				(b == -1 && a == std::numeric_limits<qint64>::min())) {
				status.overflow = true;
			}
			else if (result / a != b) {
				status.overflow = true;
			}
		}
		return result;
	}

	// ------------------------------------------------------------------------
	// Unsigned Operations
	// ------------------------------------------------------------------------
AXP_HOT AXP_ALWAYS_INLINE quint64 addQU(quint64 a, quint64 b, IntStatus& status) noexcept {
		quint64 result = a + b;
		if (result < a) status.overflow = true;
		return result;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 subQU(quint64 a, quint64 b, IntStatus& status) noexcept {
		quint64 result = a - b;
		if (a < b) status.overflow = true;
		return result;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 mulQU(quint64 a, quint64 b, IntStatus& status) noexcept {
		quint64 result = a * b;
		if (b != 0 && result / b != a) status.overflow = true;
		return result;
	}

	// ------------------------------------------------------------------------
	// Logical (no overflow)
	// ------------------------------------------------------------------------
	// Basic operations
AXP_HOT AXP_ALWAYS_INLINE quint64 andQ(quint64 a, quint64 b) noexcept { return a & b; }
AXP_HOT AXP_ALWAYS_INLINE quint64 orQ(quint64 a, quint64 b) noexcept { return a | b; }
AXP_HOT AXP_ALWAYS_INLINE quint64 xorQ(quint64 a, quint64 b) noexcept { return a ^ b; }
AXP_HOT AXP_ALWAYS_INLINE quint64 notQ(quint64 a) noexcept { return ~a; }

	// Compound operations (Alpha-specific)
AXP_HOT AXP_ALWAYS_INLINE quint64 bicQ(quint64 a, quint64 b) noexcept { return a & ~b; }  // Bit Clear: AND NOT
AXP_HOT AXP_ALWAYS_INLINE quint64 ornotQ(quint64 a, quint64 b) noexcept { return a | ~b; }  // OR NOT
AXP_HOT AXP_ALWAYS_INLINE quint64 eqvQ(quint64 a, quint64 b) noexcept { return ~(a ^ b); } // Equivalence: XORNOT

	// ------------------------------------------------------------------------
	// Comparison Operations (return 1 or 0)
	// ------------------------------------------------------------------------
	// Signed comparisons
AXP_HOT AXP_ALWAYS_INLINE quint64 cmpEQ(quint64 a, quint64 b) noexcept { return (a == b) ? 1 : 0; }
AXP_HOT AXP_ALWAYS_INLINE quint64 cmpLT(qint64 a, qint64 b) noexcept { return (a < b) ? 1 : 0; }
AXP_HOT AXP_ALWAYS_INLINE quint64 cmpLE(qint64 a, qint64 b) noexcept { return (a <= b) ? 1 : 0; }

	// Unsigned comparisons
AXP_HOT AXP_ALWAYS_INLINE quint64 cmpULT(quint64 a, quint64 b) noexcept { return (a < b) ? 1 : 0; }
AXP_HOT AXP_ALWAYS_INLINE quint64 cmpULE(quint64 a, quint64 b) noexcept { return (a <= b) ? 1 : 0; }

	// ------------------------------------------------------------------------
	// Conditional Move Operations
	// ------------------------------------------------------------------------
AXP_HOT AXP_ALWAYS_INLINE quint64 cmovEQ(quint64 src, quint64 dst, quint64 test) noexcept {
		return (test == 0) ? src : dst;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 cmovNE(quint64 src, quint64 dst, quint64 test) noexcept {
		return (test != 0) ? src : dst;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 cmovLT(quint64 src, quint64 dst, qint64 test) noexcept {
		return (test < 0) ? src : dst;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 cmovLE(quint64 src, quint64 dst, qint64 test) noexcept {
		return (test <= 0) ? src : dst;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 cmovGT(quint64 src, quint64 dst, qint64 test) noexcept {
		return (test > 0) ? src : dst;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 cmovGE(quint64 src, quint64 dst, qint64 test) noexcept {
		return (test >= 0) ? src : dst;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 cmovLBC(quint64 src, quint64 dst, quint64 test) noexcept {
		return ((test & 1) == 0) ? src : dst;  // Low Bit Clear
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 cmovLBS(quint64 src, quint64 dst, quint64 test) noexcept {
		return ((test & 1) != 0) ? src : dst;  // Low Bit Set
	}

	// ------------------------------------------------------------------------
	// Shift Operations
	// ------------------------------------------------------------------------
AXP_HOT AXP_ALWAYS_INLINE quint64 sllQ(quint64 v, int s) noexcept {
		if (s >= 64) return 0;
		if (s < 0) return v;
		return v << s;
	}

AXP_HOT AXP_ALWAYS_INLINE quint64 srlQ(quint64 v, int s) noexcept {
		if (s >= 64) return 0;
		if (s < 0) return v;
		return v >> s;
	}

AXP_HOT AXP_ALWAYS_INLINE qint64 sraQ(qint64 v, int s) noexcept {
		if (s >= 64) return (v < 0) ? -1 : 0;
		if (s < 0) return v;
		return v >> s;
	}

	// ------------------------------------------------------------------------
	// UMULH - Unsigned Multiply High (returns upper 64 bits)
	// ------------------------------------------------------------------------
AXP_HOT AXP_ALWAYS_INLINE quint64 umulh(quint64 a, quint64 b, IntStatus& status) noexcept {
		Q_UNUSED(status);  // UMULH doesn't set overflow on Alpha

#ifdef __SIZEOF_INT128__
		// Use 128-bit arithmetic if available
		__uint128_t wide = static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b);
		return static_cast<quint64>(wide >> 64);
#elif defined(_MSC_VER)
		// MSVC: use intrinsic
#include <intrin.h>
		quint64 hi = 0;
		_umul128(a, b, &hi);
		return hi;
#else
		// Portable fallback using 32x32->64 multiplication
		// Split into high/low 32-bit parts
		quint64 a_lo = a & 0xFFFFFFFF;
		quint64 a_hi = a >> 32;
		quint64 b_lo = b & 0xFFFFFFFF;
		quint64 b_hi = b >> 32;

		// Compute partial products
		quint64 p0 = a_lo * b_lo;
		quint64 p1 = a_lo * b_hi;
		quint64 p2 = a_hi * b_lo;
		quint64 p3 = a_hi * b_hi;

		// Combine (school multiplication)
		quint64 carry = ((p0 >> 32) + (p1 & 0xFFFFFFFF) + (p2 & 0xFFFFFFFF)) >> 32;
		return p3 + (p1 >> 32) + (p2 >> 32) + carry;
#endif
	}

	// Function bit 6 (0x40) enables trapping variant for opcode 0x10.
AXP_HOT AXP_ALWAYS_INLINE bool isTrappingVariant(quint8 opcode, quint16 logFunction) noexcept {
		if (opcode != 0x10)
			return false;
		constexpr quint16 TRAP_BIT = 0x40;
		return (logFunction & TRAP_BIT) != 0;
	}

	// (Optional) We can place combined dispatch functions here later.
	// For example: executeAdd(di, cpu, status), executeSub, etc.


#endif // ALPHA_INT_HELPERS_INL_H__