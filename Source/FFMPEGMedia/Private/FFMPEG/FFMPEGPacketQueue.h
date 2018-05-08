#pragma once

#include "CondWait.h"
#include <mutex>

struct MyAVPacketList;
struct AVPacket;


class FFMPEGPacketQueue
{
public:
    FFMPEGPacketQueue();
    ~FFMPEGPacketQueue();

    int Get(AVPacket *pkt, int block, int *serial);
    int Put(AVPacket *pkt);
    int PutFlush();
    int PutNullPacket(int stream_index);
    void Start();
    void Abort();
    void Flush();
    int GetSize();
    bool IsAbortRequest();
    int GetSerial();
    int GetNumPackets();
    int GetDuration();
    static bool IsFlushPacket( void* data);

protected:

    AVPacket* FlushPkt();
    int PutPrivate(AVPacket *pkt);

    static AVPacket* flush_pkt_queue;

    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int64_t duration;
    bool abort_request;
    int serial;
    
    FCriticalSection mutex;
    CondWait cond;

    friend class FFMPEGClock;

};

