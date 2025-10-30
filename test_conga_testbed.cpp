#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "aprx-fairqueue.h"
#include "fairqueue.h"
#include "priorityqueue.h"
#include "stoc-fairqueue.h"
#include "flow-generator.h"
#include "pipe.h"
#include "leafswitch.h"
#include "test.h"
#include <vector>

namespace conga {
    // Testbed configuration
    const int N_CORE = 12;
    const int N_LEAF = 24;
    const int N_SERVER = 32;   // Per leaf

    const uint64_t LEAF_BUFFER = 512000;
    const uint64_t CORE_BUFFER = 1024000;
    const uint64_t ENDH_BUFFER = 8192000;

    const uint64_t LEAF_SPEED = 10000000000;  // 10Gbps
    const uint64_t CORE_SPEED = 40000000000;  // 40Gbps
    const uint64_t LINK_DELAY = 1;            // 1us
    
    // Network topology structure
    struct NetworkTopology {
        std::vector<LeafSwitch*> leafSwitches;
        
        // Core uplink queues: [leaf][core] -> queue/pipe
        std::vector<std::vector<Queue*>> leafToCore_queues;
        std::vector<std::vector<Pipe*>> leafToCore_pipes;
        
        // Core downlink queues: [core][leaf] -> queue/pipe
        std::vector<std::vector<Queue*>> coreToLeaf_queues;
        std::vector<std::vector<Pipe*>> coreToLeaf_pipes;
        
        // Server connections: [leaf][server] -> queue/pipe
        std::vector<std::vector<Queue*>> leafToServer_queues;
        std::vector<std::vector<Pipe*>> leafToServer_pipes;
        std::vector<std::vector<Queue*>> serverToLeaf_queues;
        std::vector<std::vector<Pipe*>> serverToLeaf_pipes;
        
        // Mapping: serverId -> leafId
        std::vector<uint32_t> serverToLeafMap;
    };
    
    NetworkTopology topology;
}

using namespace std;
using namespace conga;

// Helper function to create queue based on type
Queue* createQueue(const string& queueType, uint64_t speed, uint64_t buffer, 
                   QueueLoggerSampling* logger, const string& name, Logfile& logfile) {
    Queue* queue;
    
    if (queueType == "fq") {
        queue = new FairQueue(speed, buffer, logger);
    } else if (queueType == "pq") {
        queue = new PriorityQueue(speed, buffer, logger);
    } else {
        queue = new Queue(speed, buffer, logger);
    }
    
    queue->setName(name);
    logfile.writeName(*queue);
    return queue;
}

// Helper to get leaf ID for a server
uint32_t getLeafForServer(uint32_t serverId) {
    return serverId / N_SERVER;  // Each leaf has N_SERVER servers
}

// Helper to get local server index on a leaf
uint32_t getLocalServerIndex(uint32_t serverId) {
    return serverId % N_SERVER;
}

