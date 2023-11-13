#ifndef __MEMORY_CONTENT_HH__
#define __MEMORY_CONTENT_HH__

#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/types.h>
#include "base/types.hh"

namespace gem5
{
    class memory_content
    {
        private:
        Addr address;
        bool cmd;
        uint64_t newContent = 0;
        uint64_t oldContent = 0;
        uint64_t bitFlips[64] = {0};
        uint64_t tick = 0;

        public:
        
        uint64_t getNewContent()
        {
            return this->newContent;
        }

        void setNewContent(uint64_t newerContent)
        {   
            this->newContent = newerContent;
        }

        uint64_t getOldContent()
        {
            return this->oldContent;
        }

        void setOldContent(uint64_t oldContent)
        {   
            this->oldContent = oldContent;
        }

        Addr getAddress()
        {
            return this->address;
        }

        void setAddress(Addr newAddress)
        {
            this->address = newAddress;
        }

        uint64_t getTick()
        {
            return this->tick;
        }

        uint64_t* getBitFlips()
        {
            return this->bitFlips;
        }
        
        void setTick(uint64_t curtick)
        {
            this->tick = curtick;
        }

        bool getCmd()
        {
            return this->cmd;
        }

        void setCmd(bool cmd)
        {
            if(this->cmd != cmd)
                this->cmd = cmd;
        }

        void setContent(Addr address, bool cmd, uint64_t oldContent, uint64_t newerContent, uint64_t tick);
        void updateContent(Addr address, bool cmd, uint64_t content, uint64_t curtick);
        void addFlipCount(std::bitset<64> set);
    };  
} //namespace gem5
#endif