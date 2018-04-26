#include "FFMPEGVideoPlayer.h"
#include "FFMPEGDecoder.h"
//#include "OpenALAudioDevice.h"
#include "FFMPEGFrame.h"


extern  "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "libswscale/swscale.h"
#include "libavutil/rational.h"
#include "libavutil/pixdesc.h"
#if PLATFORM_WINDOWS
#include "libavutil/hwcontext.h"
#endif
#include "libswresample/swresample.h"
}


/* Minimum  audio buffer size, in samples. */
#define AUDIO_MIN_BUFFER_SIZE 512

/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define AUDIO_MAX_CALLBACKS_PER_SEC 30

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01




struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
};



bool FFMPEGVideoPlayer::initialized = false;

FFMPEGVideoPlayer::FFMPEGVideoPlayer()
{

    read_tid = NULL;
    iformat = NULL;
    abort_request = 0;
    force_refresh = 0;
    paused = 0;
    last_paused = 0;
    queue_attachments_req= 0;
    seek_req= 0;
    seek_flags= 0;
    seek_pos= 0;
    seek_rel= 0;
    read_pause_return= 0;
    ic= 0;
    realtime= 0;

    auddec = std::shared_ptr<FFMPEGDecoder>(new FFMPEGDecoder());
    viddec = std::shared_ptr<FFMPEGDecoder>(new FFMPEGDecoder());
    subdec = std::shared_ptr<FFMPEGDecoder>(new FFMPEGDecoder());

    audio_stream = -1;

    
    audio_clock = 0;
    audio_clock_serial = 0;
    audio_diff_cum = 0; 
    audio_diff_avg_coef = 0;
    audio_diff_threshold = 0;
    audio_diff_avg_count = 0;
    audio_st = NULL;
    
    audio_hw_buf_size = 0;
    audio_buf = NULL;
    audio_buf1 = NULL;
    audio_buf_size = 0; 
    audio_buf1_size = 0;
    audio_buf_index = 0;
    audio_write_buf_size= 0;
    audio_volume= 0;
    muted= 0;
    
    audio_src = std::shared_ptr<AudioParams>( new AudioParams );
    memset(audio_src.get(), 0, sizeof(AudioParams));
    audio_tgt = std::shared_ptr<AudioParams>( new AudioParams() );
    memset(audio_tgt.get(), 0, sizeof(AudioParams));
    
    swr_ctx = NULL;
    frame_drops_early = 0;
    frame_drops_late = 0;



    subtitle_stream = -1;
    subtitle_st = NULL;
    
    frame_timer = 0;
    frame_last_returned_time = 0;
    frame_last_filter_delay = 0;
    video_stream = -1;
    video_st = 0;
    
    max_frame_duration = 0;      
    img_convert_ctx = NULL;
    sub_convert_ctx = NULL;
    eof = 0;
    width = 0; 
    height = 0; 
    step = 0;
    last_video_stream = -1; 
    last_audio_stream = -1; 
    last_subtitle_stream= -1;          
    
    infinite_buffer = -1;
    av_sync_type = AV_SYNC_AUDIO_MASTER;
    framedrop = -1;
    audio_callback_time = 0;
    

    if ( !initialized) {
        initialized = true;
        av_log_set_flags(AV_LOG_SKIP_REPEATED);
        av_register_all();
        avformat_network_init();
        av_log_set_level(AV_LOG_INFO);
    }

    new_frame = false;
    nFrames = 0;
    dFps = 0.0;
    currentPos = 0.0;
    v_flipped = false;
    remaining_time = 0;
    useHwAccelCodec = true;
    lastFrame = -1;
}


FFMPEGVideoPlayer::~FFMPEGVideoPlayer()
{
}


FString FFMPEGVideoPlayer::getThreadName() {
    return "";    
}

void FFMPEGVideoPlayer::setFrameByFrame(bool val) {
    
}

bool FFMPEGVideoPlayer::isFrameByFrame() {
    return false;
}

bool FFMPEGVideoPlayer::hasAudio() {
    return audio_stream >= 0;
}

bool FFMPEGVideoPlayer::loadMovie(const FString& name) {
    close();
    
    abort_request = 0;
    filename = name;
    iformat = NULL;

    if (pictq.init(&videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0) {
        close();
        return false;
    }
    
    if (subpq.init(&subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0) {
        close();
        return false;
    }
    
    if (sampq.init(&audioq, SAMPLE_QUEUE_SIZE, 1) < 0) {
        close();
        return false;
    }
   

    vidclk.init(&videoq);
    audclk.init(&audioq);
    extclk.init(&extclk);
    audio_clock_serial = -1;

    
    audio_callback_time = 0;
    audio_volume = 1.0;
    muted = 0;
    
    
    ic = NULL;

    int err,  ret;
    unsigned int i;
    int st_index[AVMEDIA_TYPE_NB];
    
    


    memset(st_index, -1, sizeof(st_index));
    last_video_stream = video_stream = -1;
    last_audio_stream = audio_stream = -1;
    last_subtitle_stream = subtitle_stream = -1;
    eof = 0;

    int scan_all_pmts_set = 0;
    AVDictionary *format_opts = NULL;
    ic = avformat_alloc_context();
    if (!ic) {
        close();
        UE_LOG(LogVideoPlayer, Warning, TEXT("Could not allocate context.\n"));
        ret = AVERROR(ENOMEM);
        return false;
    }
    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = this;

    for ( auto entry: formatOptions) {
        av_dict_set(&format_opts, TCHAR_TO_UTF8(*entry.Key), TCHAR_TO_UTF8(*entry.Value), AV_DICT_DONT_OVERWRITE);
    }


    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }

    

    err = avformat_open_input(&ic, TCHAR_TO_UTF8(*filename), NULL, &format_opts);
    if (err < 0) {
        char errbuf[128];
        const char *errbuf_ptr = errbuf;

        if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
            strerror_s(errbuf, 128, AVUNERROR(err));
         
        UE_LOG(LogVideoPlayer, Error, TEXT( "%s: %s\n"), *filename, UTF8_TO_TCHAR(errbuf_ptr));
        av_dict_free(&format_opts);
        ret = -1;
        return false;
    }

    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    AVDictionaryEntry *t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX);
    if (t) {
        UE_LOG(LogVideoPlayer, Error, TEXT( "Option %s not found.\n"), UTF8_TO_TCHAR(t->key));
        ret = AVERROR_OPTION_NOT_FOUND;
        //goto fail;
        close();
        return false;
    }

    av_dict_free(&format_opts);

    av_format_inject_global_side_data(ic);

    err = avformat_find_stream_info(ic, NULL);

    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end


    max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    realtime = is_realtime(ic);

    if (!audioDevice.IsValid()) {
        //TODO:
        //audioDevice = std::make_shared<OpenALAudioDevice>();
        bAudioEnabled = false;
    }


    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
    }

    st_index[AVMEDIA_TYPE_VIDEO] = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);

    if (bAudioEnabled) {
        st_index[AVMEDIA_TYPE_AUDIO] = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, st_index[AVMEDIA_TYPE_AUDIO], st_index[AVMEDIA_TYPE_VIDEO], NULL, 0);
    }

    st_index[AVMEDIA_TYPE_SUBTITLE] = av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE, st_index[AVMEDIA_TYPE_SUBTITLE],
        (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ? st_index[AVMEDIA_TYPE_AUDIO] : st_index[AVMEDIA_TYPE_VIDEO]), NULL, 0);

    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret = stream_component_open(st_index[AVMEDIA_TYPE_VIDEO]);
    }

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        stream_component_open(st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    if (video_stream < 0 && audio_stream < 0) {
        UE_LOG(LogVideoPlayer, Error, TEXT("Failed to open file '%s' or configure filtergraph\n"), *filename);
        ret = -1;
        return false;
    }
    
    if (infinite_buffer < 0 && realtime)
        infinite_buffer = 1;


    if (video_st) {
        width = video_st->codecpar->width;
        height = video_st->codecpar->height;
        dFps = av_q2d(video_st->r_frame_rate);
        if (dFps < 0.000025) {
            dFps = av_q2d(video_st->avg_frame_rate);
        }
    }
    else if (audio_st) {
        dFps = av_q2d(audio_st->r_frame_rate);
        if (dFps < 0.000025) {
            dFps = av_q2d(audio_st->avg_frame_rate);
        }
    }

    nFrames = (float)(ic->duration / (float)AV_TIME_BASE) * dFps;
       
    read_tid = new std::thread(read_thread, this);

    if (!read_tid) {
        return false;
    }

    setPaused(true);
    
    return true;
}

