#include "FaultModels/NeuroHammer/NeuroHammer.h"
#include "include/NVMHelpers.h"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/core.hh"
#include "Simulators/gem5/nvmain_mem.hh"

#include "base/trace.hh"
#include "debug/NeuroHammer.hh"
#include "debug/NeuroHammer_GetPhysicalAddress.hh"
#include "debug/NeuroHammer_BitFlip.hh"

#include <algorithm>
#include <cassert>
#include <cmath>

using namespace NVM;
using namespace gem5;
using namespace gem5::memory;

// Static instance pointer initialization
NeuroHammer* NeuroHammer::instance = nullptr;

/**
 * @brief Get the singleton instance of the NeuroHammer model.
 */
NeuroHammer* NeuroHammer::GetInstance()
{
    if (instance == nullptr) {
        instance = new NeuroHammer();
        std::cout << "NeuroHammer: Singleton instance created" << std::endl;
    }
    return instance;
}

/**
 * @brief Destroy the singleton instance.
 */
void NeuroHammer::DestroyInstance()
{
    if (instance != nullptr) {
        delete instance;
        instance = nullptr;
        std::cout << "NeuroHammer: Singleton instance destroyed" << std::endl;
    }
}

/**
 * @brief Constructor for NeuroHammer. Initializes state and RNGs.
 */
NeuroHammer::NeuroHammer()
{
    std::cout<<"NeuroHammer Constructor Called"<<std::endl;
    
    totalBitFlips = 0;
    
    // Initialize random number generators
    std::random_device rd;
    rng = std::mt19937_64(rd());
    
    translator = NULL;
}

/**
 * @brief Destructor for NeuroHammer.
 */
NeuroHammer::~NeuroHammer()
{
    std::cout << "NeuroHammer Destructor Called" << std::endl;
}

/**
 * @brief Configures the NeuroHammer model from a config file.
 */
void NeuroHammer::SetConfig(Config *config, bool createChildren)
{
    FaultModel::SetConfig(config, createChildren);
    
    Params *params = new Params();
    params->SetParams(config);  
    SetParams(params);

    SetDebugName("NeuroHammer", config);
    
    // -------------------- DEBUG: Print NeuroHammer Parameters --------------------
    DPRINTF(NeuroHammer, "DEBUG: NeuroHammer parameters from config:\n");

    // Hammer count thresholds (common for read/write)
    DPRINTF(NeuroHammer, "  hc_first: %f  // First bit flip expected\n", config->GetEnergy("hc_first"));
    DPRINTF(NeuroHammer, "  hc_last: %f  // No new flips beyond this\n", config->GetEnergy("hc_last"));
    DPRINTF(NeuroHammer, "  hc_last_bitflip_rate: %f  // Flip probability at HC_last\n", config->GetEnergy("hc_last_bitflip_rate"));

    // -------------------- Write Distance-Dependent Increment --------------------
    DPRINTF(NeuroHammer, "  inc_write: %f  // Increment factor for write aggressors\n", config->GetEnergy("inc_write"));

    // Bit flip probabilities per quadword (common for read/write)
    for (int i = 1; i <= 4; i++) {
        DPRINTF(NeuroHammer, "  proba_%d_bit_flipped: %f  // Probability of %d bit(s) flipping\n",i, config->GetEnergy("proba_" + std::to_string(i) + "_bit_flipped"), i);
    }

    // Flip mask (common for read/write, only if exists)
    if (config->KeyExists("flip_mask")) {
        uint64_t flip_mask = config->GetValueUL("flip_mask");
        DPRINTF(NeuroHammer, "  flip_mask: 0x%llx  // Static forced bit flip mask\n", flip_mask);
    } else {
        DPRINTF(NeuroHammer, "  flip_mask: does not exist\n");
    }

    // -------------------- Hammer count decay time constant --------------------
    DPRINTF(NeuroHammer, "  hammer_count_decay_constant: %f  // Hammer count decay time constant\n", config->GetEnergy("hammer_count_decay_constant"));
}

/**
 * @brief Sets the address translator for the model.
 */
void NeuroHammer::SetTranslator(AddressTranslator *trans)
{
    translator = trans;
}

/**
 * @brief Generates a deterministic probability for a given address.
 *
 * Caches the result to ensure that the same address always yields the same
 * probability, which is crucial for reproducible simulations.
 */
double NeuroHammer::GenerateProbability(uint64_t addr)
{
    auto pp = probabilities.find(addr);
    if (pp == probabilities.end()) {
        std::mt19937_64 gen(addr);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        probabilities[addr] = dist(gen);
        return probabilities[addr];
    } else {
        return pp->second;
    }
}

