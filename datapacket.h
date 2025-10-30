/*
 * Datapacket header
 */
#ifndef DATAPACKET_H
#define DATAPACKET_H

#include "network.h"

// DataPacket and DataAck are subclasses of Packet used by TcpSrc and other flow control protocols.
// They incorporate a packet database, to reuse packet objects that are no longer needed.
// Note: you never construct a new DataPacket or DataAck directly; 
// rather you use the static method newpkt() which knows to reuse old packets from the database.

class DataPacket : public Packet
{
    public:
        typedef uint64_t seq_t;
        virtual ~DataPacket() {}

        inline static DataPacket* newpkt(PacketFlow &flow, route_t &route, seq_t seqno, int size)
        {
            DataPacket *p = _packetdb.allocPacket();

            // The sequence number is the first byte of the packet.
            // This will ID the packet by its last byte.
            p->set(flow, route, size, seqno);
            p->_seqno = seqno;
            
            // Initialize CONGA metadata
            p->_srcLeaf = 0;
            p->_dstLeaf = 0;
            p->_selectedCore = 0;
            p->_congestionMetric = 0;
            p->_isFeedback = false;
            
            flow._nPackets++;
            return p;
        }

        void free() {
            flow()._nPackets--;
            _packetdb.freePacket(this);
        }

        inline seq_t seqno() const {return _seqno;}
        inline simtime_picosec ts() const {return _ts;}
        inline void set_ts(simtime_picosec ts) {_ts = ts;}
        
        // CONGA metadata methods
        inline void setCongaMetadata(uint32_t srcLeaf, uint32_t dstLeaf) {
            _srcLeaf = srcLeaf;
            _dstLeaf = dstLeaf;
            _congestionMetric = 0;
            _isFeedback = false;
        }
        
        inline void addCongestion(uint64_t congestion) {
            _congestionMetric += congestion;
        }
        
        inline void setSelectedCore(uint32_t coreId) {
            _selectedCore = coreId;
        }
        
        inline void markAsFeedback() {
            _isFeedback = true;
        }
        
        inline uint32_t getSrcLeaf() const { return _srcLeaf; }
        inline uint32_t getDstLeaf() const { return _dstLeaf; }
        inline uint32_t getSelectedCore() const { return _selectedCore; }
        inline uint64_t getCongestionMetric() const { return _congestionMetric; }
        inline bool isFeedback() const { return _isFeedback; }

    protected:
        seq_t _seqno;
        simtime_picosec _ts;
        
        // CONGA metadata
        uint32_t _srcLeaf;
        uint32_t _dstLeaf;
        uint32_t _selectedCore;
        uint64_t _congestionMetric;
        bool _isFeedback;

        static PacketDB<DataPacket> _packetdb;
};

class DataAck : public Packet
{
    public:
        typedef DataPacket::seq_t seq_t;

        virtual ~DataAck(){}

        inline static DataAck* newpkt(PacketFlow &flow, route_t &route, seq_t seqno, seq_t ackno)
        {
            DataAck *p = _packetdb.allocPacket();
            p->set(flow, route, ACK_SIZE, ackno);
            p->_seqno = seqno;
            p->_ackno = ackno;
            
            // Initialize CONGA metadata for ACKs too
            p->_srcLeaf = 0;
            p->_dstLeaf = 0;
            p->_selectedCore = 0;
            p->_congestionMetric = 0;
            p->_isFeedback = false;
            
            flow._nPackets++;
            return p;
        }

        void free() {
            flow()._nPackets--;
            _packetdb.freePacket(this);
        }

        inline seq_t seqno() const {return _seqno;}
        inline seq_t ackno() const {return _ackno;}
        inline simtime_picosec ts() const {return _ts;}
        inline void set_ts(simtime_picosec ts) {_ts = ts;}
        
        // CONGA metadata methods for ACKs
        inline void setCongaMetadata(uint32_t srcLeaf, uint32_t dstLeaf) {
            _srcLeaf = srcLeaf;
            _dstLeaf = dstLeaf;
            _congestionMetric = 0;
            _isFeedback = false;
        }
        
        inline void addCongestion(uint64_t congestion) {
            _congestionMetric += congestion;
        }
        
        inline void setSelectedCore(uint32_t coreId) {
            _selectedCore = coreId;
        }
        
        inline void markAsFeedback() {
            _isFeedback = true;
        }
        
        inline uint32_t getSrcLeaf() const { return _srcLeaf; }
        inline uint32_t getDstLeaf() const { return _dstLeaf; }
        inline uint32_t getSelectedCore() const { return _selectedCore; }
        inline uint64_t getCongestionMetric() const { return _congestionMetric; }
        inline bool isFeedback() const { return _isFeedback; }

    protected:
        seq_t _seqno;
        seq_t _ackno;
        simtime_picosec _ts;
        
        // CONGA metadata
        uint32_t _srcLeaf;
        uint32_t _dstLeaf;
        uint32_t _selectedCore;
        uint64_t _congestionMetric;
        bool _isFeedback;

        static PacketDB<DataAck> _packetdb;
};

#endif /* DATAPACKET_H */