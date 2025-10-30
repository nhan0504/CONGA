#include "leafswitch.h"
#include <algorithm>
#include <limits>
#include <iostream>

// Congestion metric aging timeout (100ms)
#define METRIC_TIMEOUT_US 100000
#define UPDATE_INTERVAL_US 10000  // 10ms update interval

LeafSwitch::LeafSwitch(uint32_t leafId, uint32_t nCore, EventList &eventlist)
    : EventSource("leafswitch"), _leafId(leafId), _nCore(nCore), _eventlist(eventlist),
      _updateInterval(timeFromUs(UPDATE_INTERVAL_US)),
      _metricTimeout(timeFromUs(METRIC_TIMEOUT_US))
{
    _uplinks.reserve(nCore);
}

LeafSwitch::~LeafSwitch() {
    // Cleanup if needed
}

void LeafSwitch::addUplink(uint32_t coreId, Queue* queue, Pipe* pipe) {
    _uplinks.emplace_back(coreId, queue, pipe);
}

void LeafSwitch::addDownlink(uint32_t serverId, Queue* queue, Pipe* pipe) {
    _downlinks.emplace_back(serverId, queue, pipe);
}

uint32_t LeafSwitch::selectUplink(uint32_t dstLeaf, uint32_t /* flowId */) {
    // Age old metrics
    ageCongestionMetrics();
    
    // Find the best uplink based on congestion metrics
    return selectBestUplink(dstLeaf);
}

uint32_t LeafSwitch::selectBestUplink(uint32_t dstLeaf) {
    if (_uplinks.empty()) {
        return 0;
    }
    
    uint32_t bestUplink = 0;
    uint64_t minCongestion = std::numeric_limits<uint64_t>::max();
    
    // Check if we have metrics for this destination leaf
    auto it = _congestionToLeaf.find(dstLeaf);
    
    if (it != _congestionToLeaf.end()) {
        // We have congestion metrics, use them
        const auto& metrics = it->second;
        
        for (uint32_t i = 0; i < _uplinks.size(); i++) {
            uint64_t localCongestion = calculateLocalCongestion(i);
            uint64_t remoteCongestion = (i < metrics.size()) ? metrics[i].congestion : 0;
            
            // Total congestion = local + remote
            uint64_t totalCongestion = localCongestion + remoteCongestion;
            
            if (totalCongestion < minCongestion) {
                minCongestion = totalCongestion;
                bestUplink = i;
            }
        }
    } else {
        // No metrics available, use local congestion only
        for (uint32_t i = 0; i < _uplinks.size(); i++) {
            uint64_t localCongestion = calculateLocalCongestion(i);
            
            if (localCongestion < minCongestion) {
                minCongestion = localCongestion;
                bestUplink = i;
            }
        }
    }
    
    return bestUplink;
}

uint64_t LeafSwitch::calculateLocalCongestion(uint32_t uplinkId) {
    if (uplinkId >= _uplinks.size()) {
        return 0;
    }
    
    // Use queue occupancy as congestion metric
    Queue* queue = _uplinks[uplinkId].queue;
    if (queue) {
        return queue->_queuesize;  // Access public member directly
    }
    
    return 0;
}

void LeafSwitch::updateCongestionToLeaf(uint32_t dstLeaf, uint32_t uplinkId, uint64_t congestion) {
    simtime_picosec now = _eventlist.now();
    
    // Initialize vector for this destination if needed
    if (_congestionToLeaf.find(dstLeaf) == _congestionToLeaf.end()) {
        _congestionToLeaf[dstLeaf].resize(_nCore);
    }
    
    if (uplinkId < _congestionToLeaf[dstLeaf].size()) {
        _congestionToLeaf[dstLeaf][uplinkId].congestion = congestion;
        _congestionToLeaf[dstLeaf][uplinkId].lastUpdate = now;
    }
}

void LeafSwitch::updateCongestionFromLeaf(uint32_t srcLeaf, uint64_t congestion) {
    simtime_picosec now = _eventlist.now();
    
    _congestionFromLeaf[srcLeaf].congestion = congestion;
    _congestionFromLeaf[srcLeaf].lastUpdate = now;
}

uint64_t LeafSwitch::getCongestionToLeaf(uint32_t dstLeaf, uint32_t uplinkId) {
    auto it = _congestionToLeaf.find(dstLeaf);
    if (it != _congestionToLeaf.end() && uplinkId < it->second.size()) {
        return it->second[uplinkId].congestion;
    }
    return 0;
}

uint64_t LeafSwitch::getCongestionFromLeaf(uint32_t srcLeaf) {
    auto it = _congestionFromLeaf.find(srcLeaf);
    if (it != _congestionFromLeaf.end()) {
        return it->second.congestion;
    }
    return 0;
}

void LeafSwitch::ageCongestionMetrics() {
    simtime_picosec now = _eventlist.now();
    
    // Age congestion-to-leaf metrics
    for (auto& leafEntry : _congestionToLeaf) {
        for (auto& metric : leafEntry.second) {
            if (now - metric.lastUpdate > _metricTimeout) {
                // Metric is stale, reset it
                metric.congestion = 0;
            }
        }
    }
    
    // Age congestion-from-leaf metrics
    for (auto& entry : _congestionFromLeaf) {
        if (now - entry.second.lastUpdate > _metricTimeout) {
            entry.second.congestion = 0;
        }
    }
}

void LeafSwitch::forwardPacketUp(Packet* pkt, uint32_t uplinkId) {
    if (uplinkId >= _uplinks.size()) {
        return;
    }
    
    // Forward packet to the selected uplink
    Uplink& uplink = _uplinks[uplinkId];
    
    // Send to queue
    if (uplink.queue) {
        uplink.queue->receivePacket(*pkt);
    }
}

void LeafSwitch::forwardPacketDown(Packet* pkt, uint32_t serverId) {
    // Find the downlink for this server
    for (auto& downlink : _downlinks) {
        if (downlink.serverId == serverId) {
            if (downlink.queue) {
                downlink.queue->receivePacket(*pkt);
            }
            return;
        }
    }
}

void LeafSwitch::doNextEvent() {
    // This is called when a scheduled event fires
    // For now, we don't use scheduled events in LeafSwitch
    // Could be used for periodic congestion updates in the future
}