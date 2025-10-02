#include "FaultModels/NeuroHammer/NeuroHammer.h"
#include "include/NVMHelpers.h"
#include "mem/packet.hh"
#include "mem/request.hh"
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
    rowsAffected = 0;
    totalHammerCount = 0;
    
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

    // -------------------- Read Distance-Dependent Increments --------------------
    DPRINTF(NeuroHammer, "Read distance-dependent hammer count increments:\n");
    for (int i = 1; i <= 5; i++) {
        DPRINTF(NeuroHammer, "  inc_dist_%d_read: %f  // Increment factor for neighbor distance %d\n",i, config->GetEnergy("inc_dist_" + std::to_string(i) + "_read"), i);
    }

    // -------------------- Write Distance-Dependent Increments --------------------
    DPRINTF(NeuroHammer, "Write distance-dependent hammer count increments:\n");
    for (int i = 1; i <= 5; i++) {
        DPRINTF(NeuroHammer, "  inc_dist_%d_write: %f  // Increment factor for neighbor distance %d\n",i, config->GetEnergy("inc_dist_" + std::to_string(i) + "_write"), i);
    }

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
 * @brief Entry point for fault injection, called on each memory request.
 */
bool NeuroHammer::InjectFault(NVMainRequest *request)
{
    assert(translator != nullptr && "Address translator is not set!");

     // Only inject faults for row activations (not row buffer hits).
    if (request->isRowBufferHit) {
        return false;
    }

    // Check if this is a read or write request
    bool isRead = (request->type == READ || request->type == READ_PRECHARGE);
    bool isWrite = (request->type == WRITE || request->type == WRITE_PRECHARGE);
    
    // Only process read/write requests
    if (!isRead && !isWrite) {
        return false;
    }

    DPRINTF(NeuroHammer, "[InjectFault] Processing request for addr 0x%x\n",request->address.GetPhysicalAddress());

    // Extract address components from the request
    uint64_t row, col, bank, rank, channel, subarray;
    request->address.GetTranslatedAddress(&row, &col, &bank, &rank, &channel, &subarray);
    
    // Get base address of this row
    uint64_t baseRowAddr = GetPhysicalAddress(subarray,channel, rank, bank, row, 0);

    // Calculate the row size in bytes using NVMain's official formula (from MemoryController.cpp)
    //
    // Formula explanation:
    //   memory word size (bytes) = device_width * tBURST * RATE * number_of_devices / 8
    //
    // Where:
    //   - device_width: width of a single DRAM device in bits
    //   - tBURST: minimum burst length
    //   - RATE: data rate multiplier
    //   - number_of_devices = bus_width / device_width
    //
    // Simplified form:
    //   memory word size (bytes) = BusWidth * tBURST * RATE / 8
    //
    // This gives the total size of a row in bytes.

    uint64_t memoryWordSize = p->BusWidth * p->tBURST * p->RATE / 8;
    uint64_t rowSizeBytes = p->COLS * memoryWordSize;

    // Reset the hammer count for this row
    hammerCount.erase(baseRowAddr);
    // Clear flipped status for all quadwords in this row
    for (uint64_t quadAddr = baseRowAddr; quadAddr <baseRowAddr +  rowSizeBytes; quadAddr+=sizeof(uint64_t)) {
        flippedQuadwords.erase(quadAddr);
    }

    bool isReadHammering = false;
    if(isRead) isReadHammering = true;
    ProcessNeuroHammer(subarray,channel, rank, bank, row,request->addressFixUp,rowSizeBytes,isReadHammering);

    return true;
}

