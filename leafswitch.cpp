#include "leafswitch.h"
#include <cassert>
#include <limits>

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
      _metric(n_leaves, std::vector<double>(n_cores, 0.0)),
      _alpha(0.25),
      _samplePeriod(timeFromUs(50)),
      _started(false)
{
    // Schedule first sampling event
    EventList::Get().sourceIsPending(*this, EventList::Get().now());
}

void LeafSwitch::addUplink(uint32_t core, Queue* q, Pipe* /*p*/) {
    assert(core < _nCores);
    _uplinkQ[core] = q;
}

void LeafSwitch::addDownlink(uint32_t /*sid*/, Queue* /*q*/, Pipe* /*p*/) {
    // not used in simplified CONGA
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
    double best = std::numeric_limits<double>::infinity();
    uint32_t bestCore = 0;
    for (uint32_t c = 0; c < _nCores; ++c) {
        double m = _metric[dstLeaf][c];
        if (m < best) {
            best = m;
            bestCore = c;
        }
    }
    return bestCore;
}

void LeafSwitch::doNextEvent() {
    sampleOnce();
    EventList::Get().sourceIsPendingRel(*this, _samplePeriod);
}

void LeafSwitch::sampleOnce() {
    for (uint32_t dst = 0; dst < _nLeaves; ++dst) {
        for (uint32_t c = 0; c < _nCores; ++c) {
            Queue* q1 = _uplinkQ[c];
            Queue* q2 = _coreToLeafQ[dst][c];
            if (!q1 || !q2) continue;

            // use current queue occupancy; method names differ across trees
            double size1 = q1->_queuesize;
            double size2 = q2->_queuesize;

            double inst = size1 + size2;
            _metric[dst][c] = ewma(_metric[dst][c], inst, _alpha);
        }
    }
}
