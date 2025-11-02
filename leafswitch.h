// leafswitch.h
#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "eventlist.h"
#include "queue.h"
#include "pipe.h"

/**
 * Simplified LeafSwitch class for CONGA simulation.
 * Each leaf maintains congestion metrics per destination leaf and core uplink.
 * It periodically samples queue occupancies to emulate CONGA feedback.
 */
class LeafSwitch : public EventSource {
public:
    LeafSwitch(uint32_t leaf_id,
               uint32_t n_cores,
               uint32_t n_leaves,
               EventList& ev);

    // Connectors
    void addUplink(uint32_t core, Queue* q_leaf_to_core, Pipe* p_leaf_to_core);
    void addDownlink(uint32_t server_global_id, Queue* q_leaf_to_server, Pipe* p_leaf_to_server);

    // Register core→leaf downlink queues so this leaf can estimate remote congestion.
    void registerCoreToLeaf(uint32_t core, uint32_t dstLeaf, Queue* q_core_to_leaf, Pipe* p_core_to_leaf);

    // Choose uplink (core index) for packets toward dstLeaf.
    uint32_t chooseCore(uint32_t dstLeaf) const;

    // Tuning parameters
    void setSamplingPeriod(simtime_picosec T) { _samplePeriod = T; }
    void setAlpha(double a) { _alpha = a; }

    // EventSource tick
    void doNextEvent() override;

private:
    void sampleOnce();
    void ensureStarted();

    // === internal state ===
    uint32_t _leafId;
    uint32_t _nCores;
    uint32_t _nLeaves;

    // leaf → core uplinks (only queues matter for congestion sampling)
    std::vector<Queue*> _uplinkQ;

    // core → leaf queues for each destination leaf: [dstLeaf][core]
    std::vector<std::vector<Queue*>> _coreToLeafQ;

    // congestion metric [dstLeaf][core]
    std::vector<std::vector<double>> _metric;

    // EWMA & scheduling
    double _alpha;
    simtime_picosec _samplePeriod;
    bool _started;
};
