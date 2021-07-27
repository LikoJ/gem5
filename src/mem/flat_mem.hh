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
        bool blocked;
      public:
        BusSidePort(const std::string& _name, FlatMemory& _flatmem);
        AddrRangeList getAddrRanges() const override;
        bool sendPacket(PacketPtr pkt);
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
        bool needRetry;
        bool blocked;
      public:
        MemSidePort(const std::string& _name, FlatMemory& _flatmem);
        bool sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    BusSidePort bus_side_port;
    MemSidePort mem_side_port;

    bool bus_side_blocked;
    bool mem_side_blocked;

    EventFunctionWrapper event;

    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
    void handleReqRetry();
    void handleRespRetry();

    void processEvent();

  public:
    FlatMemory(const FlatMemoryParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
};

#endif