/**
 * @brief Computes the exponentially decayed hammer count based on elapsed ticks.
 */
double NeuroHammer::computeDecayedHammerCount(double currentHammerCount, Tick deltaTicks){
    if (deltaTicks == 0){
        return currentHammerCount;
    }

    // const double dt_seconds = static_cast<double>(deltaTicks) / getClockFrequency();

    // double decayFactor = std::exp(-dt_seconds / p->hammer_count_decay_constant);
    // double decayedHammerCount = currentHammerCount * decayFactor;
    // return decayedHammerCount;
    return currentHammerCount;
}

/**
 * @brief Entry point for fault injection, called on each memory request.
 */
bool NeuroHammer::InjectFault(NVMainRequest *request)
{
    assert(translator != nullptr && "Address translator is not set!");

    // Check if this is a write request
    bool isWrite = (request->type == WRITE || request->type == WRITE_PRECHARGE);
    
    // Only process write requests
    if (!isWrite) {
        return false;
    }

    DPRINTF(NeuroHammer, "[InjectFault] Processing write request for address 0x%x\n",request->address.GetPhysicalAddress());

    // Extract address components from the request
    uint64_t row, col, bank, rank, channel, subarray;
    request->address.GetTranslatedAddress(&row, &col, &bank, &rank, &channel, &subarray);
    
    // Calculate the row size in bytes using NVMain's official formula (from MemoryController.cpp)
    //
    // Formula explanation:
    //   cell word size (bytes) = device_width * tBURST * RATE * number_of_devices / 8
    //
    // Where:
    //   - device_width: width of a single DRAM device in bits
    //   - tBURST: minimum burst length
    //   - RATE: data rate multiplier
    //   - number_of_devices = bus_width / device_width
    //
    // Simplified form:
    //   cell word size (bytes) = BusWidth * tBURST * RATE / 8
    //
    // This gives the total size of a row in bytes.

    uint64_t cellSizeBytes = (p->BusWidth * p->tBURST * p->RATE) / 8;
    
    uint64_t cellAddress = GetPhysicalAddress(subarray,channel, rank, bank, row, col);

    for(uint64_t quadAddr = cellAddress; quadAddr<cellAddress+cellSizeBytes; quadAddr+=sizeof(uint64_t)){
        if(hammerState.find(quadAddr) != hammerState.end()){
            hammerState.erase(quadAddr);
        }
    }

    uint64_t hammerIncrement = p->inc_write;
    uint64_t addressFixUp = request->addressFixUp;

    if(row + 1 < p->ROWS){
        uint64_t cellAddressDown = GetPhysicalAddress(subarray,channel, rank, bank, row + 1, col);
        for(uint64_t quadAddr = cellAddressDown; quadAddr<cellAddressDown+cellSizeBytes; quadAddr+=sizeof(uint64_t)){
            if(hammerState.find(quadAddr) == hammerState.end()){
                hammerState[quadAddr] = HammerInfo();
            }
            if(hammerState[quadAddr].lastTimeFullCellHammered == 0) hammerState[quadAddr].lastTimeFullCellHammered = curTick();
            if(hammerState[quadAddr].lastTimeLeftMostBitHammered == 0)hammerState[quadAddr].lastTimeLeftMostBitHammered = curTick();
            if(hammerState[quadAddr].lastTimeRightMostBitHammered == 0)hammerState[quadAddr].lastTimeRightMostBitHammered = curTick();
            hammerState[quadAddr].fullCellDisturbanceCount=computeDecayedHammerCount(
                hammerState[quadAddr].fullCellDisturbanceCount,
                curTick() - hammerState[quadAddr].lastTimeFullCellHammered
            ) + hammerIncrement;
            hammerState[quadAddr].lastTimeFullCellHammered = curTick();

            hammerState[quadAddr].leftmostBitDisturbanceCount=computeDecayedHammerCount(
                hammerState[quadAddr].leftmostBitDisturbanceCount,
                curTick() - hammerState[quadAddr].lastTimeLeftMostBitHammered
            ) + hammerIncrement;
            hammerState[quadAddr].lastTimeLeftMostBitHammered = curTick();

            hammerState[quadAddr].rightmostBitDisturbanceCount=computeDecayedHammerCount(
                hammerState[quadAddr].rightmostBitDisturbanceCount,
                curTick() - hammerState[quadAddr].lastTimeRightMostBitHammered
            ) + hammerIncrement;
            hammerState[quadAddr].lastTimeRightMostBitHammered = curTick();

            DPRINTF(NeuroHammer,"Hammer count: %llu\n",hammerState[quadAddr].fullCellDisturbanceCount);
            if(!hammerState[quadAddr].fullCellFlipped && hammerState[quadAddr].fullCellDisturbanceCount >= p->HC_first){        
                ProcessBitflipInQuadword(quadAddr,addressFixUp);
            }
        }
    }
    if(row > 0){
        uint64_t cellAddressUp = GetPhysicalAddress(subarray,channel, rank, bank, row - 1, col);
        for(uint64_t quadAddr = cellAddressUp; quadAddr<cellAddressUp+cellSizeBytes; quadAddr+=sizeof(uint64_t)){
            if(hammerState.find(quadAddr) == hammerState.end()){
                hammerState[quadAddr] = HammerInfo();
            }
            if(hammerState[quadAddr].lastTimeFullCellHammered == 0) hammerState[quadAddr].lastTimeFullCellHammered = curTick();
            if(hammerState[quadAddr].lastTimeLeftMostBitHammered == 0)hammerState[quadAddr].lastTimeLeftMostBitHammered = curTick();
            if(hammerState[quadAddr].lastTimeRightMostBitHammered == 0)hammerState[quadAddr].lastTimeRightMostBitHammered = curTick();
            hammerState[quadAddr].fullCellDisturbanceCount=computeDecayedHammerCount(
                hammerState[quadAddr].fullCellDisturbanceCount,
                curTick() - hammerState[quadAddr].lastTimeFullCellHammered
            ) + hammerIncrement;
            hammerState[quadAddr].lastTimeFullCellHammered = curTick();

            hammerState[quadAddr].leftmostBitDisturbanceCount=computeDecayedHammerCount(
                hammerState[quadAddr].leftmostBitDisturbanceCount,
                curTick() - hammerState[quadAddr].lastTimeLeftMostBitHammered
            ) + hammerIncrement;
            hammerState[quadAddr].lastTimeLeftMostBitHammered = curTick();

            hammerState[quadAddr].rightmostBitDisturbanceCount=computeDecayedHammerCount(
                hammerState[quadAddr].rightmostBitDisturbanceCount,
                curTick() - hammerState[quadAddr].lastTimeRightMostBitHammered
            ) + hammerIncrement;
            hammerState[quadAddr].lastTimeRightMostBitHammered = curTick();

            if(!hammerState[quadAddr].fullCellFlipped && hammerState[quadAddr].fullCellDisturbanceCount >= p->HC_first){        
                ProcessBitflipInQuadword(quadAddr,addressFixUp);
            }
        }
    }    
    if(col > 0){
        uint64_t cellAddressLeft = GetPhysicalAddress(subarray,channel, rank, bank, row, col - 1);
        uint64_t totalQuadwords =  cellSizeBytes/sizeof(uint64_t);
        uint64_t cellAddressRightMostQuadwordOfLeft = cellAddressLeft + (totalQuadwords-1)*sizeof(uint64_t);

        if(hammerState.find(cellAddressRightMostQuadwordOfLeft) == hammerState.end()){
            hammerState[cellAddressRightMostQuadwordOfLeft] = HammerInfo();
        }
        if(hammerState[cellAddressRightMostQuadwordOfLeft].lastTimeRightMostBitHammered == 0)hammerState[cellAddressRightMostQuadwordOfLeft].lastTimeRightMostBitHammered = curTick();
        hammerState[cellAddressRightMostQuadwordOfLeft].rightmostBitDisturbanceCount=computeDecayedHammerCount(
            hammerState[cellAddressRightMostQuadwordOfLeft].rightmostBitDisturbanceCount,
            curTick() - hammerState[cellAddressRightMostQuadwordOfLeft].lastTimeRightMostBitHammered
        ) + hammerIncrement;
        hammerState[cellAddressRightMostQuadwordOfLeft].lastTimeRightMostBitHammered = curTick();

        if(!hammerState[cellAddressRightMostQuadwordOfLeft].rightmostBitFlipped && hammerState[cellAddressRightMostQuadwordOfLeft].rightmostBitDisturbanceCount >= p->HC_first){
            ProcessSingleBitEdgeFlip(cellAddressRightMostQuadwordOfLeft,false,addressFixUp);
        }
    }
    if(col + 1 < p->COLS){
        uint64_t cellAddressRight = GetPhysicalAddress(subarray,channel, rank, bank, row, col + 1);
        uint64_t cellAddressLeftMostQuadwordOfRight = cellAddressRight;

        if(hammerState.find(cellAddressLeftMostQuadwordOfRight) == hammerState.end()){
            hammerState[cellAddressLeftMostQuadwordOfRight] = HammerInfo();
        }
        if(hammerState[cellAddressLeftMostQuadwordOfRight].lastTimeLeftMostBitHammered == 0)hammerState[cellAddressLeftMostQuadwordOfRight].lastTimeLeftMostBitHammered = curTick();
        hammerState[cellAddressLeftMostQuadwordOfRight].leftmostBitDisturbanceCount=computeDecayedHammerCount(
            hammerState[cellAddressLeftMostQuadwordOfRight].leftmostBitDisturbanceCount,
            curTick() - hammerState[cellAddressLeftMostQuadwordOfRight].lastTimeLeftMostBitHammered
        ) + hammerIncrement;
        hammerState[cellAddressLeftMostQuadwordOfRight].lastTimeLeftMostBitHammered = curTick();

        if(!hammerState[cellAddressLeftMostQuadwordOfRight].leftmostBitFlipped && hammerState[cellAddressLeftMostQuadwordOfRight].leftmostBitDisturbanceCount >= p->HC_first){
            ProcessSingleBitEdgeFlip(cellAddressLeftMostQuadwordOfRight,true,addressFixUp);
        }
    }

    return true;
}