void FFMPEGVideoPlayer::close() {
    lastFrame = -1;
    abort_request = 1;
    remaining_time  = 0;
    if ( read_tid) {
        try {
            if (read_tid->joinable()) {
                read_tid->join();
            }
        }
        catch (std::system_error &) {
        }
        delete read_tid;
        read_tid = NULL;
    }

    /* close each stream */
    if (audio_stream >= 0)
        stream_component_close(audio_stream);
    if (video_stream >= 0)
        stream_component_close(video_stream);
    if (subtitle_stream >= 0)
        stream_component_close(subtitle_stream);

    if ( ic ) {
        avformat_close_input(&ic);
        ic = NULL;
    }

    if ( img_convert_ctx) {
        sws_freeContext(img_convert_ctx);
        img_convert_ctx = NULL;
    }
    if ( sub_convert_ctx ) {
        sws_freeContext(sub_convert_ctx);
        sub_convert_ctx = NULL;
    }
   

    nFrames = 0;
    dFps = 0.0;
    currentPos = 0.0;
    

}

void FFMPEGVideoPlayer::update() {
    
    if (remaining_time > 0.0)
        av_usleep((int64_t)(remaining_time * 1000000.0));

    remaining_time = REFRESH_RATE;

    if ((!paused || force_refresh))
        video_refresh(&remaining_time);

    if ( videoFrameCallback ) {
        int nFrame = getCurrentFrame();
        if ( nFrame != lastFrame) {
            videoFrameCallback(this, filename, nFrame);
            lastFrame = nFrame;
        }
    }
    
}


void FFMPEGVideoPlayer::play() {
    startThread();
    setPaused(false);
}

void FFMPEGVideoPlayer::stop() {
    setPaused(true);
    stopThread();
}

bool  FFMPEGVideoPlayer::isFlipped() {
    return v_flipped;
}

float FFMPEGVideoPlayer::getWidth() const {
    return width;
}

float FFMPEGVideoPlayer::getHeight() const {
    return height;
}

bool FFMPEGVideoPlayer::isFrameNew() {
    return new_frame;
}

bool FFMPEGVideoPlayer::isPaused() {
    return paused != 0;
}

bool FFMPEGVideoPlayer::isLoaded() {
    return ic != NULL;
}

bool FFMPEGVideoPlayer::isPlaying() {
    return isLoaded() && !paused;
}

bool FFMPEGVideoPlayer::isImage(){
    return nFrames < 2;
}

//should be implemented
float FFMPEGVideoPlayer::getPosition(){
    return currentPos / getDuration();
}

float FFMPEGVideoPlayer::getSpeed() {
    return 1.0;
}

float FFMPEGVideoPlayer::getDuration(){
    if ( ic ) {
        return ic->duration / (float)AV_TIME_BASE;
    }
    return 0.0f;
}

bool  FFMPEGVideoPlayer::getIsMovieDone() {
    return false;
}

float FFMPEGVideoPlayer::getVolume() {
    if ( audioDevice.IsValid()) {
        return audioDevice->getVolume();
    }
    return 0.0f;
}

void FFMPEGVideoPlayer::setPaused(bool bPause) {
    bool _paused = paused != 0;
    if ( _paused != bPause) {
        stream_toggle_pause();
    }
}

void FFMPEGVideoPlayer::setPosition(float pct) {
    int64_t pos = (int64_t)((double)pct*(double)ic->duration);
    stream_seek(pos, 0, 0);
}

void FFMPEGVideoPlayer::setVolume(float volume) {
    if (audioDevice.IsValid()) {
        return audioDevice->setVolume(volume);
    }
}

void FFMPEGVideoPlayer::setLoopState(LoopType state) {
    this->state = state;
}

void FFMPEGVideoPlayer::setSpeed(float speed) {
    
}

void FFMPEGVideoPlayer::setFrame(int frame) {
    float pct = (float)frame / (float)nFrames;
    setPosition(pct);
}  

int	FFMPEGVideoPlayer::getCurrentFrame() {
    int frame = 0;

    // zach I think this may fail on variable length frames...
    float pos = getPosition();
    if (pos == -1) return -1;


    float  framePosInFloat = ((float)getTotalNumFrames() * pos);
    int    framePosInInt = (int)framePosInFloat;
    float  floatRemainder = (framePosInFloat - framePosInInt);
    if (floatRemainder > 0.5f) framePosInInt = framePosInInt + 1;
    //frame = (int)ceil((getTotalNumFrames() * getPosition()));
    frame = framePosInInt;

    return frame;
}

int	FFMPEGVideoPlayer::getTotalNumFrames() {
    return nFrames;
}

int	FFMPEGVideoPlayer::getLoopState() {
    return state;
}

void FFMPEGVideoPlayer::firstFrame() {
    setFrame(0);
}

void FFMPEGVideoPlayer::nextFrame() {
    int currentFrame = getCurrentFrame();
    if (currentFrame != -1) setFrame(currentFrame + 1);
}

void FFMPEGVideoPlayer::previousFrame() {
    int currentFrame = getCurrentFrame();
    if (currentFrame != -1) setFrame(currentFrame - 1);
}

