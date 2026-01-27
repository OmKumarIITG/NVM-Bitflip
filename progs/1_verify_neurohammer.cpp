#include <iostream>
#include <emmintrin.h>
#include <fcntl.h>
#include <inttypes.h>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <unistd.h>
#include <vector>
#include <cstdint>
#include <signal.h>
#include <assert.h>
#include <string.h>
#include <cstdio> // For perror
#include <bitset> // Added for std::bitset
#include <time.h>
using namespace std;

// Size of allocated buffer
#define BUFFER_SIZE_MB 64

// Size of hugepages in system
#define HUGE_PAGE_SIZE (1 << 21) //2 MB (MEGA BYTES)

// Size of basepage in system
#define BASE_PAGE_SIZE (1 << 12) //4 KB (KILO BYTES)

// Size of DRAM row (1 bank)
#define ROW_SIZE (8192)

// Number of hammers to perform per iteration
#define HAMMERS_PER_ITER 5000

// Physical Page Number to Virtual Page Number Map
std::map<uint64_t, uint64_t> PPN_VPN_map;
/*
 * allocate_pages
 *
 * Allocates a memory block of size BUFFER_SIZE_MB
 *
 * Make sure to write something to each page in the block to ensure
 * that the memory has actually been allocated!
 *
 * Inputs: none
 * Outputs: A pointer to the beginning of the allocated memory block
 */
void *allocate_pages(uint64_t memory_size)
{
    void *memory_block = mmap(NULL, memory_size, PROT_READ | PROT_WRITE,
                              MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE , -1, 0);
    assert(memory_block != (void *)-1);

    for (uint64_t i = 0; i < memory_size; i += BASE_PAGE_SIZE)
    {
        uint64_t *addr = (uint64_t *)((uint8_t *)(memory_block) + i);
        *addr = i;
        if(i%1000==0){
                cout<<" [*]Touched page: "<<i<<endl;
        }
    }

    return memory_block;
}

/*
 * virt_to_phys
 * Translates a virtual address to a physical address using /proc/self/pagemap.
 */
 uint64_t virt_to_phys(uint64_t virt_addr) {
    uint64_t phys_addr = 0;
    uint64_t virt_page_number = virt_addr / BASE_PAGE_SIZE;
    uint64_t file_offset = virt_page_number * sizeof(uint64_t);

    FILE *pagemap = fopen("/proc/self/pagemap", "rb");
    if (!pagemap) {
        perror("fopen /proc/self/pagemap");
        return 0;
    }

    if (lseek(fileno(pagemap), file_offset, SEEK_SET) == (off_t)file_offset) {
        uint64_t entry;
        if (fread(&entry, sizeof(uint64_t), 1, pagemap)) {
            if (entry & (1ULL << 63)) {  // Present bit
                uint64_t pfn = entry & ((1ULL << 54) - 1);
                uint64_t offset_in_base_page = virt_addr % BASE_PAGE_SIZE;
                phys_addr = (pfn * BASE_PAGE_SIZE) + offset_in_base_page;
            } else {
                std::cout << "[!] Page not present for VA=0x"
                          << std::hex << virt_addr << std::dec << std::endl;
            }
        } else {
            std::cout << "[!] Failed to read pagemap entry for VA=0x"
                      << std::hex << virt_addr << std::dec << std::endl;
        }
    } else {
        std::cout << "[!] Failed to seek pagemap for VA=0x"
                  << std::hex << virt_addr << std::dec << std::endl;
    }

    fclose(pagemap);
    return phys_addr;
}


/*
 * setup_PPN_VPN_map
 *
 * Populates the Physical Page Number -> Virtual Page Number mapping table
 *
 * Inputs: mem_map - Base pointer to the large allocated pool
 *         PPN_VPN_map - Reference to a PPN->VPN map
 *
 * Side-Effects: For *each page* in the allocated pool, the virtual page
 *               number is into the map with a key corresponding to the
 *               page's physical page number.
 *
 */
void setup_PPN_VPN_map(void * mem_map,
                       std::map<uint64_t, uint64_t> &PPN_VPN_map) {
    // Calculate the total size of allocated memory
    uint64_t memory_size = BUFFER_SIZE_MB * (1ULL << 20); // MB → bytes

    // Clear any existing mappings
    PPN_VPN_map.clear();

    // Iterate through each base page in the allocated memory pool
    for (uint64_t offset = 0; offset < memory_size; offset += BASE_PAGE_SIZE) {
        uint64_t virt_addr = (uint64_t)mem_map + offset;
        uint64_t phys_addr = virt_to_phys(virt_addr);

        if (phys_addr) {
            uint64_t vpn = virt_addr / BASE_PAGE_SIZE;
            uint64_t ppn = phys_addr / BASE_PAGE_SIZE;

            PPN_VPN_map[ppn] = vpn;
        }
    }

    printf("Setup PPN->VPN mapping for %zu pages completed!.\n", PPN_VPN_map.size());
}

