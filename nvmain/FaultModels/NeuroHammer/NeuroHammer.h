#ifndef __NEUROHAMMER_H__
#define __NEUROHAMMER_H__

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
    // Tracks the accumulated hammer count for each victim row's base address.
    std::map<uint64_t, double> hammerCount;

    // Tracks physical addresses of quadwords that have already been flipped to prevent re-flipping.
    std::set<uint64_t> flippedQuadwords;

    // Caches generated probabilities for addresses to ensure deterministic behavior.
    std::unordered_map<uint64_t, double> probabilities;

    // Random Number Generation 
    std::mt19937_64 rng;

    // Statistics Counters 
    ncounter_t totalBitFlips;
    ncounter_t rowsAffected;
    ncounter_t totalHammerCount;
    
    // Helper methods
    double GenerateProbability(uint64_t addr);
    void ProcessNeuroHammer(uint64_t subarray, uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row,uint64_t addressFixUp,uint64_t rowSizeBytes, bool isReadHammering);
    uint64_t GetPhysicalAddress(uint64_t subarray, uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row, uint64_t col);

    // Pointer to the NVMain address translator.
    AddressTranslator* translator;
};

} // namespace NVM

#endif // __NEUROHAMMER_H__