// Route generation for CONGA
void generateCongaRoute(route_t *&fwd, route_t *&rev, uint32_t &src, uint32_t &dst) {
    // Determine source and destination leaves
    uint32_t srcLeaf = getLeafForServer(src);
    uint32_t dstLeaf = getLeafForServer(dst);
    
    fwd = new route_t();
    rev = new route_t();
    
    if (srcLeaf == dstLeaf) {
        // Same rack communication - direct path
        uint32_t localSrc = getLocalServerIndex(src);
        uint32_t localDst = getLocalServerIndex(dst);
        
        // Server -> Leaf -> Server
        fwd->push_back(topology.serverToLeaf_queues[srcLeaf][localSrc]);
        fwd->push_back(topology.serverToLeaf_pipes[srcLeaf][localSrc]);
        fwd->push_back(topology.leafToServer_queues[dstLeaf][localDst]);
        fwd->push_back(topology.leafToServer_pipes[dstLeaf][localDst]);
        
        // Reverse
        rev->push_back(topology.serverToLeaf_queues[dstLeaf][localDst]);
        rev->push_back(topology.serverToLeaf_pipes[dstLeaf][localDst]);
        rev->push_back(topology.leafToServer_queues[srcLeaf][localSrc]);
        rev->push_back(topology.leafToServer_pipes[srcLeaf][localSrc]);
    } else {
        // Cross-rack communication - need to go through core
        // CONGA will select which core switch to use dynamically
        
        // For now, use a hash-based selection (will be overridden by CONGA logic)
        uint32_t selectedCore = (src + dst) % N_CORE;
        uint32_t localSrc = getLocalServerIndex(src);
        uint32_t localDst = getLocalServerIndex(dst);
        
        // Forward: Server -> SrcLeaf -> Core -> DstLeaf -> Server
        fwd->push_back(topology.serverToLeaf_queues[srcLeaf][localSrc]);
        fwd->push_back(topology.serverToLeaf_pipes[srcLeaf][localSrc]);
        fwd->push_back(topology.leafToCore_queues[srcLeaf][selectedCore]);
        fwd->push_back(topology.leafToCore_pipes[srcLeaf][selectedCore]);
        fwd->push_back(topology.coreToLeaf_queues[selectedCore][dstLeaf]);
        fwd->push_back(topology.coreToLeaf_pipes[selectedCore][dstLeaf]);
        fwd->push_back(topology.leafToServer_queues[dstLeaf][localDst]);
        fwd->push_back(topology.leafToServer_pipes[dstLeaf][localDst]);
        
        // Reverse path
        rev->push_back(topology.serverToLeaf_queues[dstLeaf][localDst]);
        rev->push_back(topology.serverToLeaf_pipes[dstLeaf][localDst]);
        rev->push_back(topology.leafToCore_queues[dstLeaf][selectedCore]);
        rev->push_back(topology.leafToCore_pipes[dstLeaf][selectedCore]);
        rev->push_back(topology.coreToLeaf_queues[selectedCore][srcLeaf]);
        rev->push_back(topology.coreToLeaf_pipes[selectedCore][srcLeaf]);
        rev->push_back(topology.leafToServer_queues[srcLeaf][localSrc]);
        rev->push_back(topology.leafToServer_pipes[srcLeaf][localSrc]);
    }
}

