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

    int get(AVPacket *pkt, int block, int *serial);
    int put(AVPacket *pkt);
    int put_flush();
    int put_nullpacket(int stream_index);
    void start();
    void abort();
    void flush();
    int get_size();
    int get_abort_request();
    int get_serial();
    int get_nb_packets();
    int get_duration();
    static bool is_flush_packet( void* data);

protected:

    AVPacket* flush_pkt();

    static AVPacket* flush_pkt_queue;

    int put_private(AVPacket *pkt);

    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int64_t duration;
    int abort_request;
    int serial;
    
    std::recursive_mutex mutex;
    CondWait cond;

    friend class FFMPEGClock;

};

