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
    abort_request = 0;
    first_pkt = NULL;
    last_pkt = NULL;
    nb_packets = 0;
    size = 0;
    duration = 0;
    abort_request = 1;
    serial = 0;
}


FFMPEGPacketQueue::~FFMPEGPacketQueue()
{
     flush();
}

AVPacket* FFMPEGPacketQueue::flush_pkt() {
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

int FFMPEGPacketQueue::put(AVPacket *pkt) {
    int ret;

    mutex.lock();
    ret = put_private( pkt);
    mutex.unlock();

    if (pkt != flush_pkt() && ret < 0)
        av_packet_unref(pkt);

    return ret;
}

int FFMPEGPacketQueue::put_nullpacket(int stream_index) {
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return put(pkt);
}

int FFMPEGPacketQueue::put_private(AVPacket *pkt) {
    MyAVPacketList *pkt1;

    if (abort_request)
        return -1;

    pkt1 = new MyAVPacketList();
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == flush_pkt())
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

int  FFMPEGPacketQueue::get(AVPacket *pkt, int block, int *serial) {
    MyAVPacketList *pkt1;
    int ret;

    mutex.lock();

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
    mutex.unlock();
    return ret;
}


void FFMPEGPacketQueue::abort() {
    mutex.lock();
    abort_request = 1;
    cond.signal();
    mutex.unlock();
}

void FFMPEGPacketQueue::start() {
    mutex.lock();
    abort_request = 0;
    put_private(flush_pkt());
    mutex.unlock();
}

void FFMPEGPacketQueue::flush() {
    MyAVPacketList *pkt, *pkt1;

    mutex.lock();
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
    mutex.unlock();
}

int FFMPEGPacketQueue::put_flush() {
    return put(flush_pkt());
}

int FFMPEGPacketQueue::get_size() {
    return size;
}

int FFMPEGPacketQueue::get_serial() {
    return serial;
}

int FFMPEGPacketQueue::get_abort_request() {
    return abort_request;
}

int FFMPEGPacketQueue::get_nb_packets() {
    return nb_packets;
}

int FFMPEGPacketQueue::get_duration() {
    return duration;    
}

bool FFMPEGPacketQueue::is_flush_packet( void* data) {
    if ( flush_pkt_queue != NULL) {
        return flush_pkt_queue->data == data;
    }
    return false;
}



