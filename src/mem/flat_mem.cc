#include "base/trace.hh"
#include "mem/flat_mem.hh"
#include "debug/FlatMemory.hh"

FlatMemory::FlatMemory(const FlatMemoryParams &params)
    : SimObject(params),
      bus_side_port(name() + ".bus_side_port", *this),
      mem_side_port(name() + ".mem_side_port", *this),
      bus_side_blocked(false),
      event([this]{processEvent();}, name()) {}

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
    // DPRINTF(FlatMemory, "Sending new ranges\n");
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
        DPRINTF(FlatMemory, "Request blocked directly for addr %#x\n", pkt->getAddr());
        return false;
    }
    if (curTick() != lastTick) {
        DPRINTF(FlatMemory, "Req schedule\n");
        lastTick = curTick();
        schedule(event, curTick());
    }
    // DPRINTF(FlatMemory, "Got request for addr %#x\n", pkt->getAddr());

    // Simply forward to the memory port
    if (!mem_side_port.sendPacket(pkt)) {
        DPRINTF(FlatMemory, "Request blocked for addr %#x\n", pkt->getAddr());
        bus_side_blocked = true;
        return false;
    }
    return true;
}

bool
FlatMemory::handleResponse(PacketPtr pkt) {
    if (mem_side_blocked) {
        DPRINTF(FlatMemory, "Response blocked directly for addr %#x\n", pkt->getAddr());
        return false;
    }
    if (curTick() != lastTick) {
        DPRINTF(FlatMemory, "Req schedule\n");
        lastTick = curTick();
        schedule(event, curTick());
    }
    // DPRINTF(FlatMemory, "Got response for addr %#x\n", pkt->getAddr());

    // Simply forward to the bus port
    if (!bus_side_port.sendPacket(pkt)) {
        DPRINTF(FlatMemory, "Response blocked for addr %#x\n", pkt->getAddr());
        mem_side_blocked = true;
        return false;
    }
    return true;
}

void
FlatMemory::handleReqRetry() {
    assert(bus_side_blocked);
    DPRINTF(FlatMemory, "Retry request\n");

    bus_side_blocked = false;
    bus_side_port.trySendRetry();
}

void
FlatMemory::handleRespRetry() {
    assert(mem_side_blocked);
    DPRINTF(FlatMemory, "Retry response\n");

    mem_side_blocked = false;
    mem_side_port.trySendRetry();

}

void
FlatMemory::sendRangeChange() {
    bus_side_port.sendRangeChange();
}

void
FlatMemory::processEvent() {
    DPRINTF(FlatMemory, "Event process!\n");
}

FlatMemory::BusSidePort::BusSidePort(const std::string& _name,
                                     FlatMemory& _flatmem)
    : ResponsePort(_name, &_flatmem),
      flatmem(_flatmem),
      needRetry(false),
      blocked(false) {}

AddrRangeList
FlatMemory::BusSidePort::getAddrRanges() const {
    return flatmem.getAddrRanges();
}

bool
FlatMemory::BusSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blocked, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        blocked = true;
        return false;
    } else {
        return true;
    }
}

void
FlatMemory::BusSidePort::trySendRetry()
{
    if (needRetry && !blocked) {
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
    assert(blocked);

    blocked = false;

    // Try to resend it. It's possible that it fails again.
    flatmem.handleRespRetry();
}

FlatMemory::MemSidePort::MemSidePort(const std::string& _name,
                                     FlatMemory& _flatmem)
    : RequestPort(_name, &_flatmem),
      flatmem(_flatmem),
      blocked(false) {}

bool
FlatMemory::MemSidePort::sendPacket(PacketPtr pkt) {
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blocked, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blocked = true;
        return false;
    } else {
        return true;
    }
}

void
FlatMemory::MemSidePort::trySendRetry() {
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(FlatMemory, "Sending retry resp for %d\n", id);
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
    assert(blocked);

    // Grab the blocked packet.
    blocked = false;

    flatmem.handleReqRetry();
}

void
FlatMemory::MemSidePort::recvRangeChange() {
    flatmem.sendRangeChange();
}