void NeuroHammer::ProcessNeuroHammer(uint64_t subarray,uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row,uint64_t addressFixUp,uint64_t rowSizeBytes, bool isReadHammering)
{

    // Process neighboring rows within distance 5
    for (int dist = -5; dist <= 5; dist++) {
        if (dist == 0 || static_cast<int64_t>(row) + dist < 0 || static_cast<int64_t>(row) + dist >= static_cast<int64_t>(p->ROWS)) {
            DPRINTF(NeuroHammer,"Row out of bounds, no neurohammer effects\n");
            continue;
        }
        
        // Determine hammer increment value
        double hammerIncrement = 0.0;
        int abs_dist = std::abs(dist);
        if (isReadHammering) {
            if (abs_dist == 1) hammerIncrement = p->inc_dist_1_read;
            else if (abs_dist == 2) hammerIncrement = p->inc_dist_2_read;
            else if (abs_dist == 3) hammerIncrement = p->inc_dist_3_read;
            else if (abs_dist == 4) hammerIncrement = p->inc_dist_4_read;
            else if (abs_dist == 5) hammerIncrement = p->inc_dist_5_read;
        } else { // isWrite
            if (abs_dist == 1) hammerIncrement = p->inc_dist_1_write;
            else if (abs_dist == 2) hammerIncrement = p->inc_dist_2_write;
            else if (abs_dist == 3) hammerIncrement = p->inc_dist_3_write;
            else if (abs_dist == 4) hammerIncrement = p->inc_dist_4_write;
            else if (abs_dist == 5) hammerIncrement = p->inc_dist_5_write;
        }
        
        if (hammerIncrement == 0.0) {
            // We do not increment for this distance
            continue;
        }
        
        // Get base address of the victim row
        uint64_t victimRowBase = GetPhysicalAddress(subarray,channel, rank, bank, row + dist, 0);
        
        // Initialize or increment hammer count
        if (hammerCount.find(victimRowBase) == hammerCount.end()) {
            hammerCount[victimRowBase] = hammerIncrement;
            DPRINTF(NeuroHammer,"Row accessed for the first time\n");
            continue;
        }
        hammerCount[victimRowBase] += hammerIncrement;
        DPRINTF(NeuroHammer,"Hammer count for row %llu: %f/%f\n", victimRowBase,hammerCount[victimRowBase],p->HC_first);

        // Check if we've reached the threshold for bit flips
        if (hammerCount[victimRowBase] < p->HC_first) {
            continue;
        }
        
        // Calculate bit flip probability based on hammer count
        double progress = std::min(
            static_cast<double>(hammerCount[victimRowBase] - p->HC_first) / 
            static_cast<double>(p->HC_last - p->HC_first), 
            1.0
        );
        
        double rowFlipRate = p->HC_last_bitflip_rate * progress * 64; // * bits in quadword
        
        DPRINTF(NeuroHammer,"Row flip rate: %f And hammer count: %llu\n", rowFlipRate,hammerCount[victimRowBase]);

        // Iterate over quadwords in the victim row
        for (uint64_t quadAddr=victimRowBase;quadAddr<victimRowBase+rowSizeBytes;quadAddr+=sizeof(uint64_t)) {
            
            // Skip if already flipped
            if (flippedQuadwords.find(quadAddr) != flippedQuadwords.end()) {
                continue;
            }
            
            // Probabilistically flip quadword
            if (GenerateProbability(quadAddr) > rowFlipRate) {
                // No flip
                continue;
            }
            
            // Mark as flipped
            flippedQuadwords.insert(quadAddr);
            
            DPRINTF(NeuroHammer,"Generating bit flip mask\n");
            // Generate bit flip mask
            uint64_t mask = 0;
            if (p->flip_mask) {
                mask = p->flip_mask;
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
                
                // Generate random bit positions
                std::uniform_int_distribution<int> posDist(0, 63);
                for (int j = 0; j < flippedBits; j++) {
                    int pos;
                    // Find position that is not yet taken
                    do {
                        pos = posDist(gen);
                    } while (mask & (((uint64_t)1) << pos));
                    mask |= ((uint64_t)1) << pos;
                }
            }

            // Apply the flip to the simulated memory.
            uint64_t gem5Addr = quadAddr + addressFixUp;
            uint64_t* hostAddr = (uint64_t*)NVMainMemory::masterInstance->toHostAddr(gem5Addr);
            uint64_t oldData = *hostAddr;
            *hostAddr ^= mask; // Apply XOR mask to flip bits.
            totalBitFlips += __builtin_popcountll(mask);

            DPRINTF(NeuroHammer_BitFlip,"BIT FLIP! Addr: 0x%x, Mask: 0x%x, Old Data: 0x%x, New Data: 0x%x\n",quadAddr, mask, oldData, *hostAddr);
        }
    }
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
    AddStat(totalBitFlips);
    AddStat(rowsAffected);
    AddStat(totalHammerCount);
}