void NeuroHammer::ProcessBitflipInQuadword(uint64_t quadAddr,uint64_t addressFixUp){
    // Calculate bit flip probability based on hammer count
    double progress = std::min(
        static_cast<double>(hammerState[quadAddr].fullCellDisturbanceCount - p->HC_first) / 
        static_cast<double>(p->HC_last - p->HC_first), 
        1.0
    );
    
    double quadwordFlipRate = p->HC_last_bitflip_rate * progress * 64;
    
    DPRINTF(NeuroHammer,"Quadword flip rate: %f And hammer count: %llu\n", quadwordFlipRate,hammerState[quadAddr].fullCellDisturbanceCount);

    // Probabilistically flip quadword
    if (GenerateProbability(quadAddr) > quadwordFlipRate) {
        // No flip
        return;
    }

    hammerState[quadAddr].fullCellFlipped = true;
    
    DPRINTF(NeuroHammer,"Generating bit flip mask\n");
    // Generate bit flip mask
    uint64_t mask = 0;
    uint64_t gem5Addr = quadAddr + addressFixUp;
    uint64_t* hostAddr = (uint64_t*)NVMainMemory::masterInstance->toHostAddr(gem5Addr);
    uint64_t oldData = *hostAddr;
    if (p->flip_mask) {
        // Optional: apply external mask, but enforce 0->1 only
        mask = p->flip_mask & ~oldData; // Mask out any bits already 1
    } else {
        // Generate random mask based on bit flip probabilities
        std::mt19937_64 gen(quadAddr ^ 0xcafecafecafecafe);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double flippedBitsRan = dist(gen);

        int flippedBits;
        if (flippedBitsRan <= p->proba_1_bit_flipped) {
            flippedBits = 1;
        } else if (flippedBitsRan <= p->proba_1_bit_flipped + p->proba_2_bit_flipped) {
            flippedBits = 2;
        } else if (flippedBitsRan <= p->proba_1_bit_flipped + p->proba_2_bit_flipped + p->proba_3_bit_flipped) {
            flippedBits = 3;
        } else {
            flippedBits = 4;
        }

        // Find all 0-bit positions in oldData
        std::vector<int> zeroBits;
        for (int b = 0; b < 64; ++b) {
            if (((oldData >> b) & 1) == 0) {
                zeroBits.push_back(b);
            }
        }

        // If no zero bits are available, skip
        if (zeroBits.empty()) {
            DPRINTF(NeuroHammer_BitFlip, "No 0-bits to flip at Addr: 0x%x\n", quadAddr);
            return;
        }

        // Shuffle zero-bit positions and pick up to flippedBits
        std::shuffle(zeroBits.begin(), zeroBits.end(), gen);
        int numToFlip = std::min(flippedBits, (int)zeroBits.size());
        for (int i = 0; i < numToFlip; ++i) {
            mask |= ((uint64_t)1 << zeroBits[i]);
        }
    }
    *hostAddr ^= mask;
    totalBitFlips += __builtin_popcountll(mask);

    DPRINTF(NeuroHammer_BitFlip,
            "BIT FLIP! Addr: 0x%x, Mask: 0x%x, Old Data: 0x%x, New Data: 0x%x\n",
            quadAddr, mask, oldData, *hostAddr);
}

