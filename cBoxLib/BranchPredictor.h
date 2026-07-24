// ============================================================================
// BranchPredictor.h - Branch Prediction Logic
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// 
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic) / ChatGPT (OpenAI)
// 
// Description:
//   Branch prediction using 2-bit saturating counters in a 2-way set
//   associative branch history table. Supports multiple prediction strategies.
//
// Commercial use prohibited without separate license.
// Contact: peert@envysys.com | https://envysys.com
// Documentation: https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================

#ifndef BRANCHPREDICTOR_H
#define BRANCHPREDICTOR_H

#include <QtGlobal>

#include "coreLib/AlphaISA.h"
#include "coreLib/Axp_Attributes_core.h"
// ============================================================================
// Branch Prediction Strategy (ICCSR selectable)
// ============================================================================

enum class BranchStrategy : quint8
{
    NeverTaken = 0,           // Branch never taken
    DisplacementBased = 1,    // Based on sign of displacement
    HistoryTable = 2          // Use branch history table (2-bit saturating)
};

// ============================================================================
// Branch History Table Entry
// ============================================================================

struct BranchHistoryEntry
{
    quint64 pc;               // Branch PC
    quint8 predictor;         // 2-bit saturating counter (0-3)
    quint64 targetPC;         // Last known target
    bool valid;

    // 2-bit saturating counter states:
    // 0 = Strongly Not Taken
    // 1 = Weakly Not Taken
    // 2 = Weakly Taken
    // 3 = Strongly Taken

    inline bool predict() const noexcept {
        return predictor >= 2;  // Taken if >= 2
    }

    inline void update(bool taken) noexcept {
        if (taken) {
            if (predictor < 3) predictor++;
        }
        else {
            if (predictor > 0) predictor--;
        }
    }
};

// ============================================================================
// Branch Predictor
// ============================================================================

class BranchPredictor
{
public:
    static constexpr int BHT_SIZE = 512;
    static constexpr int BHT_WAYS = 2;

    // ====================================================================
    // Constructor
    // ====================================================================

    BranchPredictor() noexcept {
        // Initialize branch history table - start weakly not taken
        for (int i = 0; i < BHT_SIZE; ++i) {
            for (int j = 0; j < BHT_WAYS; ++j) {
                m_branchHistoryTable[i][j] = { 0, 1, 0, false };
            }
        }
    }

    // ====================================================================
    // Strategy Control
    // ====================================================================

    inline void setStrategy(BranchStrategy strategy) noexcept {
        m_strategy = strategy;
    }

    inline BranchStrategy getStrategy() const noexcept {
        return m_strategy;
    }

    // ====================================================================
    // Branch Prediction
    // ====================================================================

   AXP_HOT AXP_ALWAYS_INLINE bool predict(quint64 pc, quint64 rawDisp21) const noexcept
    {
        switch (m_strategy)
        {
        case BranchStrategy::NeverTaken:
            return false;

        case BranchStrategy::DisplacementBased:
            // Backward branches (negative displacement) predicted taken.
            // SEXT gives the signed value  no raw sign-bit hack needed.
            return AlphaISA::SEXT<21>(rawDisp21) < 0;

        case BranchStrategy::HistoryTable:
        {
            const BranchHistoryEntry* entry = findBranchEntry(pc);
            if (entry != nullptr && entry->valid)
                return entry->predict();
            // Fall back to displacement-based when no history exists
            return AlphaISA::SEXT<21>(rawDisp21) < 0;
        }
        }
        return false;
    }

    // ====================================================================
    // Branch Resolution Update
    // ====================================================================

    inline void update(quint64 pc, bool taken, quint64 target) noexcept
    {
        if (m_strategy != BranchStrategy::HistoryTable) {
            return;  // Only history table mode needs updates
        }

        const int idx = getBHTIndex(pc);

        // Try to find existing entry
        for (int way = 0; way < BHT_WAYS; ++way) {
            auto& entry = m_branchHistoryTable[idx][way];
            if (entry.valid && entry.pc == pc) {
                entry.update(taken);
                entry.targetPC = target;
                return;
            }
        }

        // No existing entry - allocate new one (simple LRU: use way 0)
        auto& entry = m_branchHistoryTable[idx][0];
        entry.pc = pc;
        entry.predictor = taken ? 2 : 1;  // Start weakly
        entry.targetPC = target;
        entry.valid = true;
    }

    // ====================================================================
    // Target Calculation
    // ====================================================================
    // calculateBranchTarget() removed -- use AlphaISA::calculateBranchTarget()
    // directly. getPredictedTarget() delegates there for the static case.

   AXP_HOT AXP_ALWAYS_INLINE quint64 getPredictedTarget(quint64 pc, quint64 rawDisp21) const noexcept
   {
       if (m_strategy == BranchStrategy::HistoryTable) {
           const BranchHistoryEntry* entry = findBranchEntry(pc);
           if (entry != nullptr && entry->valid && entry->targetPC != 0)
               return entry->targetPC;
       }
       // Authoritative static calculation delegate to ISA utility
       return AlphaISA::calculateBranchTarget(pc, rawDisp21);
   }

    // ====================================================================
    // Statistics and Debug
    // ====================================================================

    inline int getValidEntryCount() const noexcept
    {
        int count = 0;
        for (int i = 0; i < BHT_SIZE; ++i) {
            for (int j = 0; j < BHT_WAYS; ++j) {
                if (m_branchHistoryTable[i][j].valid) {
                    count++;
                }
            }
        }
        return count;
    }

    inline void clear() noexcept
    {
        for (int i = 0; i < BHT_SIZE; ++i) {
            for (int j = 0; j < BHT_WAYS; ++j) {
                m_branchHistoryTable[i][j].valid = false;
            }
        }
    }

    inline const BranchHistoryEntry* getEntry(quint64 pc) const noexcept {
        return findBranchEntry(pc);
    }

private:
    // ====================================================================
    // BHT Index Calculation
    // ====================================================================

    inline int getBHTIndex(quint64 pc) const noexcept {
        return static_cast<int>((pc >> 2) & (BHT_SIZE - 1));
    }

    // ====================================================================
    // BHT Lookup
    // ====================================================================

    inline const BranchHistoryEntry* findBranchEntry(quint64 pc) const noexcept
    {
        const int idx = getBHTIndex(pc);

        for (int way = 0; way < BHT_WAYS; ++way) {
            const auto& entry = m_branchHistoryTable[idx][way];
            if (entry.valid && entry.pc == pc) {
                return &entry;
            }
        }

        return nullptr;
    }

    inline BranchHistoryEntry* findBranchEntry(quint64 pc) noexcept
    {
        const int idx = getBHTIndex(pc);

        for (int way = 0; way < BHT_WAYS; ++way) {
            auto& entry = m_branchHistoryTable[idx][way];
            if (entry.valid && entry.pc == pc) {
                return &entry;
            }
        }

        return nullptr;
    }

    // ====================================================================
    // Member Data
    // ====================================================================

    BranchStrategy m_strategy{ BranchStrategy::HistoryTable };
    BranchHistoryEntry m_branchHistoryTable[BHT_SIZE][BHT_WAYS];
};

#endif // BRANCHPREDICTOR_H
