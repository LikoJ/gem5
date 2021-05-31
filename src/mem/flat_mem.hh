/**
 * @file
 * FlatMemory
 */
#ifndef __MEM_FLATMEMORY_HH__
#define __MEM_FLATMEMORY_HH__

#include "mem/port.hh"
#include "params/FlatMemory.hh"
#include "sim/sim_object.hh"

class FlatMemory : public SimObject {
  private:

    class BusSidePort : public ResponsePort {
      private:
        FlatMemory& flatmem;
        bool needRetry;
        PacketPtr blockedPacket;
      public:
        BusSidePort(const std::string& _name, FlatMemory& _flatmem);
        AddrRangeList getAddrRanges() const override;
        void sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    class MemSidePort : public RequestPort {
      private:
        FlatMemory& flatmem;
        PacketPtr blockedPacket;
      public:
        MemSidePort(const std::string& _name, FlatMemory& _flatmem);
        void sendPacket(PacketPtr pkt);
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvRespRetry() override;
        void recvRangeChange() override;
    };

    BusSidePort bus_side_port;
    MemSidePort mem_side_port;

    bool bus_side_blocked;

    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic()(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);

  public:
    FlatMemory(FlatMemoryParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
};

#endif