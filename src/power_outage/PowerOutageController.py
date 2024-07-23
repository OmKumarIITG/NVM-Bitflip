from m5.params import *
from m5.objects.ClockedObject import ClockedObject
from m5.objects.BaseO3CPU import *
from m5.objects.Cache import *

class PowerOutageController(ClockedObject):
    type = 'PowerOutageController'
    cxx_header = "power_outage/power_outage_controller.hh"
    cxx_class = "gem5::PowerOutageController"
    first_po = Param.Tick(400000000,
        "Tick of first simulated power outage")
    po_period = Param.Cycles(2500000,
        "Gap to the next power outage after resuming")
    max_po = Param.Unsigned(0,
        "Maximum number of simulated power outages")

    cpus = VectorParam.BaseO3CPU([], "CPUs affected")
    l3_cache = Param.HybridCache(NULL, "L3 cache affected")
    l2_caches = VectorParam.HybridCache([], "L2 caches affected")
    l1i_caches = VectorParam.HybridCache([], "L1 instruction caches affected")
    l1d_caches = VectorParam.HybridCache([], "L1 data caches affected")