void conga_testbed(const ArgList &args, Logfile &logfile)
{
    // Parse arguments
    uint32_t Duration = 10;
    double Utilization = 0.5;
    uint32_t AvgFlowSize = 100000;
    string FlowDist = "uniform";
    string QueueType = "droptail";
    string EndHost = "tcp";
    
    parseInt(args, "duration", Duration);
    parseDouble(args, "utilization", Utilization);
    parseInt(args, "flowsize", AvgFlowSize);
    parseString(args, "flowdist", FlowDist);
    parseString(args, "queue", QueueType);
    parseString(args, "endhost", EndHost);
    
    // Setup logging
    QueueLoggerSampling *qs = new QueueLoggerSampling(timeFromUs(1000));
    logfile.addLogger(*qs);
    
    TcpLoggerSimple *logTcp = new TcpLoggerSimple();
    logfile.addLogger(*logTcp);
    
    // Initialize topology structures
    topology.leafToCore_queues.resize(N_LEAF);
    topology.leafToCore_pipes.resize(N_LEAF);
    topology.coreToLeaf_queues.resize(N_CORE);
    topology.coreToLeaf_pipes.resize(N_CORE);
    topology.leafToServer_queues.resize(N_LEAF);
    topology.leafToServer_pipes.resize(N_LEAF);
    topology.serverToLeaf_queues.resize(N_LEAF);
    topology.serverToLeaf_pipes.resize(N_LEAF);
    topology.serverToLeafMap.resize(N_LEAF * N_SERVER);
    
    for (int i = 0; i < N_LEAF; i++) {
        topology.leafToCore_queues[i].resize(N_CORE);
        topology.leafToCore_pipes[i].resize(N_CORE);
        topology.leafToServer_queues[i].resize(N_SERVER);
        topology.leafToServer_pipes[i].resize(N_SERVER);
        topology.serverToLeaf_queues[i].resize(N_SERVER);
        topology.serverToLeaf_pipes[i].resize(N_SERVER);
    }
    
    for (int i = 0; i < N_CORE; i++) {
        topology.coreToLeaf_queues[i].resize(N_LEAF);
        topology.coreToLeaf_pipes[i].resize(N_LEAF);
    }
    
    // Create leaf switches
    for (int leaf = 0; leaf < N_LEAF; leaf++) {
        LeafSwitch* leafSwitch = new LeafSwitch(leaf, N_CORE, EventList::Get());
        topology.leafSwitches.push_back(leafSwitch);
    }
    
    // Create core layer connections (leaf <-> core)
    for (int leaf = 0; leaf < N_LEAF; leaf++) {
        for (int core = 0; core < N_CORE; core++) {
            // Leaf -> Core (uplink)
            string queueName = "L" + to_string(leaf) + "_C" + to_string(core) + "_up";
            topology.leafToCore_queues[leaf][core] = createQueue(
                QueueType, CORE_SPEED, LEAF_BUFFER, qs, queueName, logfile);
            
            string pipeName = "pipe_L" + to_string(leaf) + "_C" + to_string(core) + "_up";
            topology.leafToCore_pipes[leaf][core] = new Pipe(timeFromUs(LINK_DELAY));
            topology.leafToCore_pipes[leaf][core]->setName(pipeName);
            logfile.writeName(*topology.leafToCore_pipes[leaf][core]);
            
            // Core -> Leaf (downlink)
            queueName = "C" + to_string(core) + "_L" + to_string(leaf) + "_down";
            topology.coreToLeaf_queues[core][leaf] = createQueue(
                QueueType, CORE_SPEED, CORE_BUFFER, qs, queueName, logfile);
            
            pipeName = "pipe_C" + to_string(core) + "_L" + to_string(leaf) + "_down";
            topology.coreToLeaf_pipes[core][leaf] = new Pipe(timeFromUs(LINK_DELAY));
            topology.coreToLeaf_pipes[core][leaf]->setName(pipeName);
            logfile.writeName(*topology.coreToLeaf_pipes[core][leaf]);
            
            // Register uplink with leaf switch
            topology.leafSwitches[leaf]->addUplink(
                core, 
                topology.leafToCore_queues[leaf][core],
                topology.leafToCore_pipes[leaf][core]
            );
        }
    }

    // Create server connections
    for (int leaf = 0; leaf < N_LEAF; leaf++) {
        for (int server = 0; server < N_SERVER; server++) {
            uint32_t globalServerId = leaf * N_SERVER + server;
            topology.serverToLeafMap[globalServerId] = leaf;
            
            // Server -> Leaf
            string queueName = "S" + to_string(globalServerId) + "_L" + to_string(leaf);
            topology.serverToLeaf_queues[leaf][server] = createQueue(
                QueueType, LEAF_SPEED, ENDH_BUFFER, NULL, queueName, logfile);
            
            string pipeName = "pipe_S" + to_string(globalServerId) + "_L" + to_string(leaf);
            topology.serverToLeaf_pipes[leaf][server] = new Pipe(timeFromUs(LINK_DELAY));
            topology.serverToLeaf_pipes[leaf][server]->setName(pipeName);
            logfile.writeName(*topology.serverToLeaf_pipes[leaf][server]);
            
            // Leaf -> Server
            queueName = "L" + to_string(leaf) + "_S" + to_string(globalServerId);
            topology.leafToServer_queues[leaf][server] = createQueue(
                QueueType, LEAF_SPEED, LEAF_BUFFER, NULL, queueName, logfile);
            
            pipeName = "pipe_L" + to_string(leaf) + "_S" + to_string(globalServerId);
            topology.leafToServer_pipes[leaf][server] = new Pipe(timeFromUs(LINK_DELAY));
            topology.leafToServer_pipes[leaf][server]->setName(pipeName);
            logfile.writeName(*topology.leafToServer_pipes[leaf][server]);
            
            // Register downlink with leaf switch
            topology.leafSwitches[leaf]->addDownlink(
                globalServerId,
                topology.leafToServer_queues[leaf][server],
                topology.leafToServer_pipes[leaf][server]
            );
        }
    }

    // Setup flow generator
    DataSource::EndHost eh = DataSource::TCP;
    if (EndHost == "dctcp") {
        eh = DataSource::DCTCP;
    }
    
    Workloads::FlowDist fd = Workloads::UNIFORM;
    if (FlowDist == "pareto") {
        fd = Workloads::PARETO;
    } else if (FlowDist == "datamining") {
        fd = Workloads::DATAMINING;
    }
    
    // Calculate aggregate capacity (all server-facing links)
    uint64_t totalCapacity = N_LEAF * N_SERVER * LEAF_SPEED;
    linkspeed_bps flowRate = llround(totalCapacity * Utilization);
    
    FlowGenerator *flowGen = new FlowGenerator(
        eh, generateCongaRoute, flowRate, AvgFlowSize, fd);
    
    flowGen->setEndhostQueue(LEAF_SPEED, ENDH_BUFFER);
    flowGen->setTimeLimits(0, timeFromSec(Duration) - 1);
    
    EventList::Get().setEndtime(timeFromSec(Duration));
}