int FFMPEGVideoPlayer::is_realtime(AVFormatContext *s) {
    if (!strcmp(s->iformat->name, "rtp")
        || !strcmp(s->iformat->name, "rtsp")
        || !strcmp(s->iformat->name, "sdp")
        )
        return 1;

    if (s->pb && (!strncmp(s->filename, "rtp:", 4)
        || !strncmp(s->filename, "udp:", 4)
        )
        )
        return 1;
    return 0;
}

int FFMPEGVideoPlayer::decode_interrupt_cb(void *ctx) {
    return ((FFMPEGVideoPlayer*)ctx)->abort_request;
}

int FFMPEGVideoPlayer::read_thread(void* data) {
    return ((FFMPEGVideoPlayer*)data)->read_thread_internal();
}

int FFMPEGVideoPlayer::audio_thread(void* data) {
    return ((FFMPEGVideoPlayer*)data)->audio_thread_internal();
}

int FFMPEGVideoPlayer::video_thread(void* data) {
    return ((FFMPEGVideoPlayer*)data)->video_thread_internal();
}

int FFMPEGVideoPlayer::subtitle_thread(void* data) {
    return ((FFMPEGVideoPlayer*)data)->subtitle_thread_internal();
}

int FFMPEGVideoPlayer::read_thread_internal() {
  
    std::recursive_mutex wait_mutex;
    AVPacket pkt1, *pkt = &pkt1;
    int64_t stream_start_time;
    int pkt_in_play_range = 0;

    int scan_all_pmts_set = 0;
    int64_t pkt_ts;

    int ret = 0;

    for (;;) {
        if (abort_request)
            break;
        if (paused != last_paused) {
            last_paused = paused;
            if (paused)
                read_pause_return = av_read_pause(ic);
            else
                av_read_play(ic);
        }

        if (seek_req) {
            int64_t seek_target = seek_pos;
            int64_t seek_min = seek_rel > 0 ? seek_target - seek_rel + 2 : INT64_MIN;
            int64_t seek_max = seek_rel < 0 ? seek_target - seek_rel - 2 : INT64_MAX;
            // FIXME the +-2 is due to rounding being not done in the correct direction in generation
            //      of the seek_pos/seek_rel variables

            ret = avformat_seek_file(ic, -1, seek_min, seek_target, seek_max, seek_flags);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                    "%s: error while seeking\n", ic->filename);
            }
            else {
                if (audio_stream >= 0) {
                    audioq.flush();
                    audioq.put_flush();
                }
                if (subtitle_stream >= 0) {
                    subtitleq.flush();
                    subtitleq.put_flush();
                }
                if (video_stream >= 0) {
                    videoq.flush();
                    videoq.put_flush();
                }
                if (seek_flags & AVSEEK_FLAG_BYTE) {
                    extclk.set(NAN, 0);
                }
                else {
                    extclk.set(seek_target / (double)AV_TIME_BASE, 0);
                }
            }
            seek_req = 0;
            queue_attachments_req = 1;
            eof = 0;
            if (paused)
                step_to_next_frame();
        }

        if (queue_attachments_req) {
            if (video_st && video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket copy = { 0 };
                if ((ret = av_packet_ref(&copy, &video_st->attached_pic)) < 0) {
                    return -1;
                }
                videoq.put(&copy);
                videoq.put_nullpacket(video_stream);
            }
            queue_attachments_req = 0;
        }

        /* if the queue are full, no need to read more */
        if (infinite_buffer < 1 &&
            (audioq.get_size() + videoq.get_size() + subtitleq.get_size() > MAX_QUEUE_SIZE
                || (stream_has_enough_packets(audio_st, audio_stream, &audioq) &&
                    stream_has_enough_packets(video_st, video_stream, &videoq) &&
                    stream_has_enough_packets(subtitle_st, subtitle_stream, &subtitleq)))) {
            /* wait 10 ms */
            wait_mutex.lock();
            continue_read_thread.waitTimeout(wait_mutex, 10);
            wait_mutex.unlock();
            continue;
        }
        if (!paused &&
            (!audio_st || (auddec->get_finished() == audioq.get_serial() && sampq.get_nb_remaining() == 0)) &&
            (!video_st || (viddec->get_finished() == videoq.get_serial() && pictq.get_nb_remaining() == 0))) {

            if (videoFinishedCallback)
                videoFinishedCallback(this, filename);



            if ( state == LOOP_NORMAL) {
                stream_seek(0, 0, 0);
            } else if  (state == LOOP_PALINDROME) {
                UE_LOG(LogVideoPlayer, Warning, TEXT("Loop Palindrome is not supported by FFMPEG Player"));
                paused = true;
            } else {
                paused = true;
            }
 
            
        }
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !eof) {
                if (video_stream >= 0)
                    videoq.put_nullpacket(video_stream);
                if (audio_stream >= 0)
                    audioq.put_nullpacket(audio_stream);
                if (subtitle_stream >= 0)
                    subtitleq.put_nullpacket(subtitle_stream);
                eof = 1;
            }
            if (ic->pb && ic->pb->error)
                break;

            wait_mutex.lock();
            continue_read_thread.waitTimeout(wait_mutex, 10);
            wait_mutex.unlock();
            continue;
        }
        else {
            eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;

        pkt_in_play_range =  1;
        if (pkt->stream_index == audio_stream && pkt_in_play_range) {
            audioq.put(pkt);
        }
        else if (pkt->stream_index == video_stream && pkt_in_play_range
            && !(video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            videoq.put(pkt);
        }
        else if (pkt->stream_index == subtitle_stream && pkt_in_play_range) {
            subtitleq.put(pkt);
        }
        else {
            av_packet_unref(pkt);
        }
    }

    return 0;
}




int FFMPEGVideoPlayer::stream_has_enough_packets(AVStream *st, int stream_id, FFMPEGPacketQueue *queue) {
    return stream_id < 0 ||
        queue->get_abort_request() ||
        (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        queue->get_nb_packets() > MIN_FRAMES && (!queue->get_duration() || av_q2d(st->time_base) * queue->get_duration() > 1.0);
}

void FFMPEGVideoPlayer::stream_seek( int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!seek_req) {
        seek_pos = pos;
        seek_rel = rel;
        seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
            seek_flags |= AVSEEK_FLAG_BYTE;
        seek_req = 1;
        continue_read_thread.signal();
    }
}

void  FFMPEGVideoPlayer::stream_toggle_pause()
{
    if (paused) {
        frame_timer += av_gettime_relative() / 1000000.0 - vidclk.get_last_updated();
        if (read_pause_return != AVERROR(ENOSYS)) {
            vidclk.set_paused(0);
        }
        vidclk.set(vidclk.get(), vidclk.get_serial());
    }
    extclk.set(extclk.get(), extclk.get_serial());
    paused = !paused;

    audclk.set_paused(paused);
    vidclk.set_paused(paused);
    extclk.set_paused(paused);
}


void FFMPEGVideoPlayer::step_to_next_frame()
{
    /* if the stream is paused unpause it, then step */
    if (paused)
        stream_toggle_pause();
    
    step = 1;
}

