#ifndef LEAF_SWITCH_H
#define LEAF_SWITCH_H

#include <vector>
#include <unordered_map>
#include "queue.h"
#include "pipe.h"
#include "datapacket.h"
#include "eventlist.h"

// Congestion metric for a path
struct CongestionMetric {
    uint64_t congestion;      // Measured congestion (e.g., queue occupancy)
    simtime_picosec lastUpdate; // Last time this metric was updated
    
    CongestionMetric() : congestion(0), lastUpdate(0) {}
};

class LeafSwitch : public EventSource {
public:
    LeafSwitch(uint32_t leafId, uint32_t nCore, EventList &eventlist);
    ~LeafSwitch();
    
    // Add uplink to a core switch
    void addUplink(uint32_t coreId, Queue* queue, Pipe* pipe);
    
    // Add downlink to a server
    void addDownlink(uint32_t serverId, Queue* queue, Pipe* pipe);
    
    // Select best uplink for a packet destined to dstLeaf
    uint32_t selectUplink(uint32_t dstLeaf, uint32_t flowId);
    
    // Update congestion metrics based on packet feedback
    void updateCongestionToLeaf(uint32_t dstLeaf, uint32_t uplinkId, uint64_t congestion);
    void updateCongestionFromLeaf(uint32_t srcLeaf, uint64_t congestion);
    
    // Get congestion metric for a specific path
    uint64_t getCongestionToLeaf(uint32_t dstLeaf, uint32_t uplinkId);
    uint64_t getCongestionFromLeaf(uint32_t srcLeaf);
    
    // Forward packet through selected uplink
    void forwardPacketUp(Packet* pkt, uint32_t uplinkId);
    
    // Forward packet down to server
    void forwardPacketDown(Packet* pkt, uint32_t serverId);
    
    // EventSource interface
    void doNextEvent();
    
    // Getters
    uint32_t getLeafId() const { return _leafId; }
    uint32_t getNumUplinks() const { return _uplinks.size(); }
    
    // Route structure for uplink
    struct Uplink {
        uint32_t coreId;
        Queue* queue;
        Pipe* pipe;
        uint64_t bytesQueued;  // Local congestion metric
        
        Uplink(uint32_t id, Queue* q, Pipe* p) 
            : coreId(id), queue(q), pipe(p), bytesQueued(0) {}
    };
    
    // Route structure for downlink
    struct Downlink {
        uint32_t serverId;
        Queue* queue;
        Pipe* pipe;
        
        Downlink(uint32_t id, Queue* q, Pipe* p)
            : serverId(id), queue(q), pipe(p) {}
    };
    
private:
    uint32_t _leafId;
    uint32_t _nCore;
    EventList& _eventlist;
    
    // Uplinks to core switches
    std::vector<Uplink> _uplinks;
    
    // Downlinks to servers
    std::vector<Downlink> _downlinks;
    
    // Congestion-To-Leaf Table: [dstLeaf][uplinkId] -> congestion metric
    std::unordered_map<uint32_t, std::vector<CongestionMetric>> _congestionToLeaf;
    
    // Congestion-From-Leaf Table: [srcLeaf] -> congestion metric
    std::unordered_map<uint32_t, CongestionMetric> _congestionFromLeaf;
    
    // Aging timer for congestion metrics
    simtime_picosec _updateInterval;
    simtime_picosec _metricTimeout;
    
    // Helper functions
    void ageCongestionMetrics();
    uint32_t selectBestUplink(uint32_t dstLeaf);
    uint64_t calculateLocalCongestion(uint32_t uplinkId);
};

// CONGA packet extensions - add these fields to your packet header
struct CongaMetadata {
    uint32_t srcLeaf;           // Source leaf switch ID
    uint32_t dstLeaf;           // Destination leaf switch ID
    uint32_t selectedUplink;    // Which core switch was used
    uint64_t congestionMetric;  // Accumulated congestion along path
    bool isFeedback;            // Is this a feedback packet?
    
    CongaMetadata() : srcLeaf(0), dstLeaf(0), selectedUplink(0), 
                      congestionMetric(0), isFeedback(false) {}
};

#endif // LEAF_SWITCH_H