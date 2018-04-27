#include "FFMPEGDecoder.h"
#include "LambdaRunnable.h"


FFMPEGDecoder::FFMPEGDecoder() {
    decoder_reorder_pts = -1;
    queue = NULL;
    avctx = NULL;
    pkt_serial = -1;
    finished = 0;
    packet_pending = 0;
    empty_queue_cond = NULL;
    start_pts = 0;
    start_pts_tb = {0,0};
    next_pts = 0;
    next_pts_tb = {0, 0};
    pkt = {0};
}


FFMPEGDecoder::~FFMPEGDecoder()
{
}

void FFMPEGDecoder::init(AVCodecContext *avctx, FFMPEGPacketQueue *queue, CondWait *empty_queue_cond) {
    this->avctx = avctx;
    this->queue = queue;
    this->empty_queue_cond = empty_queue_cond;
    this->start_pts = AV_NOPTS_VALUE;
    this->pkt_serial = -1;
}

int FFMPEGDecoder::decode_frame( AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    for (;;) {
        AVPacket pkt;

        if (queue->get_serial() == pkt_serial) {
            do {
                if (queue->get_abort_request())
                    return -1;

                switch (avctx->codec_type) {
                case AVMEDIA_TYPE_VIDEO:
                    ret = avcodec_receive_frame(avctx, frame);
                    if (ret >= 0) {
                        if (decoder_reorder_pts == -1) {
                            frame->pts = frame->best_effort_timestamp;
                        }
                        else if (!decoder_reorder_pts) {
                            frame->pts = frame->pkt_dts;
                        }
                    }
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    ret = avcodec_receive_frame(avctx, frame);
                    if (ret >= 0) {
                        AVRational tb = { 1, frame->sample_rate };
                        if (frame->pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(frame->pts, av_codec_get_pkt_timebase(avctx), tb);
                        else if (next_pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(next_pts, next_pts_tb, tb);
                        if (frame->pts != AV_NOPTS_VALUE) {
                            next_pts = frame->pts + frame->nb_samples;
                            next_pts_tb = tb;
                        }
                    }
                    break;
                }
                if (ret == AVERROR_EOF) {
                    finished = pkt_serial;
                    avcodec_flush_buffers(avctx);
                    return 0;
                }
                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (queue->get_nb_packets() == 0)
                empty_queue_cond->signal();
            if (packet_pending) {
                av_packet_move_ref(&pkt, &pkt);
                packet_pending = 0;
            }
            else {
                if (queue->get(&pkt, 1, &pkt_serial) < 0)
                    return -1;
            }
        } while (queue->get_serial() != pkt_serial);

        if (FFMPEGPacketQueue::is_flush_packet(pkt.data)) {
            avcodec_flush_buffers(avctx);
            finished = 0;
            next_pts = start_pts;
            next_pts_tb = start_pts_tb;
        }
        else {
            if (avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                int got_frame = 0;
                ret = avcodec_decode_subtitle2(avctx, sub, &got_frame, &pkt);
                if (ret < 0) {
                    ret = AVERROR(EAGAIN);
                }
                else {
                    if (got_frame && !pkt.data) {
                        packet_pending = 1;
                        av_packet_move_ref(&pkt, &pkt);
                    }
                    ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
                }
            }
            else {
                if (avcodec_send_packet(avctx, &pkt) == AVERROR(EAGAIN)) {
                    av_log(avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                    packet_pending = 1;
                    av_packet_move_ref(&pkt, &pkt);
                }
            }
            av_packet_unref(&pkt);
        }
    }

    return ret;
}

void FFMPEGDecoder::set_decoder_reorder_pts ( int pts ) {
    decoder_reorder_pts = pts;
}

void  FFMPEGDecoder::destroy() {    
    av_packet_unref(&pkt);
    avcodec_free_context(&avctx);
}

void FFMPEGDecoder::abort(FFMPEGFrameQueue* fq) {
    queue->abort();
    fq->signal();
    queue->flush();
}

int FFMPEGDecoder::start() {
    queue->start();

    return 0;
}

AVCodecContext*  FFMPEGDecoder::get_avctx() {
    return avctx;
}

int  FFMPEGDecoder::get_pkt_serial() {
    return pkt_serial;
}

int  FFMPEGDecoder::get_finished() {
    return finished;
}

void  FFMPEGDecoder::set_time ( int64_t start_pts, AVRational  start_pts_tb) {
    this->start_pts = start_pts;
    this->start_pts_tb = start_pts_tb;
}

void  FFMPEGDecoder::set_finished ( int finished ) {
    this->finished = finished;
}