int FFMPEGVideoPlayer::stream_component_open(int stream_index) {
    AVCodecContext *avctx;
    AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts = NULL;
    
    AVDictionaryEntry *t = NULL;
    int sample_rate, nb_channels;
    int64_t channel_layout;
    int ret = 0;

    int fast = 0;
    int stream_lowres = 0;

    if (stream_index < 0 || stream_index >= (int)ic->nb_streams)
        return -1;

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0) {
        avcodec_free_context(&avctx);
        return ret;
    }
    av_codec_set_pkt_timebase(avctx, ic->streams[stream_index]->time_base);

    //TODO: Force harware
    if ( useHwAccelCodec && avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        codec = findDecoder(avctx->codec_id, true);
        if ( !codec ) {
            codec = avcodec_find_decoder(avctx->codec_id);    
        }
    } else {
        codec = avcodec_find_decoder(avctx->codec_id);
    }

   
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with name '%s'\n", forced_codec_name);
        else                   av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with id %d\n", avctx->codec_id);
        ret = AVERROR(EINVAL);
        avcodec_free_context(&avctx);
        return ret;
    }

    avctx->codec_id = codec->id;
    if(stream_lowres > av_codec_get_max_lowres(codec)){
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                av_codec_get_max_lowres(codec));
        stream_lowres = av_codec_get_max_lowres(codec);
    }
    av_codec_set_lowres(avctx, stream_lowres);

    if(stream_lowres) avctx->flags |= CODEC_FLAG_EMU_EDGE;

    if (fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;
#if FF_API_EMU_EDGE
    if(codec->capabilities & AV_CODEC_CAP_DR1)
        avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif

    if ( codecOptions.Contains(avctx->codec_type) ) {
        const TMap<FString, FString>& options =  codecOptions [avctx->codec_type];
        for (auto entry: options) {
            av_dict_set(&opts, TCHAR_TO_UTF8(*entry.Key), TCHAR_TO_UTF8(*entry.Value), 0);
        }
    }

    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);

    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_dict_set(&opts, "refcounted_frames", "1", 0);
    
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        if ( useHwAccelCodec && avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
              //UE_LOG(LogVideoPlayer, Warning, TEXT("Coudn't open the hwaccel codec, trying a different one"));
            codec = avcodec_find_decoder(avctx->codec_id);
            avctx->codec_id = codec->id;
            if (stream_lowres > av_codec_get_max_lowres(codec)) {
                av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                    av_codec_get_max_lowres(codec));
                stream_lowres = av_codec_get_max_lowres(codec);
            }
            av_codec_set_lowres(avctx, stream_lowres);
            if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
                avcodec_free_context(&avctx);
                return ret;
            }

        } else {
            avcodec_free_context(&avctx);
            return ret;
        }
    }
    t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX);
    if (t) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret =  AVERROR_OPTION_NOT_FOUND;
        avcodec_free_context(&avctx);
        return ret;
    }

    eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;

        /* prepare audio output */
        if ((ret = audio_open(channel_layout, nb_channels, sample_rate, audio_tgt.get())) < 0) {
            avcodec_free_context(&avctx);
            return ret;
        }
        audio_hw_buf_size = ret;
        //audio_src = audio_tgt;

        audio_src->freq = audio_tgt->freq;
        audio_src->channels = audio_tgt->channels;
        audio_src->channel_layout= audio_tgt->channel_layout;
        audio_src->fmt = audio_tgt->fmt;
        audio_src->frame_size = audio_tgt->frame_size;
        audio_src->bytes_per_sec = audio_tgt->bytes_per_sec;

        audio_buf_size  = 0;
        audio_buf_index = 0;

        /* init averaging filter */
        audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
           we correct audio sync only if larger than this threshold */
        audio_diff_threshold = (double)(audio_hw_buf_size) / audio_tgt->bytes_per_sec;

        audio_stream = stream_index;
        audio_st = ic->streams[stream_index];

        auddec->init(avctx, &audioq,  &continue_read_thread);
        if ((ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !ic->iformat->read_seek) {
            auddec->set_time( audio_st->start_time, audio_st->time_base);
        }
        if ((ret = auddec->start(audio_thread, this)) < 0) {
            av_dict_free(&opts);
            return ret;
        }

        ic->audio_codec = codec;
        
        break;
    case AVMEDIA_TYPE_VIDEO:
        video_stream = stream_index;
        video_st = ic->streams[stream_index];

        viddec->init(avctx, &videoq, &continue_read_thread);
        if ((ret = viddec->start(video_thread, this)) < 0) {
            av_dict_free(&opts);
            return ret;
        }
        queue_attachments_req = 1;
        ic->video_codec = codec;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        subtitle_stream = stream_index;
        subtitle_st = ic->streams[stream_index];

        subdec->init(avctx, &subtitleq, &continue_read_thread);
        if ((ret = subdec->start(subtitle_thread, this)) < 0) {
            av_dict_free(&opts);
            return ret;
        }
        ic->subtitle_codec = codec;
        break;
    default:
        break;
    }
    
    av_dict_free(&opts);

/*fail:
    avcodec_free_context(&avctx);*/


    return ret;
}

void FFMPEGVideoPlayer::stream_component_close( int stream_index)
{
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= (int)ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        if ( audioDevice.IsValid()) {
            audioDevice->close();
        }

        auddec->abort(&sampq);
        
        auddec->destroy();
        swr_free(&swr_ctx);
        swr_ctx = NULL;
        av_freep(&audio_buf1);
        audio_buf1_size = 0;
        audio_buf = NULL;
        break;
    case AVMEDIA_TYPE_VIDEO:
        viddec->abort(&pictq);
        viddec->destroy();
        video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        subdec->abort(&subpq);
        subdec->destroy();
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        audio_st = NULL;
        audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        video_st = NULL;
        video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        subtitle_st = NULL;
        subtitle_stream = -1;
        break;
    default:
        break;
    }
}


int  FFMPEGVideoPlayer::audio_open(int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params) {
    static const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };
    static const int next_sample_rates[] = { 0, 44100, 48000, 96000, 192000 };
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);

    int samples = FFMAX(AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_sample_rate / AUDIO_MAX_CALLBACKS_PER_SEC));

    if (wanted_sample_rate <= 0 || wanted_nb_channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = wanted_sample_rate;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels = wanted_nb_channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }

    

    audioDevice->openAudioDevice();
    audioDevice->setConfiguration(wanted_nb_channels, samples, audio_hw_params->frame_size, wanted_sample_rate, SAMPLE_FMT_S16);
    
    audioDevice->setAudioCallBack([this](unsigned char* stream, int len) {
        audio_callback(stream, len);
    });

    audioDevice->start();


    int bufferSize = av_samples_get_buffer_size(NULL, audio_hw_params->channels, samples, AV_SAMPLE_FMT_S16, 1);
    return bufferSize;
}

