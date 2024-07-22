#include "power_outage/power_outage_controller.hh"

#include <random>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/PowerOutage.hh"

namespace gem5
{

    PowerOutageController::PowerOutageController(const Params &p)
        : ClockedObject(p),
          powerOutageEvent([this]{ performPowerOutage(); }, name()),
          checkFlushFinishEvent([this]{ finishFlush(); }, name()),
          firstPowerOutage(p.first_po),
          powerOutagePeriod(p.po_period),
          maxNoOfPowerOutages(p.max_po),
          noOfPowerOutages(0),
          cpus(p.cpus),
          l1i_caches(p.l1i_caches),
          l1d_caches(p.l1d_caches),
          l2_caches(p.l2_caches),
          l3_cache(p.l3_cache),
          po_stats(*this)
    {
        DPRINTF(PowerOutage, "Starting power outage controller\n");
        assert(l2_caches.size() <= l1d_caches.size());
        schedulePowerOutage(p.first_po);
    }

    void PowerOutageController::schedulePowerOutage(Tick tick)
    {
        schedule(powerOutageEvent, tick);
    }

    void PowerOutageController::performPowerOutage()
    {
        DPRINTF(PowerOutage, "Performing power outage\n");
        po_stats.noOfPowerOutages++;
        noOfPowerOutages++;
        startOfPowerOutage = ticksToCycles(curTick());
        is_draining_cpu = true;

        for (const auto& cpu : cpus) {
            DPRINTF(PowerOutage, "Pushing drain\n");
            cpu->manualDrain();
        }
        schedule(checkFlushFinishEvent, clockEdge(Cycles(1)));
    }

    void PowerOutageController::finishFlush()
    {
        if (is_draining_cpu) {
            bool are_all_drained = true;
            for (const auto& cpu : cpus) {
                if (cpu->manualDrain() != DrainState::Drained) {
                    are_all_drained = false;
                }
            }
            if (are_all_drained) {
                DPRINTF(PowerOutage, "Drained CPU\n");
                is_draining_cpu = false;
                is_flushing_l1_cache = true;
            }
            schedule(checkFlushFinishEvent, clockEdge(Cycles(1)));
        } else if (is_flushing_l1_cache) {

            DPRINTF(PowerOutage, "Flushing L1 caches\n");
            bool drainStageDone = true;
            for (HybridCache* l1i_cache : l1i_caches) {
                if (!l1i_cache->awaitMSHRDrain()) {
                    drainStageDone = false;
                }
            }
            for (HybridCache* l1d_cache : l1d_caches) {
                if (!l1d_cache->awaitMSHRDrain()) {
                    drainStageDone = false;
                }
            }

            if (drainStageDone) {
                if (once) {
                    DPRINTF(PowerOutage, "Drained L1 cache MSHR Queues\n");
                    po_stats.cyclesToAwaitL1MSHR +=
                        (ticksToCycles(curTick()) - startOfPowerOutage);
                    for (int i = 0; i < l1i_caches.size(); i++) {
                        writebacks_icache.push_back(PacketList());
                        l1i_caches[i]->collectWritebacks(writebacks_icache[i]);
                        po_stats.poL1Writebacks += writebacks_icache[i].size();
                    }
                    for (int i = 0; i < l1d_caches.size(); i++) {
                        writebacks_dcache.push_back(PacketList());
                        l1d_caches[i]->collectWritebacks(writebacks_dcache[i]);
                        po_stats.poL1Writebacks += writebacks_dcache[i].size();
                    }
                    once = false;
                }
            }

            if (drainStageDone) {
                for (int i = 0; i < l1i_caches.size(); i++) {
                    if (!l1i_caches[i]->tryWritebacks(writebacks_icache[i])) {
                        drainStageDone = false;
                    }
                }
                for (int i = 0; i < l1d_caches.size(); i++) {
                    if (!l1d_caches[i]->tryWritebacks(writebacks_dcache[i])) {
                        drainStageDone = false;
                    }
                }
            }

            if (drainStageDone) {
                DPRINTF(PowerOutage, "Stored L1 writebacks in write buffer\n");
                for (int i = 0; i < l1i_caches.size(); i++) {
                    assert(writebacks_icache[i].empty());
                }
                for (int i = 0; i < l1d_caches.size(); i++) {
                    assert(writebacks_dcache[i].empty());
                }
                for (HybridCache* l1i_cache : l1i_caches) {
                    if (!l1i_cache->awaitWriteBufferDrain()) {
                        drainStageDone = false;
                    }
                }
                for (HybridCache* l1d_cache : l1d_caches) {
                    if (!l1d_cache->awaitWriteBufferDrain()) {
                        drainStageDone = false;
                    }
                }
            }

            if (drainStageDone) {
                DPRINTF(PowerOutage, "Flushed L1 caches\n");
                is_flushing_l1_cache = false;
                if (!l2_caches.empty()) {
                    is_flushing_l2_cache = true;
                    once = true;
                    l1FinishCycle = ticksToCycles(curTick());
                }
            }

            schedule(checkFlushFinishEvent, clockEdge(Cycles(1)));
        } else if (is_flushing_l2_cache) {
            DPRINTF(PowerOutage, "Flushing L2 caches\n");
            bool drainStageDone = true;
            for (HybridCache* l2_cache : l2_caches) {
                if (!l2_cache->awaitMSHRDrain()) {
                    drainStageDone = false;
                }
            }
            if (drainStageDone) {
                if (once) {
                    DPRINTF(PowerOutage, "Drained L2 cache MSHR Queues\n");
                    po_stats.cyclesToAwaitL2MSHR +=
                        (ticksToCycles(curTick()) - l1FinishCycle);
                    for (int i = 0; i < l2_caches.size(); i++) {
                        l2_caches[i]->collectWritebacks(writebacks_dcache[i]);
                        po_stats.poL2Writebacks += writebacks_dcache[i].size();
                    }
                    once = false;
                }
            }

            if (drainStageDone) {
                for (int i = 0; i < l2_caches.size(); i++) {
                    if (!l2_caches[i]->tryWritebacks(writebacks_dcache[i])) {
                        drainStageDone = false;
                    }
                }
            }

            if (drainStageDone) {
                DPRINTF(PowerOutage, "Stored L2 writebacks in write buffer\n");
                for (int i = 0; i < l2_caches.size(); i++) {
                    assert(writebacks_dcache[i].empty());
                }
                for (HybridCache* l2_cache : l2_caches) {
                    if (!l2_cache->awaitWriteBufferDrain()) {
                        drainStageDone = false;
                    }
                }
            }

            if (drainStageDone) {
                DPRINTF(PowerOutage, "Flushed L2 caches\n");
                is_flushing_l2_cache = false;
                if (l3_cache) {
                    is_flushing_l3_cache = true;
                    once = true;
                    l2FinishCycle = ticksToCycles(curTick());
                }
            }
            schedule(checkFlushFinishEvent, clockEdge(Cycles(1)));

        } else if (is_flushing_l3_cache) {
            DPRINTF(PowerOutage, "Flushing L3 cache\n");
            bool drainStageDone = true;
            if (!l3_cache->awaitMSHRDrain()) {
                drainStageDone = false;
            }
            if (drainStageDone) {
                if (once) {
                    DPRINTF(PowerOutage, "Drained L3 cache MSHR Queue\n");
                    po_stats.cyclesToAwaitL3MSHR +=
                        (ticksToCycles(curTick()) - l2FinishCycle);
                    l3_cache->collectWritebacks(writebacks_dcache[0]);
                    po_stats.poL3Writebacks += writebacks_dcache[0].size();
                    once = false;
                }
            }

            if (drainStageDone) {
                if (!l3_cache->tryWritebacks(writebacks_dcache[0])) {
                    drainStageDone = false;
                }
            }

            if (drainStageDone) {
                DPRINTF(PowerOutage, "Stored L3 writebacks in write buffer\n");
                assert(writebacks_dcache[0].empty());
                if (!l3_cache->awaitWriteBufferDrain()) {
                    drainStageDone = false;
                }
            }

            if (drainStageDone) {
                DPRINTF(PowerOutage, "Flushed L3 cache\n");
                is_flushing_l3_cache = false;
            }
            schedule(checkFlushFinishEvent, clockEdge(Cycles(1)));
        } else {
            for (const auto& cpu : cpus) {
                cpu->manualDrainResume();
            }
            once = true;
            po_stats.cyclesToSecureState +=
                (ticksToCycles(curTick())- startOfPowerOutage);
            if (noOfPowerOutages < maxNoOfPowerOutages
                || maxNoOfPowerOutages == 0) {
                DPRINTF(PowerOutage, "Next power outage at cycle: %d\n",
                clockEdge(powerOutagePeriod));
                schedule(powerOutageEvent, clockEdge(powerOutagePeriod));
            }
        }
    }

