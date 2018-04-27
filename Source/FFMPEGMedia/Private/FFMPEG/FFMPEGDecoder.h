#pragma once


#include "CondWait.h"
#include "FFMPEGPacketQueue.h"
#include "FFMPEGFrameQueue.h"


extern "C" {
#include <libavcodec/avcodec.h>
}


class FFMPEGDecoder
{
public:
    FFMPEGDecoder();
    ~FFMPEGDecoder();

    void init(AVCodecContext *avctx, FFMPEGPacketQueue *queue, CondWait *empty_queue_cond);
    int decode_frame( AVFrame *frame, AVSubtitle *sub);
    void set_decoder_reorder_pts ( int pts );
    void abort(FFMPEGFrameQueue* fq);
    void destroy();
    int start();

    AVCodecContext* get_avctx();
    int get_pkt_serial();
    int get_finished();
    
    void set_time ( int64_t start_pts, AVRational  start_pts_tb);
    void set_finished ( int finished );

private:
    int decoder_reorder_pts;
    AVPacket pkt;
    FFMPEGPacketQueue *queue;
    AVCodecContext *avctx;
    int pkt_serial;
    int finished;
    int packet_pending;
    CondWait *empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
};

