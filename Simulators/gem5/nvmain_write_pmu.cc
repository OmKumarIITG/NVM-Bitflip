#include "nvmain_write_pmu.hh"
#include <params/NVMainWritePMU.hh>
#include <iostream>

Nvmain_Write_PMU *Nvmain_Write_PMU::instance=0;

Nvmain_Write_PMU::Nvmain_Write_PMU(const Params *p) : SimObject(p) {
    instance = this;
}

void Nvmain_Write_PMU::regProbePoints(){
/**
     * Register the memory write pmu probe
     */
    ppMemBusWrites.reset(
        new ProbePoints::PMU(getProbeManager(), "MemBusWrites"));
}

void Nvmain_Write_PMU::triggerWrite() { 
    ppMemBusWrites->notify(1); }

Nvmain_Write_PMU *NVMainWritePMUParams::create() {
    return new Nvmain_Write_PMU(this);
}