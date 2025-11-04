// leafswitch.h
#pragma once
#include <vector>
#include <cstdint>
#include <random>
#include "eventlist.h"
#include "queue.h"
#include "pipe.h"

class LeafSwitch : public EventSource {
public:
    LeafSwitch(uint32_t leaf_id,
               uint32_t n_cores,
               uint32_t n_leaves,
               EventList& ev);

    void addUplink(uint32_t core, Queue* q_leaf_to_core, Pipe* p_leaf_to_core);
    void addDownlink(uint32_t /*server_global_id*/, Queue* /*q*/, Pipe* /*p*/);

    // Remote segment registration: core -> dstLeaf downlink
    void registerCoreToLeaf(uint32_t core, uint32_t dstLeaf, Queue* q_core_to_leaf, Pipe* p_core_to_leaf);

    // Pick the core uplink for a packet going to dstLeaf
    uint32_t chooseCore(uint32_t dstLeaf) const;

    // Tunables
    void setSamplingPeriod(simtime_picosec T) { _samplePeriod = T; }
    void setAlpha(double a) { _alpha = a; }
    void setWeights(double w_to, double w_from) { _w_to = w_to; _w_from = w_from; }
    void setEps(double eps) { _eps = eps; }

    void doNextEvent() override;

private:
    void sampleOnce();

    uint32_t _leafId, _nCores, _nLeaves;

    // leaf -> core (local uplinks)
    std::vector<Queue*> _uplinkQ; // size nCores

    // core -> leaf queues indexed [dstLeaf][core]
    std::vector<std::vector<Queue*>> _coreToLeafQ;

    // CONGA-style tables (EWMA of bytes-in-queue)
    // toLeaf[dst][core]   := local leaf->core congestion toward dst leaf
    // fromLeaf[dst][core] := remote core->dstLeaf congestion learned/sampled
    std::vector<std::vector<double>> _toLeaf;
    std::vector<std::vector<double>> _fromLeaf;

    // Combined metric used for routing: metric = w_to*toLeaf + w_from*fromLeaf (+ jitter)
    std::vector<std::vector<double>> _metric;

    // EWMA, period, weights, tie threshold
    double _alpha;
    simtime_picosec _samplePeriod;
    double _w_to, _w_from;
    double _eps;

    // Jitter to break symmetry
    mutable std::mt19937 _rng;
};