void FFMPEGVideoPlayer::audio_callback(unsigned char *stream, int len)
{
    int audio_size, len1;

    int64_t audio_callback_time = av_gettime_relative();

    while (len > 0) {
        if (audio_buf_index >= (int)audio_buf_size) {
            audio_size = audio_decode_frame();
            if (audio_size < 0) {
                /* if error, just output silence */
                audio_buf = NULL;
                audio_buf_size = AUDIO_MIN_BUFFER_SIZE / audio_tgt->frame_size * audio_tgt->frame_size;
            }
            else {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len)
            len1 = len;
        if (!muted && audio_buf )
            memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        else {
            memset(stream, 0, len1);
            /*if (!is->muted && audio_buf)
                SDL_MixAudioFormat(stream, (uint8_t *)audio_buf + audio_buf_index, AUDIO_S16SYS, len1, audio_volume);*/
        }
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
    audio_write_buf_size = audio_buf_size - audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(audio_clock)) {
        audclk.set_at(audio_clock - (double)(2 * audio_hw_buf_size + audio_write_buf_size) / audio_tgt->bytes_per_sec, audio_clock_serial, audio_callback_time / 1000000.0);
        extclk.sync_to_slave(&audclk);
    }
}

int FFMPEGVideoPlayer::audio_decode_frame () {
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    FFMPEGFrame *af;

    if (paused)
        return -1;

    do {
#if defined(_WIN32)
        while (sampq.get_nb_remaining() == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * audio_hw_buf_size / audio_tgt->bytes_per_sec / 2)
                return -1;
            av_usleep(1000);
        }
#endif
        af = sampq.peek_readable();
        if (!af)
            return -1;
        sampq.next();
    } while (af->get_serial() != audioq.get_serial());

    data_size = av_samples_get_buffer_size(NULL, af->get_frame()->channels,
        af->get_frame()->nb_samples,
        (AVSampleFormat)af->get_frame()->format, 1);

    dec_channel_layout =
        (af->get_frame()->channel_layout && af->get_frame()->channels == av_get_channel_layout_nb_channels(af->get_frame()->channel_layout)) ?
        af->get_frame()->channel_layout : av_get_default_channel_layout(af->get_frame()->channels);
    wanted_nb_samples = synchronize_audio( af->get_frame()->nb_samples);

    if (af->get_frame()->format != audio_src->fmt ||
        dec_channel_layout != audio_src->channel_layout ||
        af->get_frame()->sample_rate != audio_src->freq ||
        (wanted_nb_samples != af->get_frame()->nb_samples && !swr_ctx)) {
        swr_free(&swr_ctx);
        swr_ctx = swr_alloc_set_opts(NULL,
            audio_tgt->channel_layout, audio_tgt->fmt, audio_tgt->freq,
            dec_channel_layout, (AVSampleFormat)af->get_frame()->format, af->get_frame()->sample_rate,
            0, NULL);
        if (!swr_ctx || swr_init(swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                af->get_frame()->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->get_frame()->format), af->get_frame()->channels,
                audio_tgt->freq, av_get_sample_fmt_name(audio_tgt->fmt), audio_tgt->channels);
            swr_free(&swr_ctx);
            return -1;
        }
        audio_src->channel_layout = dec_channel_layout;
        audio_src->channels = af->get_frame()->channels;
        audio_src->freq = af->get_frame()->sample_rate;
        audio_src->fmt = (AVSampleFormat)af->get_frame()->format;
    }

    if (swr_ctx) {
        const uint8_t **in = (const uint8_t **)af->get_frame()->extended_data;
        uint8_t **out = &audio_buf1;
        int out_count = (int64_t)wanted_nb_samples * audio_tgt->freq / af->get_frame()->sample_rate + 256;
        int out_size = av_samples_get_buffer_size(NULL, audio_tgt->channels, out_count, audio_tgt->fmt, 0);
        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->get_frame()->nb_samples) {
            if (swr_set_compensation(swr_ctx, (wanted_nb_samples - af->get_frame()->nb_samples) * audio_tgt->freq / af->get_frame()->sample_rate,
                wanted_nb_samples * audio_tgt->freq / af->get_frame()->sample_rate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&audio_buf1, &audio_buf1_size, out_size);
        if (!audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(swr_ctx, out, out_count, in, af->get_frame()->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(swr_ctx) < 0)
                swr_free(&swr_ctx);
        }
        audio_buf = audio_buf1;
        resampled_data_size = len2 * audio_tgt->channels * av_get_bytes_per_sample(audio_tgt->fmt);
    }
    else {
        audio_buf = af->get_frame()->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->get_pts()))
        audio_clock = af->get_pts() + (double)af->get_frame()->nb_samples / af->get_frame()->sample_rate;
    else
        audio_clock = NAN;
    audio_clock_serial = af->get_serial();
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
            audio_clock - last_clock,
            audio_clock, audio_clock0);
        last_clock = audio_clock;
    }
#endif
    return resampled_data_size;
}

