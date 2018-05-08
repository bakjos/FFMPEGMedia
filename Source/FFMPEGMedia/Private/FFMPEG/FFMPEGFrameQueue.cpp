#include "FFMPEGFrameQueue.h"
#include "FFMPEGFrame.h"


FFMPEGFrameQueue::FFMPEGFrameQueue()
{
    for ( int i = 0; i < FRAME_QUEUE_SIZE; i++) {
        queue[i] = new FFMPEGFrame();
    }

    rindex = 0;
    windex = 0;
    size = 0;
    max_size = 0;
    keep_last = 0;
    rindex_shown = 0;
    pktq = NULL;
}


FFMPEGFrameQueue::~FFMPEGFrameQueue()
{
    Destroy();
    for (int i = 0; i < FRAME_QUEUE_SIZE; i++) {
        if ( queue[i] ) delete queue[i];
        queue[i] = NULL;
    }
}

int FFMPEGFrameQueue::Init( FFMPEGPacketQueue *pktq, int max_size, int keep_last) {
    this->pktq = pktq;
    this->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    this->keep_last = !!keep_last;
    for (int i = 0; i < this->max_size; i++)
        if (!(queue[i]->Init()))
            return AVERROR(ENOMEM);
    return 0;
}

void FFMPEGFrameQueue::Destroy() {
    
    for (int i = 0; i < max_size; i++) {
        FFMPEGFrame *vp = queue[i];
        if ( vp) vp->Destroy();
    }
}

void FFMPEGFrameQueue::Signal() {
    mutex.Lock();
    cond.signal();
    mutex.Unlock();
}

FFMPEGFrame *FFMPEGFrameQueue::Peek() {
    return queue[(rindex + rindex_shown) % max_size];
}

FFMPEGFrame *FFMPEGFrameQueue::PeekNext() {
    return queue[(rindex + rindex_shown + 1) % max_size];
}

FFMPEGFrame *FFMPEGFrameQueue::PeekLast() {
    return queue[rindex];
}

FFMPEGFrame *FFMPEGFrameQueue::PeekWritable() {
    mutex.Lock();
    while (size >= max_size &&
        !pktq->IsAbortRequest()) {
        cond.wait(mutex);
    }
    mutex.Unlock();

    if (pktq->IsAbortRequest())
        return NULL;

    return queue[windex];
}
FFMPEGFrame *FFMPEGFrameQueue::PeekReadable() {
    mutex.Lock();
    while (size - rindex_shown <= 0 &&
        !pktq->IsAbortRequest()) {
        cond.wait(mutex);
    }
    mutex.Unlock();

    if (pktq->IsAbortRequest())
        return NULL;

    return queue[(rindex + rindex_shown) % max_size];
}

int FFMPEGFrameQueue::QueuePicture( AVFrame *src_frame, double pts, double duration, int64_t pos, int serial) {
    
    FFMPEGFrame *vp = PeekWritable();
    if (!vp )
        return -1;

    vp->UpdateFrame(src_frame, pts, duration, pos, serial);
    av_frame_move_ref(vp->GetFrame(), src_frame);

    Push();
    return 0;
}

void FFMPEGFrameQueue::Push() {
    if (++windex == max_size)
        windex = 0;
    mutex.Lock();
    size++;
    cond.signal();
    mutex.Unlock();
}

void FFMPEGFrameQueue::Next() {
    if (keep_last && !rindex_shown) {
        rindex_shown = 1;
        return;
    }
    queue[rindex]->UnRef();
    if (++rindex == max_size)
        rindex = 0;
    mutex.Lock();
    size--;
    cond.signal();
    mutex.Unlock();
   
}

void FFMPEGFrameQueue::Lock() {
    mutex.Lock();
}

void FFMPEGFrameQueue::Unlock() {
    mutex.Unlock();
}

int FFMPEGFrameQueue::GetNumRemaining() {
     return size - rindex_shown;
}

int64_t FFMPEGFrameQueue::GetQueueLastPos() {
    FFMPEGFrame *fp = queue[rindex];
    if (rindex_shown && fp->GetSerial() == pktq->GetSerial())
        return fp->GetPos();
    else
        return -1;
}

int  FFMPEGFrameQueue::GetIndexShown() {
    return rindex_shown;
}