/*
 * phys_to_virt
 *
 * Determines the virtual address mapping to a given physical address
 *
 * HINT: This should use your PPN_VPN_map!
 *
 * Inputs: phys_addr - A physical pointer/address
 * Output: virt_addr - The virtual address corresponding to the physical pointer
 *                     If the physical pointer is not mapped, return 0
 *
 */

uint64_t phys_to_virt(uint64_t phys_addr) {
    uint64_t ppn = phys_addr / BASE_PAGE_SIZE;
    auto it = PPN_VPN_map.find(ppn);

    if (it == PPN_VPN_map.end()) {
        return 0; // not found
    }

    uint64_t vpn = it->second;
    uint64_t offset_in_page = phys_addr % BASE_PAGE_SIZE;
    return (vpn * BASE_PAGE_SIZE) + offset_in_page;
}

// ================== CONFIG ==================
// Bit widths (customize to your DRAM org)
#define CHANNEL_BITS   2
#define BANK_BITS      2
#define ROW_BITS       13
#define RANK_BITS      0
#define COL_BITS       9
#define SUBARRAY_BITS  0   // set to 0 if unused
#define OFFSET_BITS    6   // byte offset within cache line (e.g., 32B)

// ================== FORWARD: PA → FIELDS ==================
void translate_address(uint64_t addr,
                       uint64_t *channel,
                       uint64_t *bank,
                       uint64_t *row,
                       uint64_t *rank,
                       uint64_t *col,
                       uint64_t *subarray)
{
    // Remove byte offset first
    addr >>= OFFSET_BITS;

    *col      = addr & ((1ULL << COL_BITS) - 1);
    addr    >>= COL_BITS;

    *rank     = addr & ((1ULL << RANK_BITS) - 1);
    addr    >>= RANK_BITS;

    *row      = addr & ((1ULL << ROW_BITS) - 1);
    addr    >>= ROW_BITS;

    *bank     = addr & ((1ULL << BANK_BITS) - 1);
    addr    >>= BANK_BITS;

    *channel  = addr & ((1ULL << CHANNEL_BITS) - 1);
    addr    >>= CHANNEL_BITS;

    if (SUBARRAY_BITS > 0) {
        *subarray = addr & ((1ULL << SUBARRAY_BITS) - 1);
    } else {
        *subarray = 0;
    }
}

// ================== REVERSE: FIELDS → PA ==================
uint64_t reverse_translate_address(uint64_t channel,
                                   uint64_t bank,
                                   uint64_t row,
                                   uint64_t rank,
                                   uint64_t col,
                                   uint64_t subarray)
{
    uint64_t addr = 0;

    // Build address from MSB → LSB (reverse order of extraction)
    addr |= (channel & ((1ULL << CHANNEL_BITS) - 1));
    addr <<= BANK_BITS;
    addr |= (bank & ((1ULL << BANK_BITS) - 1));
    addr <<= ROW_BITS;
    addr |= (row & ((1ULL << ROW_BITS) - 1));
    addr <<= RANK_BITS;
    addr |= (rank & ((1ULL << RANK_BITS) - 1));
    addr <<= COL_BITS;
    addr |= (col & ((1ULL << COL_BITS) - 1));

    if (SUBARRAY_BITS > 0) {
        addr <<= SUBARRAY_BITS;
        addr |= (subarray & ((1ULL << SUBARRAY_BITS) - 1));
    }

    // Restore offset bits
    addr <<= OFFSET_BITS;

    return addr;
}

// Hammer: with sum accumulation for sanity check
void hammer(uint64_t *addr1, uint64_t *addr2, size_t hammer_count) {
    for (size_t i = 0; i < hammer_count; i++) {
        // write 0
        *(volatile uint64_t *)addr1 = 0ULL;
        *(volatile uint64_t *)addr2 = 0ULL;

        // flush from cache
        asm volatile("clflush (%0)" :: "r"(addr1) : "memory");
        asm volatile("clflush (%0)" :: "r"(addr2) : "memory");

        // enforce ordering
        asm volatile("mfence" ::: "memory");
    }
}

