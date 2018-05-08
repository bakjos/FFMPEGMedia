#pragma once
#include <mutex>
#include "FFMPEGPacketQueue.h"


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

    int Init( FFMPEGPacketQueue *pktq, int max_size, int keep_last);
    void Destroy();
    void Signal();


    int QueuePicture( AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);

    FFMPEGFrame *Peek();
    FFMPEGFrame *PeekNext();
    FFMPEGFrame *PeekLast();
    FFMPEGFrame *PeekWritable();
    FFMPEGFrame *PeekReadable();
    void Push();
    void Next();
    

    void Lock();
    void Unlock();

    int64_t GetQueueLastPos();
    int GetNumRemaining();
    int GetIndexShown();

private:
    
    FFMPEGFrame* queue[FRAME_QUEUE_SIZE];
    int rindex;
    int windex;
    int size;
    int max_size;
    int keep_last;
    int rindex_shown;
    FCriticalSection mutex;
    CondWait cond;
    FFMPEGPacketQueue *pktq;
};