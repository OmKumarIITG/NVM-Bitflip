#ifndef __NEUROHAMMER_H__
#define __NEUROHAMMER_H__

#include "src/FaultModel.h"
#include "src/AddressTranslator.h"
#include "Simulators/gem5/nvmain_mem.hh"
#include <map>
#include <set>
#include <random>
#include <unordered_map>

namespace NVM {

class NeuroHammer : public FaultModel
{
  public:
    // Singleton pattern
    static NeuroHammer* GetInstance();
    static void DestroyInstance();
    
    void SetConfig(Config *config, bool createChildren = true);

    void SetTranslator(AddressTranslator *trans);
    
    bool InjectFault(NVMainRequest *request);
    
    void RegisterStats();

  private:
    // Private constructor for singleton
    NeuroHammer();
    ~NeuroHammer();
    
    // Delete copy constructor and assignment operator
    NeuroHammer(const NeuroHammer&) = delete;
    NeuroHammer& operator=(const NeuroHammer&) = delete;
    
    // Static instance pointer
    static NeuroHammer* instance;
    
    // Track hammer count for each row
    std::map<uint64_t, uint64_t> hammerCount;
    
    // Track flipped quadwords to avoid re-flipping
    std::set<uint64_t> flippedQuadwords;
    
    // Cache for random probabilities for a particular address
    std::unordered_map<uint64_t, double> probabilities;
    
    // Random number generators
    std::mt19937_64 rng;
    std::mt19937_64 paraRng;
    
    // Statistics
    ncounter_t totalBitFlips;
    ncounter_t rowsAffected;
    ncounter_t totalHammerCount;
    
    // Helper methods
    double GenerateProbability(uint64_t addr);
    void ProcessNeuroHammer(uint64_t subarray, uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row, bool bufferHit,uint64_t addressFixUp,uint64_t rowSizeBytes);
    uint64_t GetPhysicalAddress(uint64_t subarray, uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row, uint64_t col);

    AddressTranslator *translator;  // Address translator from NVMain
};

}

#endif