#pragma once
#include <mutex>
#include "FFMPEGPacketQueue.h"
#include "CondWait.h"

#ifndef  FFMAX
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

class FFMPEGFrame;
struct AVFrame;

class FFMPEGFrameQueue
{
public:
    FFMPEGFrameQueue();
    ~FFMPEGFrameQueue();

    int init( FFMPEGPacketQueue *pktq, int max_size, int keep_last);
    void destroy();
    void signal();


    int queue_picture( AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);

    FFMPEGFrame *peek();
    FFMPEGFrame *peek_next();
    FFMPEGFrame *peek_last();
    FFMPEGFrame *peek_writable();
    FFMPEGFrame *peek_readable();
    void push();
    void next();
    

    void lock();
    void unlock();

    int64_t get_queue_last_pos();
    int get_nb_remaining();
    int get_rindex_shown();

private:
    
    FFMPEGFrame* queue[FRAME_QUEUE_SIZE];
    int rindex;
    int windex;
    int size;
    int max_size;
    int keep_last;
    int rindex_shown;
    std::recursive_mutex mutex;
    CondWait cond;
    FFMPEGPacketQueue *pktq;
};