void NeuroHammer::ProcessSingleBitEdgeFlip(uint64_t quadAddr, bool flipLeft,uint64_t addressFixUp){
    // Calculate bit flip probability based on hammer count
    uint64_t currentHammerCount = flipLeft ? hammerState[quadAddr].leftmostBitDisturbanceCount : hammerState[quadAddr].rightmostBitDisturbanceCount;
    double progress = std::min(
        static_cast<double>(currentHammerCount - p->HC_first) / 
        static_cast<double>(p->HC_last - p->HC_first), 
        1.0
    );
    
    double currentBitFlipRate = p->HC_last_bitflip_rate * progress * 1;
    
    DPRINTF(NeuroHammer,"Bit flip rate: %f And hammer count: %llu\n", currentBitFlipRate,currentHammerCount);

    // Probabilistically flip quadword
    if (GenerateProbability(quadAddr) > currentBitFlipRate) {
        // No flip
        return;
    }

    if(flipLeft){
        hammerState[quadAddr].leftmostBitFlipped = true;
    }else{
        hammerState[quadAddr].rightmostBitFlipped = true;
    }
    
    DPRINTF(NeuroHammer,"Generating bit flip mask\n");
    // Generate bit flip mask
    uint64_t mask = 0;
    uint64_t gem5Addr = quadAddr + addressFixUp;
    uint64_t* hostAddr = (uint64_t*)NVMainMemory::masterInstance->toHostAddr(gem5Addr);
    uint64_t oldData = *hostAddr;
    // Decide which bit to flip
    int bitIndex = flipLeft ? 63 : 0;
    // Check if this bit is currently 0 (only 0 -> 1 allowed)
    bool bitIsZero = (((oldData >> bitIndex) & 1ULL) == 0ULL);
    if (bitIsZero)
    {
        // Create mask with single bit at bitIndex
        mask = (1ULL << bitIndex);
    }
    else
    {
        // The bit is already 1 → cannot flip again; mask stays 0
        mask = 0;
    }
    *hostAddr ^= mask;
    totalBitFlips += __builtin_popcountll(mask);

    DPRINTF(NeuroHammer_BitFlip,
            "BIT FLIP! Addr: 0x%x, Mask: 0x%x, Old Data: 0x%x, New Data: 0x%x\n",
            quadAddr, mask, oldData, *hostAddr);
}

