#include "FaultModels/NeuroHammer/NeuroHammer.h"
#include "include/NVMHelpers.h"
#include <iostream>
#include <cmath>
#include <algorithm>

#include "mem/request.hh"
#include "mem/packet.hh"
#include "Simulators/gem5/nvmain_mem.hh"
using namespace NVM;
using namespace gem5;
using namespace gem5::memory;

//for debug
#include "debug/NeuroHammer.hh"
#include "base/trace.hh"

// Static instance pointer initialization
NeuroHammer* NeuroHammer::instance = nullptr;

// Singleton implementation
NeuroHammer* NeuroHammer::GetInstance()
{
    if (instance == nullptr) {
        instance = new NeuroHammer();
        std::cout << "NeuroHammer: Singleton instance created" << std::endl;
    }
    return instance;
}

void NeuroHammer::DestroyInstance()
{
    if (instance != nullptr) {
        delete instance;
        instance = nullptr;
        std::cout << "NeuroHammer: Singleton instance destroyed" << std::endl;
    }
}

NeuroHammer::NeuroHammer()
{
    std::cout<<"NeuroHammer Constructor Called"<<std::endl;
    
    totalBitFlips = 0;
    rowsAffected = 0;
    totalHammerCount = 0;
    
    // Initialize random number generators
    std::random_device rd;
    rng = std::mt19937_64(rd());
    paraRng = std::mt19937_64(rd());
    
    translator = NULL;
    p = NULL;
}

NeuroHammer::~NeuroHammer()
{
    std::cout << "NeuroHammer Destructor Called" << std::endl;
}

void NeuroHammer::SetConfig(Config *config, bool createChildren)
{
    FaultModel::SetConfig(config, createChildren);
    
    Params *params = new Params();
    params->SetParams(config);  
    SetParams(params);

    SetDebugName("NeuroHammer", config);
    
    // Print debug information for parameters
    std::cout << "DEBUG: NeuroHammer SetConfig called, trying to fetch parameters:" << std::endl;
    
    // Check if parameters exist
    std::cout << "  HC_first exists: " << config->KeyExists("HC_first") << std::endl;
    std::cout << "  HC_last exists: " << config->KeyExists("HC_last") << std::endl;
    std::cout << "  HC_last_bitflip_rate exists: " << config->KeyExists("HC_last_bitflip_rate") << std::endl;
    std::cout << "  inc_dist_1 exists: " << config->KeyExists("inc_dist_1") << std::endl;
    
    // Try to get parameter values if they exist
    if(config->KeyExists("HC_first")) {
        std::cout << "  HC_first value: " << config->GetValueUL("HC_first") << std::endl;
    }
    if(config->KeyExists("HC_last")) {
        std::cout << "  HC_last value: " << config->GetValueUL("HC_last") << std::endl;
    }
    if(config->KeyExists("HC_last_bitflip_rate")) {
        std::cout << "  HC_last_bitflip_rate value: " << config->GetEnergy("HC_last_bitflip_rate") << std::endl;
    }
}

void NeuroHammer::SetTranslator(AddressTranslator *trans)
{
    translator = trans;
}

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

bool NeuroHammer::InjectFault(NVMainRequest *request)
{
    if (translator == NULL)
    {
        std::cout << "NeuroHammer: FATAL - Address translator is not set!" << std::endl;
        return false;
    }

    std::cout << "Inject Fault Called for request address: 0x" << std::hex 
              << request->address.GetPhysicalAddress() << std::dec << std::endl;
    // Extract address components from the request
    uint64_t row, col, bank, rank, channel, subarray;
    request->address.GetTranslatedAddress(&row, &col, &bank, &rank, &channel, &subarray);
    
    // Check if this is a read or write request
    bool isRead = (request->type == READ || request->type == READ_PRECHARGE);
    bool isWrite = (request->type == WRITE || request->type == WRITE_PRECHARGE);
    
    // Only process read/write requests
    if (!isRead && !isWrite) {
        return false;
    }
    
    // Get row buffer hit status directly from the request
    bool bufferHit = request->isRowBufferHit;
    std::cout << "Buffer Hit: " << bufferHit << std::endl;
    
    // Get base address of this row
    uint64_t baseRowAddr = GetPhysicalAddress(subarray,channel, rank, bank, row, 0);

    // Calculate row size in bytes using official NVMain formula from MemoryController.cpp:
    // memory word size (in bytes) = device width * minimum burst length * data rate / (8 bits/byte) * number of devices
    // number of devices = bus width / device width
    // Simplifies to: memory word size = BusWidth * tBURST * RATE / 8
    uint64_t memoryWordSize = p->BusWidth * p->tBURST * p->RATE / 8;
    uint64_t rowSizeBytes = p->COLS * memoryWordSize;

    std::cout<<std::dec<<"Bus Width: "<<p->BusWidth<<"\n";
    std::cout<<"tBURST: "<<p->tBURST<<"\n";
    std::cout<<"RATE: "<<p->RATE<<"\n";
    std::cout<<"Memory Word Size: "<<memoryWordSize<<"\n";
    std::cout<<"COLS: "<<p->COLS<<"\n";
    std::cout<<"Row Size in Bytes: "<<rowSizeBytes<<"\n";

    // Process row hammer effects for reads
    if (isRead) {
        DPRINTF(NeuroHammer,"Processing row hammer effects for read\n");
        hammerCount.erase(baseRowAddr);
        
        // Process row hammer effects
        ProcessNeuroHammer(subarray,channel, rank, bank, row, bufferHit,request->addressFixUp,rowSizeBytes);
    }
    
    // For writes, clear hammer count and flipped status
    if (isWrite) {
        DPRINTF(NeuroHammer,"Processing row hammer effects for write\n");
        hammerCount.erase(baseRowAddr);
        
        // Clear flipped status for all quadwords in this row
        for (uint64_t quadAddr = baseRowAddr; quadAddr <baseRowAddr +  rowSizeBytes; quadAddr+=sizeof(uint64_t)) {
            flippedQuadwords.erase(quadAddr);
        }
    }
    
    return true;
}

