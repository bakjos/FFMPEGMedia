#include "FFMPEGPacketQueue.h"

extern "C" {
    #include <inttypes.h>
    #include "libavcodec/avcodec.h"
}

struct MyAVPacketList {
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
};

typedef struct MyAVPacketList MyAVPacketList;

AVPacket* FFMPEGPacketQueue::flush_pkt_queue = NULL;
std::mutex  flush_pkt_queue_mutex;

FFMPEGPacketQueue::FFMPEGPacketQueue()
{
    
    first_pkt = NULL;
    last_pkt = NULL;
    nb_packets = 0;
    size = 0;
    duration = 0;
    abort_request = true;
    serial = 0;
}


FFMPEGPacketQueue::~FFMPEGPacketQueue()
{
     Flush();
}

AVPacket* FFMPEGPacketQueue::FlushPkt() {
    if ( flush_pkt_queue == NULL) {
        flush_pkt_queue_mutex.lock();
        if ( flush_pkt_queue == NULL) {
            flush_pkt_queue = new AVPacket();
            av_init_packet(flush_pkt_queue);
            flush_pkt_queue->data = (uint8_t *)flush_pkt_queue;
        }
        flush_pkt_queue_mutex.unlock();
    }
    return flush_pkt_queue;
}

int FFMPEGPacketQueue::Put(AVPacket *pkt) {
    int ret;

    mutex.Lock();
    ret = PutPrivate( pkt);
    mutex.Unlock();

    if (pkt != FlushPkt() && ret < 0)
        av_packet_unref(pkt);

    return ret;
}

int FFMPEGPacketQueue::PutNullPacket(int stream_index) {
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return Put(pkt);
}

int FFMPEGPacketQueue::PutPrivate(AVPacket *pkt) {
    MyAVPacketList *pkt1;

    if (abort_request)
        return -1;

    pkt1 = new MyAVPacketList();
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == FlushPkt())
        serial++;
    pkt1->serial = serial;

    if (!last_pkt)
        first_pkt = pkt1;
    else
        last_pkt->next = pkt1;
    last_pkt = pkt1;
    nb_packets++;
    size += pkt1->pkt.size + sizeof(*pkt1);
    duration += pkt1->pkt.duration;
    /* XXX: should duplicate packet data in DV case */
    cond.signal();
    return 0;
}

int  FFMPEGPacketQueue::Get(AVPacket *pkt, int block, int *serial) {
    MyAVPacketList *pkt1;
    int ret;

    mutex.Lock();

    for (;;) {
        if (abort_request) {
            ret = -1;
            break;
        }

        pkt1 = first_pkt;
        if (pkt1) {
            first_pkt = pkt1->next;
            if (!first_pkt)
                last_pkt = NULL;
            nb_packets--;
            size -= pkt1->pkt.size + sizeof(*pkt1);
            duration -= pkt1->pkt.duration;
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
            delete pkt1;
            ret = 1;
            break;
        }
        else if (!block) {
            ret = 0;
            break;
        }
        else {
            cond.wait(mutex);
        }
    }
    mutex.Unlock();
    return ret;
}


void FFMPEGPacketQueue::Abort() {
    mutex.Lock();
    abort_request = true;
    cond.signal();
    mutex.Unlock();
}

void FFMPEGPacketQueue::Start() {
    mutex.Lock();
    abort_request = false;
    PutPrivate(FlushPkt());
    mutex.Unlock();
}

void FFMPEGPacketQueue::Flush() {
    MyAVPacketList *pkt, *pkt1;

    mutex.Lock();
    for (pkt = first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        delete pkt;
    }
    last_pkt = NULL;
    first_pkt = NULL;
    nb_packets = 0;
    size = 0;
    duration = 0;
    mutex.Unlock();
}

int FFMPEGPacketQueue::PutFlush() {
    return Put(FlushPkt());
}

int FFMPEGPacketQueue::GetSize() {
    return size;
}

int FFMPEGPacketQueue::GetSerial() {
    return serial;
}

bool FFMPEGPacketQueue::IsAbortRequest() {
    return abort_request;
}

int FFMPEGPacketQueue::GetNumPackets() {
    return nb_packets;
}

int FFMPEGPacketQueue::GetDuration() {
    return duration;    
}

bool FFMPEGPacketQueue::IsFlushPacket( void* data) {
    if ( flush_pkt_queue != NULL) {
        return flush_pkt_queue->data == data;
    }
    return false;
}



