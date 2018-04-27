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
    destroy();
    for (int i = 0; i < FRAME_QUEUE_SIZE; i++) {
        if ( queue[i] ) delete queue[i];
        queue[i] = NULL;
    }
}

int FFMPEGFrameQueue::init( FFMPEGPacketQueue *pktq, int max_size, int keep_last) {
    this->pktq = pktq;
    this->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    this->keep_last = !!keep_last;
    for (int i = 0; i < this->max_size; i++)
        if (!(queue[i]->init()))
            return AVERROR(ENOMEM);
    return 0;
}

void FFMPEGFrameQueue::destroy() {
    
    for (int i = 0; i < max_size; i++) {
        FFMPEGFrame *vp = queue[i];
        if ( vp) vp->destroy();
    }
}

void FFMPEGFrameQueue::signal() {
    mutex.Lock();
    cond.signal();
    mutex.Unlock();
}

FFMPEGFrame *FFMPEGFrameQueue::peek() {
    return queue[(rindex + rindex_shown) % max_size];
}

FFMPEGFrame *FFMPEGFrameQueue::peek_next() {
    return queue[(rindex + rindex_shown + 1) % max_size];
}

FFMPEGFrame *FFMPEGFrameQueue::peek_last() {
    return queue[rindex];
}

FFMPEGFrame *FFMPEGFrameQueue::peek_writable() {
    mutex.Lock();
    while (size >= max_size &&
        !pktq->get_abort_request()) {
        cond.wait(mutex);
    }
    mutex.Unlock();

    if (pktq->get_abort_request())
        return NULL;

    return queue[windex];
}
FFMPEGFrame *FFMPEGFrameQueue::peek_readable() {
    mutex.Lock();
    while (size - rindex_shown <= 0 &&
        !pktq->get_abort_request()) {
        cond.wait(mutex);
    }
    mutex.Unlock();

    if (pktq->get_abort_request())
        return NULL;

    return queue[(rindex + rindex_shown) % max_size];
}

int FFMPEGFrameQueue::queue_picture( AVFrame *src_frame, double pts, double duration, int64_t pos, int serial) {
    
    FFMPEGFrame *vp = peek_writable();
    if (!vp )
        return -1;

    vp->update_frame(src_frame, pts, duration, pos, serial);
    av_frame_move_ref(vp->get_frame(), src_frame);

    push();
    return 0;
}

void FFMPEGFrameQueue::push() {
    if (++windex == max_size)
        windex = 0;
    mutex.Lock();
    size++;
    cond.signal();
    mutex.Unlock();
}

void FFMPEGFrameQueue::next() {
    if (keep_last && !rindex_shown) {
        rindex_shown = 1;
        return;
    }
    queue[rindex]->unref();
    if (++rindex == max_size)
        rindex = 0;
    mutex.Lock();
    size--;
    cond.signal();
    mutex.Unlock();
   
}

void FFMPEGFrameQueue::lock() {
    mutex.Lock();
}

void FFMPEGFrameQueue::unlock() {
    mutex.Unlock();
}

int FFMPEGFrameQueue::get_nb_remaining() {
     return size - rindex_shown;
}

int64_t FFMPEGFrameQueue::get_queue_last_pos() {
    FFMPEGFrame *fp = queue[rindex];
    if (rindex_shown && fp->get_serial() == pktq->get_serial())
        return fp->get_pos();
    else
        return -1;
}

int  FFMPEGFrameQueue::get_rindex_shown() {
    return rindex_shown;
}