void NeuroHammer::ProcessNeuroHammer(uint64_t subarray,uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row, bool bufferHit,uint64_t addressFixUp,uint64_t rowSizeBytes)
{
    // No neurohammer effects when rowbuffer hit
    if (bufferHit) {
        DPRINTF(NeuroHammer,"Buffer hit, no neurohammer effects\n");
        return;
    }
    
    // Process neighboring rows within distance 5
    for (int dist = -5; dist <= 5; dist++) {
        if (dist == 0 || row + dist < 0 || row + dist >= p->ROWS) {
            DPRINTF(NeuroHammer,"Row out of bounds, no neurohammer effects\n");
            continue;
        }
        
        // Determine increment based on distance
        int add = 0;
        switch (std::abs(dist)) {
            case 5: add = p->inc_dist_5; break;
            case 4: add = p->inc_dist_4; break;
            case 3: add = p->inc_dist_3; break;
            case 2: add = p->inc_dist_2; break;
            case 1: add = p->inc_dist_1; break;
        }
        
        if (add == 0) {
            DPRINTF(NeuroHammer,"We don't increment for this distance i.e add = 0\n");
            // We don't increment for this distance
            continue;
        }
        
        // Get base address of the victim row
        uint64_t victimRowBase = GetPhysicalAddress(subarray,channel, rank, bank, row + dist, 0);
        
        // Initialize or increment hammer count
        if (hammerCount.find(victimRowBase) == hammerCount.end()) {
            hammerCount[victimRowBase] = add;
            DPRINTF(NeuroHammer,"Row first time hit\n");
            continue;
        }

        hammerCount[victimRowBase] += add;
        
        DPRINTF(NeuroHammer,"Hammer count for row %llu: %llu/%llu\n", victimRowBase,hammerCount[victimRowBase],p->HC_first);
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
        
        DPRINTF(NeuroHammer,"Row flip rate: %f\n And hammer count: %llu\n", rowFlipRate,hammerCount[victimRowBase]);

        // Check each quadword in the row for potential bit flips
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
            DPRINTF(NeuroHammer,"Generated mask: 0x%x\n",mask);
            //now apply this mask at quadAddr directly
            // Convert NVMain address back to gem5 address using addressFixUp
            uint64_t gem5Addr = quadAddr + addressFixUp;
            uint64_t oldData = *(uint64_t*)NVMainMemory::masterInstance->toHostAddr(gem5Addr);
           *(uint64_t*)NVMainMemory::masterInstance->toHostAddr(gem5Addr) ^= mask;

            DPRINTF(NeuroHammer, "Flipped quadword at address 0x%x with mask 0x%x, Old Data: 0x%x, New Data: 0x%x, Hammer Count: %d\n", quadAddr, mask, oldData, *(uint64_t*)NVMainMemory::masterInstance->toHostAddr(gem5Addr),hammerCount[victimRowBase]);
        }
    }
}

uint64_t NeuroHammer::GetPhysicalAddress(uint64_t subarray,uint64_t channel, uint64_t rank, uint64_t bank, uint64_t row, uint64_t col)
{
    if (translator != NULL && translator->GetTranslationMethod() != NULL) {
        // Use the NVMain address translator
        uint64_t physAddr = translator->ReverseTranslate(row, col, bank, rank, channel, subarray);
        
        // For debugging
        std::cout <<std::dec<< "GetPhysicalAddress using NVMain translator: " << std::endl;
        std::cout << "  Channel: " << channel << ", Rank: " << rank << ", Bank: " << bank 
                  << ", Row: " << row << ", Col: " << col << std::endl;
        std::cout << "  Mapped to physical address: 0x" << std::hex << physAddr << std::dec << std::endl;
        
        return physAddr;
    } else {
        // Fallback to our simplified mapping if translator isn't available
        std::cout << "WARNING: Using fallback address mapping (translator not available)" << std::endl;
        
        // Simple address mapping scheme
        uint64_t colBits = 8;   // Typically 8-10 bits for column
        uint64_t rowBits = 16;  // Typically 14-16 bits for row
        uint64_t bankBits = 3;  // Typically 2-3 bits for bank
        uint64_t rankBits = 2;  // Typically 1-2 bits for rank
        uint64_t chBits = 1;    // Typically 1 bit for channel (renamed to avoid warning)
        
        uint64_t physAddr = 0;
        physAddr |= col;
        physAddr |= (row << colBits);
        physAddr |= (bank << (colBits + rowBits));
        physAddr |= (rank << (colBits + rowBits + bankBits));
        physAddr |= (channel << (colBits + rowBits + bankBits + rankBits));
        
        std::cout <<std::dec<< "  Channel: " << channel << ", Rank: " << rank << ", Bank: " << bank 
                  << ", Row: " << row << ", Col: " << col << std::endl;
        std::cout << "  Fallback mapped to physical address: 0x" << std::hex << physAddr << std::dec << std::endl;
        
        return physAddr;
    }
}

void NeuroHammer::RegisterStats()
{
    AddStat(totalBitFlips);
    AddStat(rowsAffected);
    AddStat(totalHammerCount);
}