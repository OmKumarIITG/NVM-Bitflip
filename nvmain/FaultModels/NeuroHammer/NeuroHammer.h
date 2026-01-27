#ifndef __NEUROHAMMER_H__
#define __NEUROHAMMER_H__

#include "base/types.hh"
#include "src/AddressTranslator.h"
#include "src/FaultModel.h"
#include "Simulators/gem5/nvmain_mem.hh"

#include <map>
#include <random>
#include <set>
#include <unordered_map>

namespace NVM {

/**
 * @class NeuroHammer
 * @brief Implements a NeuroHammer fault model for NVMain.
 *
 * This class simulates bit flips adjacent to a
 * frequently accessed row (aggressor). It uses a singleton pattern to maintain
 * a global state of hammer counts and flipped bits across the simulation.
 */
class NeuroHammer : public FaultModel {
public:
    // Singleton Management 
    static NeuroHammer* GetInstance();
    static void DestroyInstance();

    // Configuration and Initialization 
    void SetConfig(Config* config, bool createChildren = true);
    void SetTranslator(AddressTranslator* trans);

    // Core Functionality 
    bool InjectFault(NVMainRequest* request);

    // Statistics 
    void RegisterStats() override;

private:
    // Singleton Implementation 
    NeuroHammer();
    ~NeuroHammer();
    NeuroHammer(const NeuroHammer&) = delete;
    NeuroHammer& operator=(const NeuroHammer&) = delete;

    // Static instance pointer for the singleton pattern.
    static NeuroHammer* instance;

    // State Tracking 
    struct HammerInfo {
        double fullCellDisturbanceCount  = 0;   // counts full-cell disturbances (used for up/down neighbors)
        double leftmostBitDisturbanceCount  = 0;   // counts leftmost-bit disturbances (used for right neighbor)
        double rightmostBitDisturbanceCount = 0;   // counts rightmost-bit disturbances (used for left neighbor)

        bool fullCellFlipped  = false;  // flip already applied to full cell?
        bool leftmostBitFlipped  = false;  // leftmost-bit flip already applied?
        bool rightmostBitFlipped = false;  // rightmost-bit flip already applied?

        gem5::Tick lastTimeFullCellHammered = 0;
        gem5::Tick lastTimeLeftMostBitHammered = 0;
        gem5::Tick lastTimeRightMostBitHammered = 0;
    };

    // Tracks the disturbance counters and flip flags for a victim cell in a row. Key is the physical address of the cell.
    std::map<uint64_t, HammerInfo> hammerState;

    // Caches generated probabilities for addresses to ensure deterministic behavior.
    std::unordered_map<uint64_t, double> probabilities;

    // Random Number Generation 
    std::mt19937_64 rng;

    // Statistics Counters 
    ncounter_t totalBitFlips;
    
    // Helper methods
    double GenerateProbability(uint64_t addr);
    void ProcessBitflipInQuadword(uint64_t quadAddr,uint64_t addressFixUp);
    void ProcessSingleBitEdgeFlip(uint64_t quadAddr, bool flipLeft,uint64_t addressFixUp);
    void maskOldData(uint64_t quadAddr,uint64_t mask);
    uint64_t GetPhysicalAddress(uint64_t subarray, uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row, uint64_t col);
    double computeDecayedHammerCount(double currentHammerCount, gem5::Tick deltaTicks);

    // Pointer to the NVMain address translator.
    AddressTranslator* translator;
};

} // namespace NVM

#endif // __NEUROHAMMER_H__