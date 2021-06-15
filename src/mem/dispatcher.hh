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

    enum BlockType {
      Rt2Ac,    // Request packet from Remmaping Table   blocks Acess counter
      // Mm2Ac,    // Request packet from Migration Manager blocks Acess counter
      Rt2Hbm,   // Request packet from Remmaping Table   blocks Physical Hbm
      Mm2Hbm,   // Request packet from Migration Manager blocks Physical Hbm
      Rt2Dram,  // Request packet from Remmaping Table   blocks Physical Dram
      Mm2Dram,  // Request packet from Migration Manager blocks Physical Dram

      Dram2Rt,  // Response packet from Physical Dram    blocks Remmaping Table
      Hbm2Rt,   // Response packet from Physical Hbm     blocks Remmaping Table
      Dram2Mm,  // Response packet from Physical Dram    blocks Migration Manager
      Hbm2Mm,   // Response packet from Physical Hbm     blocks Migration Manager

      BlockTypeSize
    };
    
    Dispatcher(const DispatcherParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

  private:
    class CpuSidePort : public ResponsePort {
      private:
        Dispatcher& disp;
        bool needRetry;
        bool blocked;
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
        bool blocked;
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

    bool blocked[BlockType::BlockTypeSize];

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