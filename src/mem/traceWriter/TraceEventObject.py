from m5.params import *
from m5.SimObject import SimObject

class TraceEventObject(SimObject):
    type = 'TraceEventObject'
    cxx_header = "mem/traceWriter/TraceEventObject.hh"
    cxx_class = "gem5::TraceEventObject"

    timer = Param.Latency("Time for next Trace write")