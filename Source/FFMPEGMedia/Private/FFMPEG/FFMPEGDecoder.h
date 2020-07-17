#pragma once


#include "CondWait.h"
#include "FFMPEGPacketQueue.h"
#include "FFMPEGFrameQueue.h"
#include <thread>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
}


class FFMPEGDecoder
{
public:
    FFMPEGDecoder();
    ~FFMPEGDecoder();

    void Init(AVCodecContext *avctx1, FFMPEGPacketQueue *queue1, CondWait *empty_queue_cond1);
    int DecodeFrame( AVFrame *frame, AVSubtitle *sub);
    void SetDecoderReorderPts ( int pts );
    void Abort(FFMPEGFrameQueue* fq);
    void Destroy();
    int Start(std::function<int (void *)> thread_func, void *arg );

    AVCodecContext* GetAvctx();
    int GetPktSerial();
    int GetFinished();
    
    void SetTime ( int64_t start_pts1, AVRational  start_pts_tb1);
    void SetFinished ( int finished1 );

private:
    int decoder_reorder_pts;
    AVPacket pkt;
    FFMPEGPacketQueue *queue;
    AVCodecContext *avctx;
    int pkt_serial;
    int finished;
    bool packet_pending;
    CondWait *empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;

    std::thread *decoder_tid;
};

