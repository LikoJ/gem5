#include "base/trace.hh"
#include "mem/flat_mem.hh"
#include "debug/FlatMemory.hh"

FlatMemory::FlatMemory(const FlatMemoryParams &params)
    : SimObject(params),
      bus_side_port(name() + ".bus_side_port", *this),
      mem_side_port(name() + ".mem_side_port", *this),
      bus_side_blocked(false) {}

Port& 
FlatMemory::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "bus_side_port") {
        return bus_side_port;
    } else if (if_name == "mem_side_port") {
        return mem_side_port;
    } else {
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList
FlatMemory::getAddrRanges() const {
    DPRINTF(FlatMemory, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return mem_side_port.getAddrRanges();
}

Tick
FlatMemory::handleAtomic(PacketPtr pkt) {
    return mem_side_port.sendAtomic(pkt);
}

void
FlatMemory::handleFunctional(PacketPtr pkt) {
    mem_side_port.sendFunctional(pkt);
}

bool
FlatMemory::handleRequest(PacketPtr pkt) {
    if (bus_side_blocked) {
        return false;
    }

    DPRINTF(FlatMemory, "Got request for addr %#x\n", pkt->getAddr());

    // Simply forward to the memory port
    if (!mem_side_port.sendPacket(pkt)) {
        bus_side_blocked = true;
        return false;
    } else {
        return true;
    }
}

bool
FlatMemory::handleResponse(PacketPtr pkt) {
    if (mem_side_blocked) {
        return false;
    }
    DPRINTF(FlatMemory, "Got response for addr %#x\n", pkt->getAddr());

    // Simply forward to the bus port
    if (!bus_side_port.sendPacket(pkt)) {
        mem_side_blocked = false;
        return false;
    } else {
        return true;
    }
}

void
FlatMemory::handleReqRetry(PacketPtr pkt) {
    assert(bus_side_blocked);
    DPRINTF(FlatMemory, "Retry request for addr %#x\n", pkt->getAddr());
    if (!mem_side_port.sendPacket(pkt)) {
        bus_side_blocked = true;
    } else {
        bus_side_port.trySendRetry();
        bus_side_blocked = false;
    }
}

void
FlatMemory::handleRespRetry(PacketPtr pkt) {
    assert(mem_side_blocked);
    DPRINTF(FlatMemory, "Retry response for addr %#x\n", pkt->getAddr());
    if (!bus_side_port.sendPacket(pkt)) {
        mem_side_blocked = true;
    } else {
        mem_side_port.trySendRetry();
        mem_side_blocked = false;
    }
}

void
FlatMemory::sendRangeChange() {
    bus_side_port.sendRangeChange();
}

FlatMemory::BusSidePort::BusSidePort(const std::string& _name,
                                     FlatMemory& _flatmem)
    : ResponsePort(_name, &_flatmem),
      flatmem(_flatmem),
      needRetry(false),
      blockedPacket(nullptr) {}

AddrRangeList
FlatMemory::BusSidePort::getAddrRanges() const {
    return flatmem.getAddrRanges();
}

bool
FlatMemory::BusSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
        return false;
    } else {
        return true;
    }
}

void
FlatMemory::BusSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(FlatMemory, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

Tick
FlatMemory::BusSidePort::recvAtomic(PacketPtr pkt) {
    return flatmem.handleAtomic(pkt);
}

void
FlatMemory::BusSidePort::recvFunctional(PacketPtr pkt) {
    flatmem.handleFunctional(pkt);
}

bool
FlatMemory::BusSidePort::recvTimingReq(PacketPtr pkt) {
    if (!flatmem.handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
FlatMemory::BusSidePort::recvRespRetry() {
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    flatmem.handleRespRetry(pkt);
}

FlatMemory::MemSidePort::MemSidePort(const std::string& _name,
                                     FlatMemory& _flatmem)
    : RequestPort(_name, &_flatmem),
      flatmem(_flatmem),
      blockedPacket(nullptr) {}

bool
FlatMemory::MemSidePort::sendPacket(PacketPtr pkt) {
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        return false;
    } else {
        return true;
    }
}

void
FlatMemory::MemSidePort::trySendRetry() {
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(FlatMemory, "Sending retry req for %d\n", id);
        sendRetryResp();
    }
}

bool
FlatMemory::MemSidePort::recvTimingResp(PacketPtr pkt) {
    // Just forward to the memobj.
    if (!flatmem.handleResponse(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
FlatMemory::MemSidePort::recvReqRetry() {
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    flatmem.handleReqRetry(pkt);
}

void
FlatMemory::MemSidePort::recvRangeChange() {
    flatmem.sendRangeChange();
}