int main()
{
    cout << "[*] Program started." << endl;

    // 1. Allocate with base pages
    const uint64_t memory_size = BUFFER_SIZE_MB * (1ULL << 20); //convert MB to bytes
    cout << "[*] Allocating " << BUFFER_SIZE_MB << " MB of regular pages..." << endl;
    void *mem_block = allocate_pages(memory_size);
    if (!mem_block) {
        cerr << "[X] ERROR: allocate_pages() failed." << endl;
        return EXIT_FAILURE;
    }
    cout << "[*] Allocation successful. mem_block=" << mem_block << endl;

    // 2. Build mapping
    cout << "[*] Setting up PPN->VPN mapping..." << endl;
    setup_PPN_VPN_map(mem_block, PPN_VPN_map);
    cout << "[*] Mapping setup complete. Total entries=" << PPN_VPN_map.size() << endl;

    // 3. Pick one virtual address
    cout << "[*] Picking first aggressor address..." << endl;
    uint64_t virt1 = (uint64_t)mem_block + 0x10000;
    uint64_t phys1 = virt_to_phys(virt1);
    cout << "[*] virt1=0x" << hex << virt1 << " phys1=0x" << phys1 << dec << endl;

    uint64_t ch, bk, row, rk, col, sub;
    translate_address(phys1, &ch, &bk, &row, &rk, &col, &sub);

    cout << "[*] Aggressor 1:"
         << " Row=" << row << " Bank=" << bk
         << " Channel=" << ch << " Col=" << col
         << " Rank=" << rk << " Subarray=" << sub
         << endl;

    // 4. Pick another address in same bank, row+2
    cout << "[*] Picking second aggressor address..." << endl;
    uint64_t row2 = row + 2;
    uint64_t phys2 = reverse_translate_address(ch, bk, row2, rk, col, sub);
    uint64_t virt2 = phys_to_virt(phys2);

    if (!virt2) {
        cerr << "[X] ERROR: couldn't map second aggressor back to VA" << endl;
        return 1;
    }
    cout << "[*] virt2=0x" << hex << virt2 << " phys2=0x" << phys2 << dec << endl;

    uint64_t ch2, bk2, row2f, rk2f, col2, sub2;
    translate_address(phys2, &ch2, &bk2, &row2f, &rk2f, &col2, &sub2);

    cout << "[*] Aggressor 2:"
         << " Row=" << row2f << " Bank=" << bk2
         << " Channel=" << ch2 << " Col=" << col2
         << " Rank=" << rk2f << " Subarray=" << sub2
         << endl;

    // 5. Initialize all memory to 0x00
    cout << "[*] Initializing memory to 0x00..." << endl;
    memset(mem_block, 0x00, memory_size);
    cout << "[*] Memory initialization done." << endl;

    // 6. Hammer both aggressors
    cout << "[*] Starting hammering with " << HAMMERS_PER_ITER << " iterations..." << endl;
    hammer((uint64_t*)virt1, (uint64_t*)virt2, HAMMERS_PER_ITER);
    cout << "[*] Hammering complete." << endl;

    // 7. Scan memory for flips
    cout << "[*] Scanning memory for bit flips..." << endl;
    uint64_t *buf = (uint64_t*)mem_block;
    size_t words = memory_size / sizeof(uint64_t);

    size_t flip_count = 0;
    for (size_t i = 0; i < words; i++) {
        if (buf[i] != 0x0000000000000000ULL) {
            flip_count++;
            uint64_t vaddr = (uint64_t)&buf[i];
            uint64_t paddr = virt_to_phys(vaddr);
            uint64_t chx, bkx, rowx, rkx, colx, subx;
            translate_address(paddr, &chx, &bkx, &rowx, &rkx, &colx, &subx);

            cout << "[!] BIT FLIP #" << flip_count
                 << " VA=0x" << hex << vaddr
                 << " PA=0x" << paddr << dec
                 << " | Row=" << rowx
                 << " Col=" << colx
                 << " Bank=" << bkx
                 << " Subarray=" << subx
                 << " Channel=" << chx
                 << " Rank=" << rkx
                 << " Original=0x0"
                 << " New=0x" << hex << buf[i]
                 << endl;
        }
    }

    cout << "[*] Scan complete. Total bit flips found: " << flip_count << endl;
    cout << "[*] Program finished successfully." << endl;

    return 0;
}