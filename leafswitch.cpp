// leafswitch.cpp
#include "leafswitch.h"
#include <cassert>
#include <limits>
#include <algorithm>

static inline double ewma(double oldv, double newv, double alpha) {
    return alpha * newv + (1.0 - alpha) * oldv;
}

LeafSwitch::LeafSwitch(uint32_t leaf_id,
                       uint32_t n_cores,
                       uint32_t n_leaves,
                       EventList& /*ev*/)
    : EventSource("LeafSwitch"),
      _leafId(leaf_id),
      _nCores(n_cores),
      _nLeaves(n_leaves),
      _uplinkQ(n_cores, nullptr),
      _coreToLeafQ(n_leaves, std::vector<Queue*>(n_cores, nullptr)),
      _toLeaf(n_leaves, std::vector<double>(n_cores, 0.0)),
      _fromLeaf(n_leaves, std::vector<double>(n_cores, 0.0)),
      _metric(n_leaves, std::vector<double>(n_cores, 0.0)),
      _alpha(0.6),                             // faster tracking than 0.25
      _samplePeriod(timeFromUs(5)),            // 5 µs sampling (was 50 µs)
      _w_to(0.5), _w_from(0.5),                // equal weight by default
      _eps(1e-3),
      _rng(0xBADA551u + leaf_id)               // leaf-unique seed
{
    // Symmetry-breaking jitter so early ties don't stick to core 0
    std::uniform_real_distribution<double> J(0.0, 1e-2);
    for (uint32_t d = 0; d < _nLeaves; ++d) {
        for (uint32_t c = 0; c < _nCores; ++c) {
            _metric[d][c] = J(_rng);
        }
    }

    // Kick off periodic sampling
    EventList::Get().sourceIsPending(*this, EventList::Get().now());
}

void LeafSwitch::addUplink(uint32_t core, Queue* q, Pipe* /*p*/) {
    assert(core < _nCores);
    _uplinkQ[core] = q;
}

void LeafSwitch::addDownlink(uint32_t /*sid*/, Queue* /*q*/, Pipe* /*p*/) {
    // not needed for path choice in this simplified model
}

void LeafSwitch::registerCoreToLeaf(uint32_t core,
                                    uint32_t dstLeaf,
                                    Queue* q, Pipe* /*p*/)
{
    assert(core < _nCores && dstLeaf < _nLeaves);
    _coreToLeafQ[dstLeaf][core] = q;
}

uint32_t LeafSwitch::chooseCore(uint32_t dstLeaf) const {
    assert(dstLeaf < _nLeaves);

    // Find minimum metric
    double best = std::numeric_limits<double>::infinity();
    for (uint32_t c = 0; c < _nCores; ++c)
        best = std::min(best, _metric[dstLeaf][c]);

    // Collect all cores within epsilon of best and choose uniformly at random
    std::vector<uint32_t> cand;
    cand.reserve(_nCores);
    for (uint32_t c = 0; c < _nCores; ++c)
        if (_metric[dstLeaf][c] <= best + _eps) cand.push_back(c);

    std::uniform_int_distribution<size_t> U(0, cand.size() - 1);
    return cand[U(_rng)];
}

void LeafSwitch::doNextEvent() {
    sampleOnce();
    EventList::Get().sourceIsPendingRel(*this, _samplePeriod);
}

void LeafSwitch::sampleOnce() {
    // For each dst leaf and core, get local uplink & remote downlink occupancies
    for (uint32_t dst = 0; dst < _nLeaves; ++dst) {
        for (uint32_t c = 0; c < _nCores; ++c) {
            Queue* q_up   = _uplinkQ[c];
            Queue* q_down = _coreToLeafQ[dst][c];
            if (!q_up || !q_down) continue;

            // Your Queue exposes current occupancy via public _queuesize (bytes)
            double to_val   = (double)q_up->_queuesize;
            double from_val = (double)q_down->_queuesize;

            _toLeaf[dst][c]   = ewma(_toLeaf[dst][c],   to_val,   _alpha);
            _fromLeaf[dst][c] = ewma(_fromLeaf[dst][c], from_val, _alpha);

            // Combined DRE-like metric
            _metric[dst][c] = _w_to * _toLeaf[dst][c] + _w_from * _fromLeaf[dst][c];
        }
    }
}