/**
 * @brief Converts a translated address back to a physical address.
 *
 * Uses the NVMain translator if available, otherwise falls back to a simple
 * default mapping scheme.
 */
uint64_t NeuroHammer::GetPhysicalAddress(uint64_t subarray,uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row, uint64_t col)
{
    if (translator != NULL && translator->GetTranslationMethod() != NULL) {
        // Use the NVMain address translator
        uint64_t physAddr = translator->ReverseTranslate(row, col, bank, rank, channel, subarray);
        
        // For debugging: print mapping from NVMain translation
        DPRINTF(NeuroHammer_GetPhysicalAddress, "GetPhysicalAddress using NVMain translator:\n");
        DPRINTF(NeuroHammer_GetPhysicalAddress, "  Channel: %d, Rank: %d, Bank: %d, Row: %d, Col: %d\n",channel, rank, bank, row, col);
        DPRINTF(NeuroHammer_GetPhysicalAddress, "  Mapped to physical address: 0x%llx\n", physAddr);
        
        return physAddr;
    } else {
        // Fallback to our simplified mapping if translator isn't available
        DPRINTF(NeuroHammer_GetPhysicalAddress, "WARNING: Using fallback address mapping (translator not available)\n");
     
        // Simple address mapping scheme
        uint64_t colBits = 9;   // Typically 8-10 bits for column
        uint64_t rowBits = 13;  // Typically 14-16 bits for row
        uint64_t bankBits = 2;  // Typically 2-3 bits for bank
        uint64_t rankBits = 0;  // Typically 0-1 bits for rank
        uint64_t chBits = 2;    // Typically 1-2 bit for channel
        
        uint64_t physAddr = 0;
        physAddr |= col;
        physAddr |= (row << colBits);
        physAddr |= (bank << (colBits + rowBits));
        physAddr |= (rank << (colBits + rowBits + bankBits));
        physAddr |= (channel << (colBits + rowBits + bankBits + rankBits));
        
        DPRINTF(NeuroHammer_GetPhysicalAddress,"  Channel: %d, Rank: %d, Bank: %d, Row: %d, Col: %d\n",channel, rank, bank, row, col);
        DPRINTF(NeuroHammer_GetPhysicalAddress,"  Fallback mapped to physical address: 0x%llx\n", physAddr);

        return physAddr;
    }
}

/**
 * @brief Registers statistics to be tracked by NVMain.
 */
void NeuroHammer::RegisterStats()
{
    NVMObject::RegisterStats();
    AddStat(totalBitFlips);
}