    PowerOutageController::PowerOutageStats::PowerOutageStats(
        PowerOutageController &c)
        : statistics::Group(&c),
        ADD_STAT(cyclesToSecureState, statistics::units::Count::get(),
        "Number of system cycles to secure the state during power outages"),
        ADD_STAT(noOfPowerOutages, statistics::units::Count::get(),
        "Number of power outages performed"),
        ADD_STAT(cyclesToAwaitL1MSHR, statistics::units::Count::get(),
        "System cycles to await for mshr drain from L1 caches"),
        ADD_STAT(cyclesToAwaitL2MSHR, statistics::units::Count::get(),
        "System cycles to await for mshr drain from L2 caches"),
        ADD_STAT(cyclesToAwaitL3MSHR, statistics::units::Count::get(),
        "System cycles to await for mshr drain from L3 cache"),
        ADD_STAT(poL1Writebacks, statistics::units::Count::get(),
        "Number of L1 cache writebacks to be performed during power outages"),
        ADD_STAT(poL2Writebacks, statistics::units::Count::get(),
        "Number of L2 cache writebacks to be performed during power outages"),
        ADD_STAT(poL3Writebacks, statistics::units::Count::get(),
        "Number of L3 cache writebacks to be performed during power outages"),
        ADD_STAT(avgCyclesToSecureState, statistics::units::Rate<
        statistics::units::Ratio, statistics::units::Cycle>::get(),
        "Average number of system cycles to secure state during power outages")
    {
    }

    void
    PowerOutageController::PowerOutageStats::regStats()
    {
        using namespace statistics;

        statistics::Group::regStats();

        avgCyclesToSecureState = cyclesToSecureState /
            noOfPowerOutages;
    }

}