int FFMPEGVideoPlayer::synchronize_audio( int nb_samples) {
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type() != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = audclk.get() - get_master_clock();

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            audio_diff_cum = diff + audio_diff_avg_coef * audio_diff_cum;
            if (audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = audio_diff_cum * (1.0 - audio_diff_avg_coef);

                if (fabs(avg_diff) >= audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * audio_src->freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                        diff, avg_diff, wanted_nb_samples - nb_samples,
                        audio_clock, audio_diff_threshold);
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            audio_diff_avg_count = 0;
            audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}

int FFMPEGVideoPlayer::audio_thread_internal() {
    AVFrame *frame = av_frame_alloc();
    FFMPEGFrame *af;

    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        if ((got_frame = auddec->decode_frame(frame, NULL)) < 0)
            goto the_end;

        if (got_frame) {
            tb = { 1, frame->sample_rate };
                af = sampq.peek_writable();
                if (!af)
                    goto the_end;

                af->set_pts((frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb));
                af->set_pos(frame->pkt_pos);
                af->set_serial(auddec->get_pkt_serial());
                af->set_duration(av_q2d({ frame->nb_samples, frame->sample_rate }));

                av_frame_move_ref(af->get_frame(), frame);
                sampq.push();

        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:

    av_frame_free(&frame);
    return ret;
    
}

int FFMPEGVideoPlayer::get_master_sync_type() {
    if (av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    }
    else if (av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    }
    else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

double FFMPEGVideoPlayer::get_master_clock() {
    double val;

    switch (get_master_sync_type()) {
    case AV_SYNC_VIDEO_MASTER:
        val = vidclk.get();
        break;
    case AV_SYNC_AUDIO_MASTER:
        val = audclk.get();
        break;
    default:
        val = extclk.get();
        break;
    }
    return val;
}


void FFMPEGVideoPlayer::check_external_clock_speed() {
    if (video_stream >= 0 && videoq.get_nb_packets() <= EXTERNAL_CLOCK_MIN_FRAMES ||
        audio_stream >= 0 && audioq.get_nb_packets() <= EXTERNAL_CLOCK_MIN_FRAMES) {
        extclk.set_speed(FFMAX(EXTERNAL_CLOCK_SPEED_MIN, extclk.get_speed() - EXTERNAL_CLOCK_SPEED_STEP));
    }
    else if ((video_stream < 0 || videoq.get_nb_packets() > EXTERNAL_CLOCK_MAX_FRAMES) &&
        (audio_stream < 0 || audioq.get_nb_packets() > EXTERNAL_CLOCK_MAX_FRAMES)) {
        extclk.set_speed(FFMIN(EXTERNAL_CLOCK_SPEED_MAX, extclk.get_speed() + EXTERNAL_CLOCK_SPEED_STEP));
    }
    else {
        double speed = extclk.get_speed();
        if (speed != 1.0)
            extclk.set_speed(speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
}



int FFMPEGVideoPlayer::subtitle_thread_internal() {
    FFMPEGFrame *sp;
    int got_subtitle;
    double pts;

    for (;;) {
        sp = subpq.peek_writable();
        if (!sp)
            return 0;

        if ((got_subtitle = subdec->decode_frame(NULL, &sp->get_sub())) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->get_sub().format == 0) {
            if (sp->get_sub().pts != AV_NOPTS_VALUE)
                pts = sp->get_sub().pts / (double)AV_TIME_BASE;


            sp->set_pts(pts);
            sp->set_serial(subdec->get_pkt_serial());
            sp->set_width(subdec->get_avctx()->width);
            sp->set_height(subdec->get_avctx()->height);
            sp->set_uploaded(0);

            /* now we can update the picture count */
            subpq.push();
        }
        else if (got_subtitle) {
            avsubtitle_free(&sp->get_sub());
        }
    }
    return 0;
}

int  FFMPEGVideoPlayer::video_thread_internal() {
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(ic, video_st, NULL);


    if (!frame) {

        return AVERROR(ENOMEM);
    }

    for (;;) {
        ret = get_video_frame(frame);
        if (ret < 0) {
            av_frame_free(&frame);
            return 0;
        }
        if (!ret)
            continue;

        duration = (frame_rate.num && frame_rate.den ? av_q2d({ frame_rate.den, frame_rate.num }) : 0);
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        pictq.queue_picture(frame, pts, duration, frame->pkt_pos, viddec->get_pkt_serial());
        av_frame_unref(frame);

        if (ret < 0) {
            av_frame_free(&frame);
            return 0;
        }
    }

    av_frame_free(&frame);
    return 0;
}

int FFMPEGVideoPlayer::get_video_frame(AVFrame *frame) {
    int got_picture;

    if ((got_picture = viddec->decode_frame(frame, NULL)) < 0)
        return -1;

    if (got_picture) {
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(video_st->time_base) * frame->pts;

        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(ic, video_st, frame);

        if (framedrop > 0 || (framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock();
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - frame_last_filter_delay < 0 &&
                    viddec->get_pkt_serial() == vidclk.get_serial() &&
                    videoq.get_nb_packets()) {
                    frame_drops_early++;
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

void FFMPEGVideoPlayer::video_refresh(double *remaining_time)
{
    
    double time;

    //FFMPEGFrame *sp, *sp2;

    if (!paused && get_master_sync_type() == AV_SYNC_EXTERNAL_CLOCK && realtime)
        check_external_clock_speed();

    
    if (video_st) {
retry:
        if (pictq.get_nb_remaining() == 0) {
            // nothing to do, no picture to display in the queue
        } else {
            double last_duration, duration, delay;
            FFMPEGFrame *vp, *lastvp;


            /* dequeue the picture */
            lastvp = pictq.peek_last();
            vp = pictq.peek();
            currentPos = vp->get_pts();


            if (vp->get_serial() != videoq.get_serial()) {
                pictq.next();
                goto retry;
            }

            if (lastvp->get_serial() != vp->get_serial())
                frame_timer = av_gettime_relative() / 1000000.0;

            if (paused)
                goto display;

            if ( currentPos < 0.0001) {
                av_log(NULL, AV_LOG_INFO, "Calling video start");
                if ( videoStartedCallback ) {
                    videoStartedCallback(this, filename);
                }
            }

            /* compute nominal last_duration */
            last_duration = lastvp->get_difference(vp, max_frame_duration);
            delay = compute_target_delay(last_duration);

            time= av_gettime_relative()/1000000.0;
            if (time < frame_timer + delay) {
                *remaining_time = FFMIN(frame_timer + delay - time, *remaining_time);
                goto display;
            }

            frame_timer += delay;
            if (delay > 0 && time - frame_timer > AV_SYNC_THRESHOLD_MAX)
                frame_timer = time;

            pictq.lock();
            if (!isnan(vp->get_pts()))
                update_video_pts(vp->get_pts(), vp->get_pos(), vp->get_serial());
            pictq.unlock();

            if (pictq.get_nb_remaining() > 1) {
                FFMPEGFrame *nextvp = pictq.peek_next();
                duration = vp->get_difference(nextvp, max_frame_duration);
                if(!step && (framedrop>0 || (framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) && time > frame_timer + duration){
                    frame_drops_late++;
                    pictq.next();
                    goto retry;
                }
            }

           /* if (subtitle_st) {
                    while (subpq.get_nb_remaining() > 0) {
                        sp = subpq.peek();

                        if (subpq.get_nb_remaining() > 1)
                            sp2 = subpq.peek_next();
                        else
                            sp2 = NULL;

                        if (sp->get_serial() != subtitleq.get_serial()
                                || (vidclk.get_pts() > (sp->get_pts() + ((float) sp->get_sub().end_display_time / 1000)))
                                || (sp2 && vidclk.get_pts() > (sp2->get_pts() + ((float) sp2->get_sub().start_display_time / 1000))))
                        {
                            if (sp->get_uploaded()) {
                                int i;
                                for (i = 0; i < sp->get_sub().num_rects; i++) {
                                    AVSubtitleRect *sub_rect = sp->get_sub().rects[i];
                                    uint8_t *pixels;
                                    int pitch, j;

                                    if (!SDL_LockTexture(sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
                                        for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                            memset(pixels, 0, sub_rect->w << 2);
                                        SDL_UnlockTexture(sub_texture);
                                    }
                                }
                            }
                            subpq.next();
                        } else {
                            break;
                        }
                    }
            }*/

            pictq.next();
            force_refresh = 1;

            if (step && !paused)
                stream_toggle_pause();
        }
display:
        /* display picture */
        if (force_refresh && pictq.get_rindex_shown())
            video_display();
    }
    force_refresh = 0;
 /*   if (show_status) {
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        cur_time = av_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (audio_st)
                aqsize = audioq.get_size();
            if (video_st)
                vqsize = videoq.get_size();
            if (subtitle_st)
                sqsize = subtitleq.get_size();
            av_diff = 0;
            if (audio_st && video_st)
                av_diff = audclk.get() - vidclk.get();
            else if (video_st)
                av_diff = get_master_clock(is) - vidclk.get();
            else if (audio_st)
                av_diff = get_master_clock(is) - audclk.get();
            / *av_log(NULL, AV_LOG_INFO,
                   "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
                   get_master_clock(is),
                   (audio_st && video_st) ? "A-V" : (video_st ? "M-V" : (audio_st ? "M-A" : "   ")),
                   av_diff,
                   frame_drops_early + frame_drops_late,
                   aqsize / 1024,
                   vqsize / 1024,
                   sqsize,
                   video_st ? viddec.avctx->pts_correction_num_faulty_dts : 0,
                   video_st ? viddec.avctx->pts_correction_num_faulty_pts : 0);* /
            fflush(stdout);
            last_time = cur_time;
        }
    }*/
}

double FFMPEGVideoPlayer::compute_target_delay(double delay) {
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type() != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a FFMPEGFrame */
        diff = vidclk.get() - get_master_clock();

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < max_frame_duration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
            delay, -diff);

    return delay;
}

void FFMPEGVideoPlayer::update_video_pts(double pts, int64_t pos, int serial) {
    vidclk.set(pts, serial);
    extclk.sync_to_slave(&vidclk);
}

void FFMPEGVideoPlayer::video_display() {
    if (video_st) {
        FFMPEGFrame *vp;
        FFMPEGFrame *sp = NULL;
        //SDL_Rect rect;

        vp = pictq.peek_last();
        /*if (subtitle_st) {
            if (subpq.get_nb_remaining() > 0) {
                sp = is->subpq.peek();

                if (vp->get_pts() >= sp->get_pts() + ((float)sp->get_sub().start_display_time / 1000)) {
                    if (!sp->get_uploaded()) {
                        uint8_t* pixels[4];
                        int pitch[4];
                        int i;
                        if (!sp->get_width() || !sp->get_height()) {
                            sp->update_size(vp);
                        }
                        if (realloc_texture(&is->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->get_width(), sp->get_height(), SDL_BLENDMODE_BLEND, 1) < 0)
                            return;

                        for (i = 0; i < sp->get_sub().num_rects; i++) {
                            AVSubtitleRect *sub_rect = sp->get_sub().rects[i];

                            sub_rect->x = av_clip(sub_rect->x, 0, sp->get_width());
                            sub_rect->y = av_clip(sub_rect->y, 0, sp->get_height());
                            sub_rect->w = av_clip(sub_rect->w, 0, sp->get_width() - sub_rect->x);
                            sub_rect->h = av_clip(sub_rect->h, 0, sp->get_height() - sub_rect->y);

                            is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
                                sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
                                sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
                                0, NULL, NULL, NULL);
                            if (!is->sub_convert_ctx) {
                                av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                                return;
                            }
                            if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)pixels, pitch)) {
                                sws_scale(is->sub_convert_ctx, (const uint8_t * const *)sub_rect->data, sub_rect->linesize,
                                    0, sub_rect->h, pixels, pitch);
                                SDL_UnlockTexture(is->sub_texture);
                            }
                        }
                        sp->set_uploaded(1);
                    }
                }
                else
                    sp = NULL;
            }
        }*/

        //calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->get_width(), vp->get_height(), vp->get_sar());

        if (!vp->get_uploaded()) {
            if (upload_texture( vp->get_frame(), &img_convert_ctx) < 0)
                return;
            vp->set_uploaded(1);
            vp->set_flip_v(vp->get_frame()->linesize[0] < 0);
            new_frame = true;
        } else {
            new_frame = false;
        }
        v_flipped = vp->get_flip_v() != 0;
        //SDL_RenderCopyEx(renderer, is->vid_texture, NULL, &rect, 0, NULL, vp->get_flip_v() ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
/*
        if (sp) {
#if USE_ONEPASS_SUBTITLE_RENDER
            SDL_RenderCopy(renderer, is->sub_texture, NULL, &rect);
#else
            int i;
            double xratio = (double)rect.w / (double)sp->get_width();
            double yratio = (double)rect.h / (double)sp->get_height();
            for (i = 0; i < sp->get_sub().num_rects; i++) {
                / *SDL_Rect *sub_rect = (SDL_Rect*)sp->sub.rects[i];
                SDL_Rect target = { .x = rect.x + sub_rect->x * xratio,
                                   .y = rect.y + sub_rect->y * yratio,
                                   .w = sub_rect->w * xratio,
                                   .h = sub_rect->h * yratio };
                SDL_RenderCopy(renderer, is->sub_texture, sub_rect, &target);* /
            }
#endif
        }*/
    }   
}

int FFMPEGVideoPlayer::upload_texture(AVFrame *frame, struct SwsContext **img_convert_ctx) {

    int size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame->width, frame->height, 1);

    int bufSize = size + 1;

    if ( dataBuffer.Num() != bufSize) {
        dataBuffer.Reset(bufSize);
    }
    int ret = 0;
    int pitch[4] = { 0, 0, 0, 0 };
    ret = av_image_fill_linesizes(pitch, AV_PIX_FMT_RGBA, frame->width);

    uint8_t* data[4] = { 0 };
    av_image_fill_pointers(data, AV_PIX_FMT_RGBA, frame->height, dataBuffer.GetData(), pitch);

    FTexture2DResource* textureResource = static_cast<FTexture2DResource*>(videoTexture->Resource);
    
    if ( !videoTexture.IsValid()  ) {
        allocateTexture(frame->width, frame->height, PF_R8G8B8A8);
    } else {
        if ( textureResource->GetSizeX() != frame->width ||  textureResource->GetSizeY() != frame->height ) {
            resizeTexture(frame->width, frame->height, PF_R8G8B8A8 );
        }
    }


    *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
        frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);


    if (*img_convert_ctx != NULL) {
        sws_scale(*img_convert_ctx, frame->data, frame->linesize, 0, frame->height, data, pitch);
        copyDataToTexture(dataBuffer.GetData(), frame->width, frame->height, pitch[0], 4);
    }
    else {
        UE_LOG(LogVideoPlayer, Error, TEXT( "Cannot initialize the conversion context\n"));
        ret = -1;
        return ret;
    }
   
    return ret;
}

bool FFMPEGVideoPlayer::isHwAccel(const char* name) {
    AVHWAccel* prev = av_hwaccel_next(NULL);

    while (prev) {
        if (strcmp(prev->name, name) == 0) {
            return true;
        }
        prev = av_hwaccel_next(prev);
    }
    return false;
}

AVCodec* FFMPEGVideoPlayer::findDecoder(int codecId, bool hwaccell) {
    AVCodec* prev = av_codec_next(NULL);
    std::vector<AVCodec*> candidates;
    while (prev) {
        if (prev->id == codecId && av_codec_is_decoder(prev)) {
            candidates.push_back(prev);
        }
        prev = av_codec_next(prev);
    }
    if (hwaccell) {
        for (AVCodec* codec : candidates) {
            if (isHwAccel(codec->name)) {
                return codec;
            }
        }
    }

    if (prev == NULL && candidates.size() > 0) {
        if (!hwaccell) {
            for (AVCodec* codec : candidates) {
                if (!isHwAccel(codec->name)) {
                    return codec;
                }
            }
        }
        else {
            return NULL;
        }

        return candidates[0];
    }

    return prev;
}

static void print_option(const AVClass *clazz, const AVOption *o) {
    FString type;
    switch (o->type) {
    case AV_OPT_TYPE_BINARY:   type = "hexadecimal string"; break;
    case AV_OPT_TYPE_STRING:   type = "string";             break;
    case AV_OPT_TYPE_INT:       
    case AV_OPT_TYPE_INT64:    type = "integer";            break;
    case AV_OPT_TYPE_FLOAT:    
    case AV_OPT_TYPE_DOUBLE:   type = "float";              break;
    case AV_OPT_TYPE_RATIONAL: type = "rational number";    break;
    case AV_OPT_TYPE_FLAGS:    type = "flags";              break;
    case AV_OPT_TYPE_BOOL:     type = "bool";              break;
    case AV_OPT_TYPE_SAMPLE_FMT:type = "SampleFmt";              break;
    default:                   type = "value";              break;
    }
    
    FString flags;


    if (o->flags & AV_OPT_FLAG_ENCODING_PARAM) {
        flags = "input";
        if (o->flags & AV_OPT_FLAG_ENCODING_PARAM)
            flags += "/";
    }
    if (o->flags & AV_OPT_FLAG_ENCODING_PARAM)
        flags += "output";

    
    FString help;

    if (o->help)
        help = o->help;

    FString possibleValues;
    if (o->unit) {
        const AVOption *u = av_opt_next(&clazz, NULL);
         
        while (u) {
            bool found = false;
            if (u->type == AV_OPT_TYPE_CONST && u->unit && !strcmp(u->unit, o->unit)) {
                possibleValues += "\n";
                possibleValues += "\t\t* ";
                possibleValues +=  u->name;
                found = true;
                if ( u->help) {
                    possibleValues += " - ";
                    possibleValues += u->help;
                }
            }
            u = av_opt_next(&clazz, u);
        }
        
    }
    if (o->unit) {
        UE_LOG(LogVideoPlayer, Display, TEXT( "\t%s - %s: %s %s"), *type, UTF8_TO_TCHAR(o->name), *help, *possibleValues);
    }
    else {
        UE_LOG(LogVideoPlayer, Warning, TEXT("\t%s - %s: %s"), *type, UTF8_TO_TCHAR(o->name), *help);
    }
    

}

void FFMPEGVideoPlayer::dumpOptions(const AVClass *clazz) {
    const AVOption *o = av_opt_next(&clazz, NULL);
    
    while (o != NULL) {
        if (o->type != AV_OPT_TYPE_CONST) {
            print_option(clazz, o);
        }
        o = av_opt_next(clazz, o);
    }

    
}

void FFMPEGVideoPlayer::setUseHwAccelCodec(bool b) {
    useHwAccelCodec = b;
}

bool FFMPEGVideoPlayer::isHwAccelCodec() {
    if (viddec->get_avctx()) {
        return isHwAccel(viddec->get_avctx()->codec_descriptor->name);
    }
    return false;
}

void FFMPEGVideoPlayer::dumpFFmpegInfo() {
    UE_LOG(LogVideoPlayer, Display, TEXT("FFmpeg license: %s"),  UTF8_TO_TCHAR(avformat_license()));
    UE_LOG(LogVideoPlayer, Display, TEXT("FFmpeg AVCodec version: %d.%d.%d"), LIBAVFORMAT_VERSION_MAJOR, LIBAVFORMAT_VERSION_MINOR, LIBAVFORMAT_VERSION_MICRO);
        

        
    if ( ic ) {
            
        UE_LOG(LogVideoPlayer, Display, TEXT("%s, %s '%s':"), UTF8_TO_TCHAR(ic->iformat->name),TEXT("from"), *filename);

        FString sz_duration = "  Duration: ";
        if (ic->duration != AV_NOPTS_VALUE) {
            int hours, mins, secs, us;
            int64_t duration = ic->duration + (ic->duration <= INT64_MAX - 5000 ? 5000 : 0);
            secs = duration / AV_TIME_BASE;
            us = duration % AV_TIME_BASE;
            mins = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            sz_duration += FString::FromInt(hours) + TEXT(":") + FString::FromInt(mins) + TEXT(":") + FString::FromInt(secs) + TEXT(".") + FString::FromInt(100 * us);
        }
        else {
            sz_duration += "N/A";
        }

            


        UE_LOG(LogVideoPlayer, Display, TEXT("%s"), *sz_duration);
        UE_LOG(LogVideoPlayer, Display, TEXT("\n\nDefault format options"));

        dumpOptions(avformat_get_class());

        if (ic->iformat && ic->iformat->priv_class) {
            UE_LOG(LogVideoPlayer, Display, TEXT("\n\nFormat Options\n"));
            dumpOptions(ic->iformat->priv_class);
        }

        UE_LOG(LogVideoPlayer, Display, TEXT ("\n\nDefault codec options\n"));
        dumpOptions(avcodec_get_class());
        
        if ( ic->video_codec && ic->video_codec->priv_class ) {
            UE_LOG(LogVideoPlayer, Display, TEXT("\n\nVideo Codec\n"));
            dumpOptions( ic->video_codec->priv_class);
        }
        
        if (ic->audio_codec && ic->audio_codec->priv_class) {
            UE_LOG(LogVideoPlayer, Display, TEXT("\n\nAudio Codec\n"));
            dumpOptions(ic->audio_codec->priv_class);
        }

    }
}


void FFMPEGVideoPlayer::setFormatOption(const FString& name, const FString& value) {
    formatOptions[name] = value;
}

void FFMPEGVideoPlayer::addFormatOptions(const TMap<FString, FString>& opts) {
    for ( const auto& entry: opts) {
        formatOptions[entry.Key] = entry.Value;
    }
}
     
void FFMPEGVideoPlayer::setCodecOption(MediaType mediaType, const FString& name, const FString& value) {
    if ( codecOptions.Contains(mediaType) ) {
        codecOptions[mediaType] = TMap<FString, FString>();
    }

    TMap<FString, FString>& map = codecOptions[mediaType];
    map[name] = value;

}
void FFMPEGVideoPlayer::addCodecOptions(MediaType mediaType, const TMap<FString, FString>& opts) {
    if (codecOptions.Contains(mediaType)) {
        codecOptions[mediaType] = TMap<FString, FString>();
    }

    TMap<FString, FString>& map = codecOptions[mediaType];
    for (const auto& entry : opts) {
        map[entry.Key] = entry.Value;
    }
}


void FFMPEGVideoPlayer::setSyncType(int type) {
    av_sync_type = type;
}