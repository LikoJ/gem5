#include "base/trace.hh"
#include "mem/dispatcher.hh"
#include "debug/Dispatcher.hh"

Dispatcher::Dispatcher(const DispatcherParams& params)
    : SimObject(params),
      rt_side_port(name() + ".rt_side_port", *this, Dispatcher::PortType::RemappingTable),
      mm_side_port(name() + ".mm_side_port", *this, Dispatcher::PortType::MigrationManager),
      ac_side_port(name() + ".ac_side_port", *this, Dispatcher::PortType::AccessCounter),
      hbm_side_port(name() + ".hbm_side_port", *this, Dispatcher::PortType::PhysicalHbm),
      dram_side_port(name() + "dram_side_port", *this, Dispatcher::PortType::PhysicalDram),
      req_ac_rt_blocked(false),
      req_ac_mm_blocked(false),
      req_hbm_rt_blocked(false),
      req_hbm_mm_blocked(false),
      req_dram_rt_blocked(false),
      req_dram_mm_blocked(false),
      resp_mm_dram_blocked(false),
      resp_mm_hbm_blocked(false),
      resp_rt_dram_blocked(false),
      resp_rt_hbm_blocked(false) {}

Port&
Dispatcher::getPort(const std::string& if_name, PortID idx) {
    if (if_name == "rt_side_port") {
        return rt_side_port;
    } else if (if_name == "mm_side_port") {
        return mm_side_port;
    } else if (if_name == "ac_side_port") {
        return ac_side_port;
    } else if (if_name == "hbm_side_port") {
        return hbm_side_port;
    } else if (if_name == "dram_side_port") {
        return dram_side_port;
    } else {
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList
Dispatcher::getAddrRanges() const {
    DPRINTF(Dispatcher, "Sending new ranges\n");
    AddrRangeList hbmranges = hbm_side_port.getAddrRanges();
    AddrRangeList dramranges = dram_side_port.getAddrRanges();
    AddrRangeList res;
    AddrRange ranges(hbmranges.front().start(), dramranges.front().end());
    res.push_back(ranges);
    return res;
}

void
Dispatcher::sendRangeChange() {
    rt_side_port.sendRangeChange();
}

bool
Dispatcher::isDramOrHbm(PacketPtr pkt) {
    const uint64_t hbm_maxsize = 1048576; // hbm 1MB
    return pkt->getAddr() >= hbm_maxsize;
}

Tick
Dispatcher::handleAtomic(PacketPtr pkt) {
    if (isDramOrHbm(pkt)) {
        return dram_side_port.sendAtomic(pkt);
    } else {
        return hbm_side_port.sendAtomic(pkt);
    }
}

void
Dispatcher::handleFunctional(PacketPtr pkt) { 
    if(isDramOrHbm(pkt)) {
        dram_side_port.sendFunctional(pkt);
    } else {
        hbm_side_port.sendFunctional(pkt);
    }
}

bool
Dispatcher::handleRequest(PacketPtr pkt) {
    if (req_ac_rt_blocked || req_ac_mm_blocked || !ac_side_port.sendPacket(pkt)) {
        DPRINTF(Dispatcher, "Access Counter is busy! Request blocked for addr %#x\n", pkt->getAddr());
        if (pkt->reqport == Packet::PortType::RemappingTable) {
            req_ac_rt_blocked = true;
        } else if (pkt->reqport == Packet::PortType::MigrationManager) {
            req_ac_mm_blocked = true;
        }
        return false;
    }
    if (isDramOrHbm(pkt)) {
        if (req_dram_rt_blocked || req_dram_mm_blocked || !dram_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Physical DRAM is busy! Request blocked for addr %#x\n", pkt->getAddr());
            if (pkt->reqport == Packet::PortType::RemappingTable) {
                req_dram_rt_blocked = true;
            } else if (pkt->reqport == Packet::PortType::MigrationManager) {
                req_dram_mm_blocked = true;
            }
            return false;
        }
    } else {
        if (req_hbm_rt_blocked || req_hbm_mm_blocked || !hbm_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Physical HBM is busy! Request blocked for addr %#x\n", pkt->getAddr());
            if (pkt->reqport == Packet::PortType::RemappingTable) {
                req_hbm_rt_blocked = true;
            } else if (pkt->reqport == Packet::PortType::MigrationManager) {
                req_hbm_mm_blocked = true;
            }
            return false;
        }
    }
    return true;
}

bool
Dispatcher::handleResponse(PacketPtr pkt) {
    if (pkt->reqport == Packet::PortType::RemappingTable) {
        if (resp_rt_dram_blocked || resp_rt_hbm_blocked || !rt_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Remapping table is busy! Response blocked for addr %#x\n", pkt->getAddr());
            if (pkt->respport == Packet::PortType::PhysicalDram) {
                resp_rt_dram_blocked = true;
            } else if (pkt->respport == Packet::PortType::PhysicalHbm) {
                resp_rt_hbm_blocked = true;
            }
            return false;
        }
        return true;
    } else if (pkt->reqport == Packet::PortType::MigrationManager) {
        if (resp_mm_dram_blocked || resp_mm_hbm_blocked || !mm_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Remapping table is busy! Response blocked for addr %#x\n", pkt->getAddr());
            if (pkt->respport == Packet::PortType::PhysicalDram) {
                resp_mm_dram_blocked = true;
            } else if (pkt->respport == Packet::PortType::PhysicalHbm) {
                resp_mm_hbm_blocked = true;
            }
            return false;
        }
        return true;
    }
}

void
Dispatcher::handleReqRetry() {
    assert(req_ac_rt_blocked || req_ac_mm_blocked ||
           req_dram_rt_blocked || req_dram_mm_blocked ||
           req_hbm_rt_blocked || req_hbm_mm_blocked);

    if (req_ac_rt_blocked || req_dram_rt_blocked || req_hbm_rt_blocked) {
        req_ac_rt_blocked = false;
        req_dram_rt_blocked = false;
        req_hbm_rt_blocked = false;
        rt_side_port.trySendRetry();
    }
    if (req_ac_mm_blocked || req_dram_mm_blocked || req_hbm_mm_blocked) {
        req_ac_mm_blocked = false;
        req_dram_mm_blocked = false;
        req_hbm_mm_blocked = false;
        mm_side_port.trySendRetry();
    }
}

void
Dispatcher::handleRespRetry() {
    assert(resp_rt_dram_blocked || resp_rt_hbm_blocked ||
           resp_mm_dram_blocked || resp_mm_hbm_blocked);
    if (resp_rt_dram_blocked || resp_rt_hbm_blocked) {
        resp_rt_dram_blocked = false;
        resp_mm_dram_blocked = false;
        dram_side_port.trySendRetry();
    }
    if (resp_rt_hbm_blocked || resp_mm_hbm_blocked) {
        resp_rt_hbm_blocked = false;
        resp_mm_hbm_blocked = false;
        hbm_side_port.trySendRetry();
    }
}

Dispatcher::CpuSidePort::CpuSidePort(const std::string& _name,
                                     Dispatcher& _disp,
                                     Dispatcher::PortType _type)
    : ResponsePort(_name, &_disp),
      disp(_disp),
      porttype(_type),
      needRetry(false) {}

AddrRangeList
Dispatcher::CpuSidePort::getAddrRanges() const {
    return disp.getAddrRanges();
}

bool
Dispatcher::CpuSidePort::sendPacket(PacketPtr pkt) {
    // If we can't send the packet across the port, return false.
    if (!sendTimingResp(pkt)) {
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::CpuSidePort::trySendRetry()
{
    if (needRetry) {
        needRetry = false;
        DPRINTF(Dispatcher, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

Tick
Dispatcher::CpuSidePort::recvAtomic(PacketPtr pkt) {
    return disp.handleAtomic(pkt);
}

void
Dispatcher::CpuSidePort::recvFunctional(PacketPtr pkt) {
    disp.handleFunctional(pkt);
}

void
Dispatcher::CpuSidePort::setReqPort(PacketPtr pkt) {
    if (porttype == Dispatcher::PortType::RemappingTable) {
        pkt->reqport = Packet::PortType::RemappingTable;
    } else if (porttype == Dispatcher::PortType::MigrationManager) {
        pkt->reqport = Packet::PortType::MigrationManager;
    }
}

bool
Dispatcher::CpuSidePort::recvTimingReq(PacketPtr pkt) {
    setReqPort(pkt);
    if (!disp.handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::CpuSidePort::recvRespRetry() {
    assert(needRetry);
    disp.handleRespRetry();
}

Dispatcher::MemSidePort::MemSidePort(const std::string& _name,
                                     Dispatcher& _disp,
                                     Dispatcher::PortType _type)
    : RequestPort(_name, &_disp),
      disp(_disp),
      porttype(_type),
      needRetry(false) {}

bool
Dispatcher::MemSidePort::sendPacket(PacketPtr pkt) {
    // If we can't send the packet across the port, return false.
    if (!sendTimingReq(pkt)) {
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::MemSidePort::trySendRetry() {
    if (needRetry) {
        needRetry = false;
        DPRINTF(Dispatcher, "Sending retry req for %d\n", id);
        sendRetryResp();
    }
}

void
Dispatcher::MemSidePort::setRespPort(PacketPtr pkt) {
    if (porttype == Dispatcher::PortType::PhysicalDram) {
        pkt->respport = Packet::PortType::PhysicalDram;
    } else if (porttype == Dispatcher::PortType::PhysicalHbm) {
        pkt->respport = Packet::PortType::PhysicalHbm;
    }
}

bool
Dispatcher::MemSidePort::recvTimingResp(PacketPtr pkt) {
    setRespPort(pkt);
    if (!Dispatcher.handleResponse(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::MemSidePort::recvReqRetry() {
    assert(needRetry);
    disp.handleReqRetry();
}

void
Dispatcher::MemSidePort::recvRangeChange() {
    disp.sendRangeChange();
}