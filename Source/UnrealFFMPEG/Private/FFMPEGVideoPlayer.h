#pragma once

#include "CoreMinimal.h"

#include "AbstractVideoPlayer.h"
#include "AbstractAudioDevice.h"
#include "FFMPEGClock.h"
#include "FFMPEGFrameQueue.h"



struct AVStream;
struct AVInputFormat;
struct AVFormatContext;
struct SwsContext;
struct AudioParams;
struct AVCodec;
struct AVClass;

class FFMPEGDecoder;

enum MediaType {
    MEDIA_TYPE_UNKNOWN = -1,  
    MEDIA_TYPE_VIDEO,
    MEDIA_TYPE_AUDIO,
    MEDIA_TYPE_DATA,          
    MEDIA_TYPE_SUBTITLE,
    MEDIA_TYPE_ATTACHMENT,    
    MEDIA_TYPE_NB
};

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};


enum BooleanType {
    Undefined = -1,
    False = 0,
    True = 1
};

class FFMPEGVideoPlayer :
    public AbstractVideoPlayer
{
public:
    FFMPEGVideoPlayer();
    ~FFMPEGVideoPlayer();

    
    FString	        getName() { return "FFMPEG"; }
    FString         getThreadName();
    void                setFrameByFrame(bool val) ;
    bool                isFrameByFrame();
    bool	            hasAudio();

    bool				loadMovie(const FString& name);
    void				close();
    void				update();
    
    void				play();
    void				stop();

    bool 				isFrameNew();

    float 				getWidth() const;
    float 				getHeight() const;

    bool				isPaused();
    bool				isLoaded();
    bool				isPlaying();
    bool				isImage();

    //should be implemented
   float 				getPosition();
   float 				getSpeed();
   float 				getDuration();
   bool				    getIsMovieDone();
   float				getVolume();

   void 				setPaused(bool bPause);
   void 				setPosition(float pct);
   void 				setVolume(float volume);
   void 				setLoopState(LoopType state);
   void   				setSpeed(float speed);
   void				    setFrame(int frame);  // frame 0 = first frame...

   int				    getCurrentFrame();
   int				    getTotalNumFrames();
   int				    getLoopState();

   void				    firstFrame();
   void				    nextFrame();
   void				    previousFrame();
   
   //FFMPEG specifics
   void                 setSyncType(int type);
   void				    setUseHwAccelCodec(bool b);
   bool				    isHwAccelCodec();
   void				    dumpFFmpegInfo();
   void                 setFrameDrop(BooleanType fd);
   void                 setInfiniteBuffer(BooleanType i);

   void                 setFormatOption(const FString& name, const FString& value);
   void                 addFormatOptions(const TMap<FString, FString>& opts);

   void                 setCodecOption(MediaType mediaType, const FString& name, const FString& value);
   void                 addCodecOptions(MediaType mediaType, const TMap<FString, FString>& opts);


   bool                isFlipped();
   

private:
    //threads
    static int          read_thread(void* data);
    int                 read_thread_internal();

    static int          audio_thread(void* data);
    int                 audio_thread_internal();

    static int          video_thread(void* data);
    int                 video_thread_internal();

    static int          subtitle_thread(void* data);
    int                 subtitle_thread_internal();

  
    static int          decode_interrupt_cb(void *ctx);
    
    //stream functions
    int stream_component_open(int stream_index);
    void stream_component_close( int stream_index);
    static int is_realtime(AVFormatContext *s);
    int stream_has_enough_packets(AVStream *st, int stream_id, FFMPEGPacketQueue *queue);
    void stream_seek( int64_t pos, int64_t rel, int seek_by_bytes);
    void step_to_next_frame();
    void stream_toggle_pause();
    
    //Audio Functions
    int  audio_open(int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params);
    void audio_callback(unsigned char *stream, int len);
    int audio_decode_frame();
    int synchronize_audio( int nb_samples);

    //Clock functions
    int get_master_sync_type();
    double get_master_clock();
    void check_external_clock_speed();

    //video functions
    int get_video_frame(AVFrame *frame);
    void video_refresh(double *remaining_time);
    double compute_target_delay(double delay);
    void update_video_pts(double pts, int64_t pos, int serial);
    void video_display();
    int upload_texture(AVFrame *frame, struct SwsContext **img_convert_ctx);

    //Utility functions
    static bool isHwAccel(const char* name);
    static AVCodec* findDecoder(int codecId, bool hwaccell);
   
    static void dumpOptions(const AVClass *clazz);
    



    static bool         initialized;
   
    std::thread *read_tid;
    AVInputFormat *iformat;
    int abort_request;
    int force_refresh;
    int paused;
    int last_paused;
    int queue_attachments_req;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int read_pause_return;
    AVFormatContext *ic;
    int realtime;

    FFMPEGClock audclk;
    FFMPEGClock vidclk;
    FFMPEGClock extclk;

    FFMPEGFrameQueue pictq;
    FFMPEGFrameQueue subpq;
    FFMPEGFrameQueue sampq;

    std::shared_ptr<FFMPEGDecoder> auddec;
    std::shared_ptr<FFMPEGDecoder> viddec;
    std::shared_ptr<FFMPEGDecoder> subdec;

    int audio_stream;

    int av_sync_type;

    double audio_clock;
    int audio_clock_serial;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    FFMPEGPacketQueue audioq;
    int audio_hw_buf_size;
    uint8_t *audio_buf;
    uint8_t *audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    int audio_volume;
    int muted;
    std::shared_ptr<AudioParams> audio_src;
    std::shared_ptr<AudioParams> audio_tgt;

    struct SwrContext *swr_ctx;
    int frame_drops_early;
    int frame_drops_late;

   
   
    int subtitle_stream;
    AVStream *subtitle_st;
    FFMPEGPacketQueue subtitleq;

    double frame_timer;
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int video_stream;
    AVStream *video_st;
    FFMPEGPacketQueue videoq;
    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    struct SwsContext *img_convert_ctx;
    struct SwsContext *sub_convert_ctx;
    int eof;

    FString filename;
    int width, height;
    int step;



    int last_video_stream, last_audio_stream, last_subtitle_stream;

    CondWait continue_read_thread;
    
    

    int infinite_buffer;
    int framedrop;
    
    int64_t audio_callback_time;

    bool   new_frame;
    bool   v_flipped;
    int nFrames;
    double dFps;
    double currentPos;
    double remaining_time;
    bool   useHwAccelCodec;
    int    lastFrame;

    TArray<uint8>	dataBuffer;
    FRWLock         mutex;

    TMap<FString, FString> formatOptions;
    TMap<int,TMap<FString, FString>> codecOptions;
};

