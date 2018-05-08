#include "FFMPEGDecoder.h"


FFMPEGDecoder::FFMPEGDecoder() {
    decoder_reorder_pts = -1;
    queue = NULL;
    avctx = NULL;
    pkt_serial = -1;
    finished = 0;
    packet_pending = false;
    empty_queue_cond = NULL;
    start_pts = 0;
    start_pts_tb = {0,0};
    next_pts = 0;
    next_pts_tb = {0, 0};
    pkt = {0};
    decoder_tid = NULL;
}


FFMPEGDecoder::~FFMPEGDecoder()
{
}

void FFMPEGDecoder::Init(AVCodecContext *avctx, FFMPEGPacketQueue *queue, CondWait *empty_queue_cond) {
    this->avctx = avctx;
    this->queue = queue;
    this->empty_queue_cond = empty_queue_cond;
    this->start_pts = AV_NOPTS_VALUE;
    this->pkt_serial = -1;
}

int FFMPEGDecoder::DecodeFrame( AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    for (;;) {
        AVPacket pkt;

        if (queue->GetSerial() == pkt_serial) {
            do {
                if (queue->IsAbortRequest())
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
                            frame->pts = av_rescale_q(frame->pts, avctx->pkt_timebase, tb);
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
            if (queue->GetNumPackets() == 0)
                empty_queue_cond->signal();
            if (packet_pending) {
                av_packet_move_ref(&pkt, &pkt);
                packet_pending = false;
            }
            else {
                if (queue->Get(&pkt, 1, &pkt_serial) < 0)
                    return -1;
            }
        } while (queue->GetSerial() != pkt_serial);

        if (FFMPEGPacketQueue::IsFlushPacket(pkt.data)) {
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

void FFMPEGDecoder::SetDecoderReorderPts ( int pts ) {
    decoder_reorder_pts = pts;
}

void  FFMPEGDecoder::Destroy() {    
    av_packet_unref(&pkt);
    avcodec_free_context(&avctx);
}

void FFMPEGDecoder::Abort(FFMPEGFrameQueue* fq) {
    queue->Abort();
    fq->Signal();

    try {
        if (decoder_tid->joinable()) {
            decoder_tid->join();
        }
    }
    catch (std::system_error &) {
    }

    delete decoder_tid;

    queue->Flush();
}

int FFMPEGDecoder::Start(std::function<int (void *)> thread_func, void *arg ) {
    queue->Start();

    std::thread cpp_thread(thread_func, arg);
    decoder_tid = new std::thread(std::move(cpp_thread));

    if (!decoder_tid) {
        //av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
#ifdef TARGET_WIN32
    HANDLE hThread = decoder_tid->native_handle();
    int currentPriority = GetThreadPriority(hThread);

    if (currentPriority != THREAD_PRIORITY_HIGHEST &&
        SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST) == 0) {
        OFX_LOG(ofx_error, "Error setting the thread priority");
    }
#endif

    return 0;
}

AVCodecContext*  FFMPEGDecoder::GetAvctx() {
    return avctx;
}

int  FFMPEGDecoder::GetPktSerial() {
    return pkt_serial;
}

int  FFMPEGDecoder::GetFinished() {
    return finished;
}

void  FFMPEGDecoder::SetTime ( int64_t start_pts, AVRational  start_pts_tb) {
    this->start_pts = start_pts;
    this->start_pts_tb = start_pts_tb;
}

void  FFMPEGDecoder::SetFinished ( int finished ) {
    this->finished = finished;
}

