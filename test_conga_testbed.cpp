// test_conga_testbed.cpp
#include <vector>
#include <random>
#include <string>
#include <cmath>
#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "queue.h"
#include "fairqueue.h"
#include "priorityqueue.h"
#include "aprx-fairqueue.h"
#include "stoc-fairqueue.h"
#include "pipe.h"
#include "leafswitch.h"
#include "flow-generator.h"
#include "test.h"

using namespace std;

namespace conga_conf {
    static const int N_CORE   = 12;
    static const int N_LEAF   = 24;
    static const int N_SERVER = 32;   // per leaf

    static const uint64_t LEAF_BUFFER = 512000;
    static const uint64_t CORE_BUFFER = 1024000;
    static const uint64_t ENDH_BUFFER = 8192000;

    static const uint64_t LEAF_SPEED  = 10000000000ULL; // 10G
    static const uint64_t CORE_SPEED  = 40000000000ULL; // 40G
    static const uint64_t LINK_DELAY_US = 1;

    struct Topo {
        vector<LeafSwitch*> leafSwitches;

        // [leaf][core]
        vector<vector<Queue*>> leafToCoreQ;
        vector<vector<Pipe*>>  leafToCoreP;

        // [core][leaf]
        vector<vector<Queue*>> coreToLeafQ;
        vector<vector<Pipe*>>  coreToLeafP;

        // [leaf][serverLocal]
        vector<vector<Queue*>> leafToServerQ;
        vector<vector<Pipe*>>  leafToServerP;
        vector<vector<Queue*>> serverToLeafQ;
        vector<vector<Pipe*>>  serverToLeafP;

        vector<uint32_t> serverToLeafMap;
    };

    static Topo topo;
}

static inline uint32_t getLeafForServer(uint32_t sid) {
    return sid / conga_conf::N_SERVER;
}
static inline uint32_t getLocalServerIndex(uint32_t sid) {
    return sid % conga_conf::N_SERVER;
}

static Queue* makeQueue(const string& qtype,
                        uint64_t speed,
                        uint64_t buffer,
                        QueueLogger* qlog,
                        const string& name,
                        Logfile& logfile)
{
    Queue* q = nullptr;
    if (qtype == "fq")      q = new FairQueue(speed, buffer, qlog);
    else if (qtype == "pq") q = new PriorityQueue(speed, buffer, qlog);
    else if (qtype == "sfq")q = new StocFairQueue(speed, buffer, qlog);
    else if (qtype == "afq")q = new AprxFairQueue(speed, buffer, qlog);
    else                    q = new Queue(speed, buffer, qlog); // droptail
    q->setName(name);
    logfile.writeName(*q);
    return q;
}

// global policy flag set from args: "ecmp" (default) or "conga"
static string g_policy = "conga";

// Route gen uses policy:
static void route_gen(route_t *&fwd, route_t *&rev, uint32_t &src, uint32_t &dst)
{
    using namespace conga_conf;
    static std::mt19937 rng(0xC0A6A5u);

    const uint32_t TOTAL_SERVERS = N_LEAF * N_SERVER;
    auto pick_pair = [&]() {
        std::uniform_int_distribution<uint32_t> U(0, TOTAL_SERVERS - 1);
        src = U(rng);
        do { dst = U(rng); } while (dst == src);
    };
    if (src >= TOTAL_SERVERS || dst >= TOTAL_SERVERS || src == dst) pick_pair();

    uint32_t srcLeaf = getLeafForServer(src);
    uint32_t dstLeaf = getLeafForServer(dst);
    uint32_t localSrc = getLocalServerIndex(src);
    uint32_t localDst = getLocalServerIndex(dst);

    fwd = new route_t();
    rev = new route_t();

    if (srcLeaf == dstLeaf) {
        // Same rack
        fwd->push_back(conga_conf::topo.serverToLeafQ[srcLeaf][localSrc]);
        fwd->push_back(conga_conf::topo.serverToLeafP[srcLeaf][localSrc]);
        fwd->push_back(conga_conf::topo.leafToServerQ[dstLeaf][localDst]);
        fwd->push_back(conga_conf::topo.leafToServerP[dstLeaf][localDst]);

        rev->push_back(conga_conf::topo.serverToLeafQ[dstLeaf][localDst]);
        rev->push_back(conga_conf::topo.serverToLeafP[dstLeaf][localDst]);
        rev->push_back(conga_conf::topo.leafToServerQ[srcLeaf][localSrc]);
        rev->push_back(conga_conf::topo.leafToServerP[srcLeaf][localSrc]);
        return;
    }

    // Choose core by policy
    uint32_t chosenCore = 0;
    if (g_policy == "conga") {
        chosenCore = conga_conf::topo.leafSwitches[srcLeaf]->chooseCore(dstLeaf);
    } else {
        // ECMP-ish hash
        chosenCore = (src * 1315423911u + dst) % N_CORE;
    }

    // FWD
    fwd->push_back(conga_conf::topo.serverToLeafQ[srcLeaf][localSrc]);
    fwd->push_back(conga_conf::topo.serverToLeafP[srcLeaf][localSrc]);

    fwd->push_back(conga_conf::topo.leafToCoreQ[srcLeaf][chosenCore]);
    fwd->push_back(conga_conf::topo.leafToCoreP[srcLeaf][chosenCore]);

    fwd->push_back(conga_conf::topo.coreToLeafQ[chosenCore][dstLeaf]);
    fwd->push_back(conga_conf::topo.coreToLeafP[chosenCore][dstLeaf]);

    fwd->push_back(conga_conf::topo.leafToServerQ[dstLeaf][localDst]);
    fwd->push_back(conga_conf::topo.leafToServerP[dstLeaf][localDst]);

    // REV
    rev->push_back(conga_conf::topo.serverToLeafQ[dstLeaf][localDst]);
    rev->push_back(conga_conf::topo.serverToLeafP[dstLeaf][localDst]);

    rev->push_back(conga_conf::topo.leafToCoreQ[dstLeaf][chosenCore]);
    rev->push_back(conga_conf::topo.leafToCoreP[dstLeaf][chosenCore]);

    rev->push_back(conga_conf::topo.coreToLeafQ[chosenCore][srcLeaf]);
    rev->push_back(conga_conf::topo.coreToLeafP[chosenCore][srcLeaf]);

    rev->push_back(conga_conf::topo.leafToServerQ[srcLeaf][localSrc]);
    rev->push_back(conga_conf::topo.leafToServerP[srcLeaf][localSrc]);
}

