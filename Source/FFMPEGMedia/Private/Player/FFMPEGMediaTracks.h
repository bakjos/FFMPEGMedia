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
#include "MediaPlayerOptions.h"

#include "HAL/RunnableThread.h"


class FFFMPEGMediaAudioSamplePool;
class FFFMPEGMediaTextureSamplePool;

struct AVFormatContext;
struct AVCodec;
struct AVBufferRef;
struct AVCodecContext;
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
		int StreamIndex;
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
	void Initialize(AVFormatContext *ic, const FString& Url, const FMediaPlayerOptions* PlayerOptions );

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

    /**
     *
     *
     */
    void TickInput(FTimespan DeltaTime, FTimespan Timecode);



public:

	//~ IMediaSamples interface

	virtual bool FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchCaption(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchMetadata(TRange<FTimespan> TimeRange, TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample) override;
	virtual void FlushSamples() override;
	virtual bool PeekVideoSampleTime(FMediaTimeStamp& TimeStamp) override;

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

/*
public:
    static int cuvid_init(AVCodecContext *avctx);
#if PLATFORM_MAC
    static int videotoolbox_init(AVCodecContext *s);
#endif*/

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
	bool AddStreamToTracks(uint32 StreamIndex, bool IsVideoDevice, const FMediaPlayerTrackOptions& TrackOptions, FString& OutInfo);

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
    AVFormatContext* FormatContext;

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

    FTimespan TargetTime;

    /** Should the video loop to the beginning at completion */
    bool ShouldLoop;


    bool bPrerolled;


    /** FFMPEG methods */
    /** Check if a codec have hardware acceleration options */
    static bool isHwAccel(const AVCodec* codec);

    /** Check if a codec have hardware acceleration options */
    static TArray<const AVCodec*> FindDecoders(int codecId, bool hwaccell);

    /** Find the better accelerated device type for the given codec*/
    static enum AVHWDeviceType FindBetterDeviceType(const AVCodec* codec, int& lastSelection);

    /** Callback for ffmpeg to return the right format when is hardware accelerated*/
    static enum AVPixelFormat GetFormatCallback(AVCodecContext *s, const enum AVPixelFormat *pix_fmts);

    /** Callback for ffmpeg to transfer the gpu data to the cpu when is harware accelerated*/
    static int HWAccelRetrieveDataCallback(AVCodecContext *avctx, AVFrame *input);

    /** Invoked to seek in the stream*/
    void StreamSeek( int64_t pos, int64_t rel, int seek_by_bytes);

    /** Check if the stream buffer has enought callbacks*/
    int StreamHasEnoughPackets(AVStream *st, int stream_id, FFMPEGPacketQueue *queue);

    /** Open the given stream using the stream_index*/
    int  StreamComponentOpen(int stream_index);

    /** Close the given stream using the stream_index*/
    void StreamComponentClose(int stream_index);

    /** Returns the current synchronization type*/
    ESynchronizationType getMasterSyncType();

    /** Transfer the obtained ffmpeg texture to the IMediaTexture */
    int UploadTexture(FFMPEGFrame* vp, AVFrame *frame, struct SwsContext **img_convert_ctx);

    /** Waits for the audio to be in sync when the synchronization is not made through the audio clock */
    int SynchronizeAudio( int nb_samples);

    /** Decode a frame from the packet queue and extract the AVFrame*/
    int GetVideoFrame(AVFrame *frame);


    /** Function to run while is reading the file*/
    int  ReadThread();
    
    /** Decode the audio frames from the packet queue*/
    int AudioThread();

    /** Decode the subtitle frames from the packet queue*/
    int SubtitleThread();

    /** Extract the picture queue */
    int VideoThread();

    /** Thread to convert the video frames*/
    int DisplayThread();

    /** Decode an audio frame and extract the current time and duration for each sample*/
    int AudioDecodeFrame (FTimespan& Time, FTimespan& Duration);

    /** Convert the audio frame to be played by the media player*/
    void RenderAudio();
    
    /** Thread to convert the audio frames */
    int AudioRenderThread();

    /** Refresh the media sample when is need it */
    void VideoRefresh(double *remaining_time);

    /** Starts the display thread*/
    void StartDisplayThread();

    /** Stops the display thread*/
    void StopDisplayThread();

    /** Starts the audio render thread*/
    void StartAudioRenderThread();

    /** Stops the audio render thread*/
    void StopAudioRenderThread();

    void VideoDisplay ();
    void StepToNextFrame();
    void StreamTogglePause();
    double ComputeTargetDelay(double delay);
    void UpdateVideoPts( double pts, int64_t pos, int serial);
    void CheckExternalClockSpeed();
    double GetMasterClock();
    static int  IsRealtime(AVFormatContext *s);

    struct SwsContext *imgConvertCtx;
    
    FRunnableThread* readThread;
    FRunnableThread* audioThread;
    FRunnableThread* videoThread;
    FRunnableThread* subtitleThread;
    FRunnableThread* displayThread;
    
    FRunnableThread* audioRenderThread;

    AVStream *audioStream;
    AVStream *videoStream;
    AVStream *subTitleStream;

    AVCodecContext* video_ctx;

    AVBufferRef* hw_device_ctx;
    AVBufferRef* hw_frames_ctx;

    FFMPEGFrameQueue pictq;
    FFMPEGFrameQueue subpq;
    FFMPEGFrameQueue sampq;

    FFMPEGPacketQueue audioq;
    FFMPEGPacketQueue videoq;
    FFMPEGPacketQueue subtitleq;


    FFMPEGClock audclk;
    FFMPEGClock vidclk;
    FFMPEGClock extclk;

    struct SwrContext *swrContext;

    CondWait continueReadCond;


    TSharedPtr<FFMPEGDecoder> auddec;
    TSharedPtr<FFMPEGDecoder> viddec;
    TSharedPtr<FFMPEGDecoder> subdec;

    bool             aborted;
    bool             displayRunning;
    bool             audioRunning;
    int              eof;
    bool             step;

    //Seek options
    bool             seekReq;
    int64_t          seekPos;
    int64_t          seekRel;
    int              seekFlags;
    bool             queueAttachmentsReq;
    int              readPauseReturn;
    
    int              videoStreamIdx;
    int              audioStreamIdx;
    int              subtitleStreamIdx;

    bool             forceRefresh;

    int              frameDropsLate;
    int              frameDropsEarly;

    double           frameTimer;
    double           maxFrameDuration;

    bool             realtime;

    TArray<uint8>	 dataBuffer;

    ESynchronizationType         sychronizationType;

    FFormat::AudioFormat         srcAudio;          
    FFormat::AudioFormat         targetAudio;          

    uint8_t *audioBuf;
    uint8_t *audioBuf1;
    unsigned int audioBufSize; /* in bytes */
    unsigned int audioBuf1Size; 
    int audioClockSerial;
    double audioClock;
    int64_t audioCallbackTime;
    double audioDiffAvgCoef;
    double audioDiffThreshold;
    int audioDiffAvgCount;
    double audioDiffCum; /* used for AV difference average computation */
    int totalStreams;
    int currentStreams;

     
    std::function<int(AVCodecContext *s, AVFrame *frame)> hwaccel_retrieve_data;

    enum AVPixelFormat hwAccelPixFmt;
    enum AVHWDeviceType hwAccelDeviceType;
    
};


