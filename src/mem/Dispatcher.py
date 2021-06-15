from m5.params import *
from m5.SimObject import SimObject

# A wrapper for DRAMSim3 multi-channel memory controller
class Dispatcher(SimObject):
    type = 'Dispatcher'
    cxx_header = "mem/dispatcher.hh"

    rt_side_port = ResponsePort("port for receiving requests from"
                        "remapping table")

    mm_side_port = ResponsePort("port for receiving requests from"
                        "migration manager")
    
    ac_side_port = RequestPort("port for sending requests to"
                        "access counter")

    hbm_side_port = RequestPort("port for sending requests to"
                        "physical hbm memory")
    
    dram_side_port = RequestPort("port for sending requests to"
                        "physical dram memory")