from m5.params import *
from m5.SimObject import SimObject

# A wrapper for DRAMSim3 multi-channel memory controller
class FlatMemory(SimObject):
    type = 'FlatMemory'
    cxx_header = "mem/flat_mem.hh"

    # A single port for now
    bus_side_port = ResponsePort("port for receiving requests from"
                        "the membus or other requestor")

    mem_side_port = RequestPort("port for sending requests to"
                        "physical memory")