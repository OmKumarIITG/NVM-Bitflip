/*
* Based on PowerOutageController from master thesis
* "Simulation of hybrid non-volatile cache hierarchies in gem5"
* by Cord Bleibaum
*/
#ifndef __POWER_OUTAGE_CONTROLLER_HH__
#define __POWER_OUTAGE_CONTROLLER_HH__

#include <random>
#include <vector>

#include "base/statistics.hh"
#include "cpu/o3/cpu.hh"
#include "mem/cache/hybrid_cache.hh"
#include "params/PowerOutageController.hh"
#include "sim/clocked_object.hh"

namespace gem5
{
    class PowerOutageController : public ClockedObject
    {
    public:
        typedef PowerOutageControllerParams Params;
        PowerOutageController(const Params &p);

        void schedulePowerOutage(Tick tick);
    private:
        void performPowerOutage();
        void finishFlush();

        EventFunctionWrapper powerOutageEvent;
        EventFunctionWrapper checkFlushFinishEvent;
        Tick firstPowerOutage;
        Cycles powerOutagePeriod;
        unsigned maxNoOfPowerOutages;
        unsigned noOfPowerOutages;

        std::vector<o3::CPU*> cpus;
        std::vector<HybridCache*> l1i_caches;
        std::vector<HybridCache*> l1d_caches;
        std::vector<HybridCache*> l2_caches;
        HybridCache* l3_cache;

        bool is_draining_cpu = false;
        bool is_flushing_l1_cache = false;
        bool is_flushing_l2_cache = false;
        bool is_flushing_l3_cache = false;
        // Transitory storage for data to be written back
        std::vector<PacketList> writebacks_icache;
        std::vector<PacketList> writebacks_dcache;
        // For stats
        Cycles startOfPowerOutage;
        Cycles l1FinishCycle;
        Cycles l2FinishCycle;
        bool once = true;
    public:
        struct PowerOutageStats : public statistics::Group
        {
            PowerOutageStats(PowerOutageController &c);
            void regStats() override;

            statistics::Scalar cyclesToSecureState;
            statistics::Scalar noOfPowerOutages;
            statistics::Scalar cyclesToAwaitL1MSHR;
            statistics::Scalar cyclesToAwaitL2MSHR;
            statistics::Scalar cyclesToAwaitL3MSHR;
            statistics::Scalar poL1Writebacks;
            statistics::Scalar poL2Writebacks;
            statistics::Scalar poL3Writebacks;
            statistics::Formula avgCyclesToSecureState;
        } po_stats;
    };
}

#endif