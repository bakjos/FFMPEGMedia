// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FFMPEGMediaSettings.h"
#include "FFMPEGMediaPrivate.h"
#include "FFMPEGFrameQueue.h"
#include "FFMPEGClock.h"


#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"
#include "IMediaSamples.h"
#include "IMediaTracks.h"
#include "IMediaControls.h"
#include "Math/IntPoint.h"
#include "MediaSampleQueue.h"
#include "Templates/SharedPointer.h"

#include "RunnableThread.h"


class FFFMPEGMediaAudioSamplePool;
class FFFMPEGMediaTextureSamplePool;

struct AVFormatContext;
struct AVCodec;

class FFMPEGDecoder;




/**
 * Track collection for Windows Media Foundation based media players.
 */
class FFFMPEGMediaTracks
	: public IMediaSamples
	, public IMediaTracks
    , public IMediaControls
{
	/** Track format. */
	struct FFormat
	{
        enum AVMediaType MediaType;
        enum AVCodecID CodecID;
		FString TypeName;

		struct AudioFormat
		{
			uint32 FrameSize;
			uint32 NumChannels;
			uint32 SampleRate;
            uint64_t ChannelLayout;
            enum AVSampleFormat Format;
            uint32 BytesPerSec;
            uint32 HardwareSize;
		}
		Audio;

		struct VideoFormat
		{
			int64_t BitRate;
			float FrameRate;
			FIntPoint OutputDim;
			enum AVPixelFormat Format;
            int  LineSize[4];
		}
		Video;
	};


	/** Track information. */
	struct FTrack
	{
		FText DisplayName;
		FFormat Format;
		FString Language;
		FString Name;
		bool Protected;
		DWORD StreamIndex;
	};

public:

	/** Default constructor. */
	FFFMPEGMediaTracks();

	/** Virtual destructor. */
	virtual ~FFFMPEGMediaTracks();

public:

	/**
	 * Append track statistics information to the given string.
	 *
	 * @param OutStats The string to append the statistics to.
	 */
	void AppendStats(FString &OutStats) const;

	/**
	 * Clear the streams flags.
	 *
	 * @see GetFlags
	 */
	void ClearFlags();

    /**
	 * Gets all deferred player events.
	 *
	 * @param OutEvents Will contain the events.
	 * @see GetCapabilities
	 */
	void GetEvents(TArray<EMediaEvent>& OutEvents);



	/**
	 * Get the current flags.
	 *
	 * @param OutMediaSourceChanged Will indicate whether the media source changed.
	 * @param OutSelectionChanged Will indicate whether the track selection changed.
	 * @see ClearFlags
	 */
	void GetFlags(bool& OutMediaSourceChanged, bool& OutSelectionChanged) const;

	/**
	 * Get the information string for the currently loaded media source.
	 *
	 * @return Info string.
	 * @see GetDuration, GetSamples
	 */
	const FString& GetInfo() const
	{
		return Info;
	}

	/**
	 * Initialize the track collection.
	 *
	 * @param AVFormatContext input format
	 * @param Url The media source URL.
	 * @see IsInitialized, Shutdown
	 */
	void Initialize(AVFormatContext *ic, const FString& Url);

	/**
	 * Reinitialize the track collection
	 *
	 * @see IsInitialized, Shutdown
	 */
	void ReInitialize();

	/**
	 * Whether this object has been initialized.
	 *
	 * @return true if initialized, false otherwise.
	 * @see Initialize, Shutdown
	 */
	bool IsInitialized() const
	{
		//return (MediaSource != NULL);
        return false;
	}

	/**
	 * Shut down the track collection.
	 *
	 * @see Initialize, IsInitialized
	 */
	void Shutdown();

public:

	//~ IMediaSamples interface

	virtual bool FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchCaption(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchMetadata(TRange<FTimespan> TimeRange, TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample) override;
	virtual void FlushSamples() override;

public:

	//~ IMediaTracks interface

	virtual bool GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const override;
	virtual int32 GetNumTracks(EMediaTrackType TrackType) const override;
	virtual int32 GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual int32 GetSelectedTrack(EMediaTrackType TrackType) const override;
	virtual FText GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual int32 GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual FString GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual FString GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual bool GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const override;
	virtual bool SelectTrack(EMediaTrackType TrackType, int32 TrackIndex) override;
	virtual bool SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex) override;
	virtual bool SetVideoTrackFrameRate(int32 TrackIndex, int32 FormatIndex, float FrameRate) override;

public:
    //~ IMediaControls interface

    virtual bool CanControl(EMediaControl Control) const override;
    virtual FTimespan GetDuration() const override;
    virtual float GetRate() const override;
    virtual EMediaState GetState() const override;
    virtual EMediaStatus GetStatus() const override;
    virtual TRangeSet<float> GetSupportedRates(EMediaRateThinning Thinning) const override;
    virtual FTimespan GetTime() const override;
    virtual bool IsLooping() const override;
    virtual bool Seek(const FTimespan& Time) override;
    virtual bool SetLooping(bool Looping) override;
    virtual bool SetRate(float Rate) override;

protected:

	/**
	 * Add the specified stream to the track collection.
	 *
	 * @param StreamIndex The index of the stream to add.
	 * @param OutInfo Will contain appended debug information.
	 * @param IsVideoDevice Whether the stream belongs to a video capture device.
	 * @return true on success, false otherwise.
	 * @see AddTrackToTopology
	 */
	bool AddStreamToTracks(uint32 StreamIndex, bool IsVideoDevice, FString& OutInfo);

	/**
	 * Add the given track to the specified playback topology.
	 *
	 * @param Track The track to add.
	 * @param Topology The playback topology.
	 * @return true on success, false otherwise.
	 * @see AddStreamToTracks
	 */
	//bool AddTrackToTopology(const FTrack& Track, IMFTopology& Topology) const;

private:

	/**
	 * Get the specified audio format.
	 *
	 * @param TrackIndex Index of the audio track that contains the format.
	 * @param FormatIndex Index of the format to return.
	 * @return Pointer to format, or nullptr if not found.
	 * @see GetVideoFormat
	 */
	const FFormat* GetAudioFormat(int32 TrackIndex, int32 FormatIndex) const;

	/**
	 * Get the specified track information.
	 *
	 * @param TrackType The type of track.
	 * @param TrackIndex Index of the track to return.
	 * @return Pointer to track, or nullptr if not found.
	 */
	const FTrack* GetTrack(EMediaTrackType TrackType, int32 TrackIndex) const;

	/**
	 * Get the specified video format.
	 *
	 * @param TrackIndex Index of the video track that contains the format.
	 * @param FormatIndex Index of the format to return.
	 * @return Pointer to format, or nullptr if not found.
	 * @see GetAudioFormat
	 */
	const FFormat* GetVideoFormat(int32 TrackIndex, int32 FormatIndex) const;

private:

	/** Callback for handling media sampler pauses. */
	//void HandleMediaSamplerClock(EWmfMediaSamplerClockEvent Event, EMediaTrackType TrackType);

	/** Callback for handling new samples from the streams' media sample buffers. */
	//void HandleMediaSamplerAudioSample(const uint8* Buffer, uint32 Size, FTimespan Duration, FTimespan Time);

	/** Callback for handling new caption samples. */
	//void HandleMediaSamplerCaptionSample(const uint8* Buffer, uint32 Size, FTimespan Duration, FTimespan Time);

	/** Callback for handling new metadata samples. */
	//void HandleMediaSamplerMetadataSample(const uint8* Buffer, uint32 Size, FTimespan Duration, FTimespan Time);

	/** Callback for handling new video samples. */
	//void HandleMediaSamplerVideoSample(const uint8* Buffer, uint32 Size, FTimespan Duration, FTimespan Time);

private:

	/** Audio sample object pool. */
	FFFMPEGMediaAudioSamplePool* AudioSamplePool;

	/** Audio sample queue. */
	TMediaSampleQueue<IMediaAudioSample> AudioSampleQueue;

	/** The available audio tracks. */
	TArray<FTrack> AudioTracks;

	/** Overlay sample queue. */
	TMediaSampleQueue<IMediaOverlaySample> CaptionSampleQueue;

	/** The available caption tracks. */
	TArray<FTrack> CaptionTracks;

	/** Synchronizes write access to track arrays, selections & sinks. */
	mutable FCriticalSection CriticalSection;

	/** Media information string. */
	FString Info;

	/** The initial media url. */
	FString SourceUrl;

	/** The currently opened media. */
    AVFormatContext* ic;

	/** Whether the media source has changed. */
	bool MediaSourceChanged;

	/** Metadata sample queue. */
	TMediaSampleQueue<IMediaBinarySample> MetadataSampleQueue;

	/** The available metadata tracks. */
	TArray<FTrack> MetadataTracks;

	/** The presentation descriptor of the currently opened media. */
	//TComPtr<IMFPresentationDescriptor> PresentationDescriptor;

	/** Index of the selected audio track. */
	int32 SelectedAudioTrack;

	/** Index of the selected caption track. */
	int32 SelectedCaptionTrack;

	/** Index of the selected binary track. */
	int32 SelectedMetadataTrack;

	/** Index of the selected video track. */
	int32 SelectedVideoTrack;

	/** Whether the track selection changed. */
	bool SelectionChanged;

	/** Video sample object pool. */
	FFFMPEGMediaTextureSamplePool* VideoSamplePool;

	/** Video sample queue. */
	TMediaSampleQueue<IMediaTextureSample> VideoSampleQueue;

	/** The available video tracks. */
	TArray<FTrack> VideoTracks;

    /** The current playback rate. */
    float CurrentRate;

    /** Media events to be forwarded to main thread. */
    TQueue<EMediaEvent> DeferredEvents;

    /** Media playback state. */
    EMediaState CurrentState;

    EMediaState LastState;

    /** The current time of the playback. */
    FTimespan CurrentTime;

    /** The duration of the media. */
    FTimespan Duration;

    /** Should the video loop to the beginning at completion */
    bool ShouldLoop;


    /** FFMPEG methods */
    static bool FFFMPEGMediaTracks::isHwAccel(const char* name);
    static AVCodec* FindDecoder(int codecId, bool hwaccell);

    void StreamSeek( int64_t pos, int64_t rel, int seek_by_bytes);
    int StreamHasEnoughPackets(AVStream *st, int stream_id, FFMPEGPacketQueue *queue);

    int  StreamComponentOpen(int stream_index);
    void StreamComponentClose(int stream_index);

    ESynchronizationType get_master_sync_type();
    int upload_texture(FFMPEGFrame* vp, AVFrame *frame, struct SwsContext **img_convert_ctx);
    int synchronize_audio( int nb_samples);


    //video functions
    int GetVideoFrame(AVFrame *frame);

   
    int  ReadThread();
    
    int AudioThread();
    int SubtitleThread();
    int VideoThread();
    int DisplayThread();
    int audio_decode_frame (FTimespan& Time, FTimespan& Duration);
    void RenderAudio();
    int AudioRenderThread();
    void VideoRefresh(double *remaining_time);

    void startDisplayThread();
    void stopDisplayThread();

    void startAudioRenderThread();
    void stopAudioRenderThread();

    void video_display ();
    void step_to_next_frame();
    void stream_toggle_pause();
    double compute_target_delay(double delay);
    void update_video_pts( double pts, int64_t pos, int serial);
    void check_external_clock_speed();
    double get_master_clock();


    struct SwsContext *img_convert_ctx;
    
    FRunnableThread* readThread;
    FRunnableThread* audioThread;
    FRunnableThread* videoThread;
    FRunnableThread* subtitleThread;
    FRunnableThread* displayThread;
    
    FRunnableThread* audioRenderThread;

    AVStream *audio_st;
    AVStream *video_st;
    AVStream *subtitle_st;

    FFMPEGFrameQueue pictq;
    FFMPEGFrameQueue subpq;
    FFMPEGFrameQueue sampq;

    FFMPEGPacketQueue audioq;
    FFMPEGPacketQueue videoq;
    FFMPEGPacketQueue subtitleq;


    FFMPEGClock audclk;
    FFMPEGClock vidclk;
    FFMPEGClock extclk;

    struct SwrContext *swr_ctx;

    CondWait continue_read_thread;


    TSharedPtr<FFMPEGDecoder> auddec;
    TSharedPtr<FFMPEGDecoder> viddec;
    TSharedPtr<FFMPEGDecoder> subdec;

    bool             aborted;
    bool             displayRunning;
    bool             audioRunning;
    int              eof;
    bool             step;

    //Seek options
    bool             seek_req;
    int64_t          seek_pos;
    int64_t          seek_rel;
    int              seek_flags;
    bool             queue_attachments_req;
    int              read_pause_return;
    
    int              video_stream;
    int              audio_stream;
    int              subtitle_stream;

    bool             force_refresh;

    int              frame_drops_late;
    int              frame_drops_early;

    double           frame_timer;
    double           max_frame_duration;

    bool             realtime;

    TArray<uint8>	dataBuffer;

    ESynchronizationType    av_sync_type;

    FFormat::AudioFormat         srcAudio;          
    FFormat::AudioFormat         targetAudio;          

    uint8_t *audio_buf;
    uint8_t *audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size; 
    int audio_clock_serial;
    double audio_clock;
    int64_t audio_callback_time;
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    double audio_diff_cum; /* used for AV difference average computation */
};


