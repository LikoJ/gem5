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
      event([this]{processEvent();}, name()) {

    for (int i = 0; i < BlockType::BlockTypeSize; i++) {
        blocked[i] = false;
    }
}

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
    DPRINTF(Dispatcher, "Req schedule!");
    schedule(event, 100);
    PacketPtr acpkt = new Packet(pkt, false, true);
    acpkt->reqport = pkt->reqport;
    acpkt->respport = pkt->respport;
    if (isDramOrHbm(pkt)) {
        if (blocked[BlockType::Rt2Dram] || blocked[BlockType::Mm2Dram] || !dram_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Physical DRAM is busy! Request blocked for addr %#x\n", pkt->getAddr());
            if (pkt->reqport == Packet::PortType::RemappingTable) {
                blocked[BlockType::Rt2Dram] = true;
            } else if (pkt->reqport == Packet::PortType::MigrationManager) {
                blocked[BlockType::Mm2Dram] = true;
            }
            return false;
        }
    } else {
        if (blocked[BlockType::Rt2Hbm] || blocked[BlockType::Mm2Hbm] || !hbm_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Physical HBM is busy! Request blocked for addr %#x\n", pkt->getAddr());
            if (pkt->reqport == Packet::PortType::RemappingTable) {
                blocked[BlockType::Rt2Hbm] = true;
            } else if (pkt->reqport == Packet::PortType::MigrationManager) {
                blocked[BlockType::Mm2Hbm] = true;
            }
            return false;
        }
    }
    if (acpkt->reqport == Packet::PortType::RemappingTable && (blocked[BlockType::Rt2Ac] || !ac_side_port.sendPacket(acpkt))) {
        DPRINTF(Dispatcher, "Access Counter is busy! Request blocked for addr %#x\n", acpkt->getAddr());
        if (acpkt->reqport == Packet::PortType::RemappingTable) {
            blocked[BlockType::Rt2Ac] = true;
        }
        return false;
    }
    return true;
}

bool
Dispatcher::handleResponse(PacketPtr pkt) {
    DPRINTF(Dispatcher, "Resp schedule!");
    schedule(event, 100);
    if (pkt->reqport == Packet::PortType::RemappingTable) {
        if (blocked[BlockType::Dram2Rt] || blocked[BlockType::Hbm2Rt] || !rt_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Remapping table is busy! Response blocked for addr %#x\n", pkt->getAddr());
            if (pkt->respport == Packet::PortType::PhysicalDram) {
                blocked[BlockType::Dram2Rt] = true;
            } else if (pkt->respport == Packet::PortType::PhysicalHbm) {
                blocked[BlockType::Hbm2Rt] = true;
            }
            return false;
        }
    } else if (pkt->reqport == Packet::PortType::MigrationManager) {
        if (blocked[BlockType::Dram2Mm] || blocked[BlockType::Hbm2Mm] || !mm_side_port.sendPacket(pkt)) {
            DPRINTF(Dispatcher, "Remapping table is busy! Response blocked for addr %#x\n", pkt->getAddr());
            if (pkt->respport == Packet::PortType::PhysicalDram) {
                blocked[BlockType::Dram2Mm] = true;
            } else if (pkt->respport == Packet::PortType::PhysicalHbm) {
                blocked[BlockType::Hbm2Mm] = true;
            }
            return false;
        }
    }
    return true;
}

void
Dispatcher::handleReqRetry() {
    assert(blocked[BlockType::Rt2Ac] ||
           blocked[BlockType::Rt2Dram] || blocked[BlockType::Mm2Dram] ||
           blocked[BlockType::Rt2Hbm] || blocked[BlockType::Mm2Hbm]);

    if (blocked[BlockType::Rt2Ac] || blocked[BlockType::Rt2Dram] || blocked[BlockType::Rt2Hbm]) {
        blocked[BlockType::Rt2Ac] = false;
        blocked[BlockType::Rt2Dram] = false;
        blocked[BlockType::Rt2Hbm] = false;
        rt_side_port.trySendRetry();
    }
    if (blocked[BlockType::Mm2Dram] || blocked[BlockType::Mm2Hbm]) {
        blocked[BlockType::Mm2Dram] = false;
        blocked[BlockType::Mm2Hbm] = false;
        mm_side_port.trySendRetry();
    }
}

void
Dispatcher::handleRespRetry() {
    assert(blocked[BlockType::Dram2Rt] || blocked[BlockType::Hbm2Rt] ||
           blocked[BlockType::Dram2Mm] || blocked[BlockType::Hbm2Mm]);
    if (blocked[BlockType::Dram2Rt] || blocked[BlockType::Dram2Mm]) {
        blocked[BlockType::Dram2Rt] = false;
        blocked[BlockType::Dram2Mm] = false;
        dram_side_port.trySendRetry();
    }
    if (blocked[BlockType::Hbm2Rt] || blocked[BlockType::Hbm2Mm]) {
        blocked[BlockType::Hbm2Rt] = false;
        blocked[BlockType::Hbm2Mm] = false;
        hbm_side_port.trySendRetry();
    }
}

void
Dispatcher::processEvent() {
    std::cout << "event!" << std::endl;
    DPRINTF(Dispatcher, "Event process!");
}

Dispatcher::CpuSidePort::CpuSidePort(const std::string& _name,
                                     Dispatcher& _disp,
                                     Dispatcher::PortType _type)
    : ResponsePort(_name, &_disp),
      disp(_disp),
      porttype(_type),
      needRetry(false),
      blocked(false) {}

AddrRangeList
Dispatcher::CpuSidePort::getAddrRanges() const {
    return disp.getAddrRanges();
}

bool
Dispatcher::CpuSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blocked, "Should never try to send if blocked!");
    // If we can't send the packet across the port, return false.
    if (!sendTimingResp(pkt)) {
        blocked = true;
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::CpuSidePort::trySendRetry()
{
    if (needRetry && !blocked) {
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
    assert(blocked);
    blocked = false;
    disp.handleRespRetry();
}

Dispatcher::MemSidePort::MemSidePort(const std::string& _name,
                                     Dispatcher& _disp,
                                     Dispatcher::PortType _type)
    : RequestPort(_name, &_disp),
      disp(_disp),
      porttype(_type),
      needRetry(false),
      blocked(false) {}

bool
Dispatcher::MemSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blocked, "Should never try to send if blocked!");
    // If we can't send the packet across the port, return false.
    if (!sendTimingReq(pkt)) {
        blocked = true;
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::MemSidePort::trySendRetry() {
    if (needRetry && !blocked) {
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
    if (!disp.handleResponse(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
Dispatcher::MemSidePort::recvReqRetry() {
    assert(blocked);
    blocked = false;
    disp.handleReqRetry();
}

void
Dispatcher::MemSidePort::recvRangeChange() {
    disp.sendRangeChange();
}