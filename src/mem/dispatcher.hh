/**
 * @file
 * Dispatcher
 */
#ifndef __MEM_DISPATCHER_HH__
#define __MEM_DISPATCHER_HH__

#include "mem/port.hh"
#include "params/Dispatcher.hh"
#include "sim/sim_object.hh"

class Dispatcher : public SimObject {
  public:
    enum PortType {
      RemappingTable,
      MigrationManager,
      AccessCounter,
      PhysicalDram,
      PhysicalHbm
    };
    Dispatcher(const DispatcherParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

  private:
    class CpuSidePort : public ResponsePort {
      private:
        Dispatcher& disp;
        bool needRetry;
        PortType porttype;
        void setReqPort(PacketPtr pkt);
      public:
        CpuSidePort(const std::string& _name, Dispatcher& _disp, Dispatcher::PortType _type);
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
        Dispatcher& disp;
        bool needRetry;
        PortType porttype;
        void setRespPort(PacketPtr pkt);
      public:
        MemSidePort(const std::string& _name, Dispatcher& _disp, Dispatcher::PortType _type);
        bool sendPacket(PacketPtr pkt);
        void trySendRetry();
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    CpuSidePort rt_side_port;   // connect to remapping table
    CpuSidePort mm_side_port;   // connect to migration manager

    MemSidePort ac_side_port;   // connect to access counter
    MemSidePort hbm_side_port;  // connect to physical hbm
    MemSidePort dram_side_port; // connect to physical dram

    bool req_ac_rt_blocked;     // access counter is busy, request from rt
    bool req_ac_mm_blocked;     // access counter is busy, request from mm
    bool req_hbm_rt_blocked;    // physical hbm is busy, request from rt
    bool req_hbm_mm_blocked;    // physical hbm is busy, request from mm
    bool req_dram_rt_blocked;   // physical dram is busy, request from rt
    bool req_dram_mm_blocked;   // physical dram is busy, request from mm

    bool resp_rt_dram_blocked;  // remapping table is busy, response from dram
    bool resp_rt_hbm_blocked;   // remapping table is busy, response from hbm
    bool resp_mm_dram_blocked;  // migration manager is busy, response from dram
    bool resp_mm_hbm_blocked;   // migration manager is busy, response from hbm

    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    Tick handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
    void handleReqRetry();
    void handleRespRetry();
    bool isDramOrHbm(PacketPtr pkt);
};

#endif