void conga_testbed(const ArgList &args, Logfile &logfile)
{
    using namespace conga_conf;

    uint32_t Duration    = 10;
    double   Util        = 0.2;
    uint32_t AvgFlowSize = 131072; // 128KB
    string   FlowDist    = "uniform";
    string   QueueType   = "droptail";
    string   EndHost     = "tcp";
    parseInt(args, "duration", Duration);
    parseDouble(args, "utilization", Util);
    parseInt(args, "flowsize", AvgFlowSize);
    parseString(args, "flowdist", FlowDist);
    parseString(args, "queue", QueueType);
    parseString(args, "endhost", EndHost);
    parseString(args, "policy",  g_policy); // "conga" (default) or "ecmp"

    // TCP logger for FCTs
    auto *logTcp = new TcpLoggerSimple();
    logfile.addLogger(*logTcp);

    // Allocate topology matrices
    topo.leafToCoreQ.assign(N_LEAF, vector<Queue*>(N_CORE, nullptr));
    topo.leafToCoreP.assign(N_LEAF, vector<Pipe*>(N_CORE, nullptr));
    topo.coreToLeafQ.assign(N_CORE, vector<Queue*>(N_LEAF, nullptr));
    topo.coreToLeafP.assign(N_CORE, vector<Pipe*>(N_LEAF, nullptr));
    topo.leafToServerQ.assign(N_LEAF, vector<Queue*>(N_SERVER, nullptr));
    topo.leafToServerP.assign(N_LEAF, vector<Pipe*>(N_SERVER, nullptr));
    topo.serverToLeafQ.assign(N_LEAF, vector<Queue*>(N_SERVER, nullptr));
    topo.serverToLeafP.assign(N_LEAF, vector<Pipe*>(N_SERVER, nullptr));
    topo.serverToLeafMap.resize(N_LEAF * N_SERVER);

    // Create leaf switches
    topo.leafSwitches.reserve(N_LEAF);
    for (int leaf = 0; leaf < N_LEAF; ++leaf) {
        auto *lsw = new LeafSwitch(leaf, N_CORE, N_LEAF, EventList::Get());
        lsw->setAlpha(0.25);
        lsw->setSamplingPeriod(timeFromUs(50));
        topo.leafSwitches.push_back(lsw);
    }

    // Leaf <-> Core wiring
    for (int leaf = 0; leaf < N_LEAF; ++leaf) {
        for (int core = 0; core < N_CORE; ++core) {
            // uplink leaf->core
            {
                string qn = "L" + to_string(leaf) + "_C" + to_string(core) + "_up";
                topo.leafToCoreQ[leaf][core] = makeQueue(QueueType, CORE_SPEED, LEAF_BUFFER, nullptr, qn, logfile);
                string pn = "pipe_L" + to_string(leaf) + "_C" + to_string(core) + "_up";
                topo.leafToCoreP[leaf][core] = new Pipe(timeFromUs(LINK_DELAY_US));
                topo.leafToCoreP[leaf][core]->setName(pn); logfile.writeName(*topo.leafToCoreP[leaf][core]);

                topo.leafSwitches[leaf]->addUplink(core, topo.leafToCoreQ[leaf][core], topo.leafToCoreP[leaf][core]);
            }
            // downlink core->leaf
            {
                string qn = "C" + to_string(core) + "_L" + to_string(leaf) + "_down";
                topo.coreToLeafQ[core][leaf] = makeQueue(QueueType, CORE_SPEED, CORE_BUFFER, nullptr, qn, logfile);
                string pn = "pipe_C" + to_string(core) + "_L" + to_string(leaf) + "_down";
                topo.coreToLeafP[core][leaf] = new Pipe(timeFromUs(LINK_DELAY_US));
                topo.coreToLeafP[core][leaf]->setName(pn); logfile.writeName(*topo.coreToLeafP[core][leaf]);
            }
        }
    }

    // Register remote core->leaf queues with each leaf (to compute dst path metric)
    for (int leaf = 0; leaf < N_LEAF; ++leaf) {
        for (int dstLeaf = 0; dstLeaf < N_LEAF; ++dstLeaf) {
            for (int core = 0; core < N_CORE; ++core) {
                topo.leafSwitches[leaf]->registerCoreToLeaf(core, dstLeaf,
                    topo.coreToLeafQ[core][dstLeaf],
                    topo.coreToLeafP[core][dstLeaf]);
            }
        }
    }

    // Leaf <-> Servers
    for (int leaf = 0; leaf < N_LEAF; ++leaf) {
        for (int s = 0; s < N_SERVER; ++s) {
            uint32_t gsid = leaf * N_SERVER + s;
            topo.serverToLeafMap[gsid] = leaf;

            // server->leaf
            {
                string qn = "S" + to_string(gsid) + "_L" + to_string(leaf) + "_up";
                topo.serverToLeafQ[leaf][s] = makeQueue(QueueType, LEAF_SPEED, ENDH_BUFFER, nullptr, qn, logfile);
                string pn = "pipe_S" + to_string(gsid) + "_L" + to_string(leaf) + "_up";
                topo.serverToLeafP[leaf][s] = new Pipe(timeFromUs(LINK_DELAY_US));
                topo.serverToLeafP[leaf][s]->setName(pn); logfile.writeName(*topo.serverToLeafP[leaf][s]);
            }
            // leaf->server
            {
                string qn = "L" + to_string(leaf) + "_S" + to_string(gsid) + "_down";
                topo.leafToServerQ[leaf][s] = makeQueue(QueueType, LEAF_SPEED, LEAF_BUFFER, nullptr, qn, logfile);
                string pn = "pipe_L" + to_string(leaf) + "_S" + to_string(gsid) + "_down";
                topo.leafToServerP[leaf][s] = new Pipe(timeFromUs(LINK_DELAY_US));
                topo.leafToServerP[leaf][s]->setName(pn); logfile.writeName(*topo.leafToServerP[leaf][s]);
            }
            topo.leafSwitches[leaf]->addDownlink(gsid, topo.leafToServerQ[leaf][s], topo.leafToServerP[leaf][s]);
        }
    }

    // Flow generator
    DataSource::EndHost eh = DataSource::TCP;
    if (EndHost == "dctcp") eh = DataSource::DCTCP;

    Workloads::FlowDist fd = Workloads::UNIFORM;
    if (FlowDist == "pareto") fd = Workloads::PARETO;
    else if (FlowDist == "datamining") fd = Workloads::DATAMINING;
    else if (FlowDist == "enterprise") {
        // If your tree has ENTERPRISE, use it; otherwise map to PARETO:
        #ifdef WORKLOADS_HAS_ENTERPRISE
        fd = Workloads::ENTERPRISE;
        #else
        fd = Workloads::PARETO;
        #endif
    }

    const uint64_t TOTAL_HOST_LINKS = (uint64_t)N_LEAF * (uint64_t)N_SERVER;
    const long double totalCapacity = (long double)TOTAL_HOST_LINKS * (long double)LEAF_SPEED;
    linkspeed_bps flowRate = llround(totalCapacity * Util);
    flowRate = llround(flowRate * 0.001);

    auto *flowGen = new FlowGenerator(eh, route_gen, flowRate, AvgFlowSize, fd);
    flowGen->setEndhostQueue(LEAF_SPEED, ENDH_BUFFER);
    flowGen->setPrefix(g_policy + "-");
    flowGen->setTimeLimits(0, timeFromSec(Duration)); // schedules itself

    EventList::Get().setEndtime(timeFromSec(Duration));
}
