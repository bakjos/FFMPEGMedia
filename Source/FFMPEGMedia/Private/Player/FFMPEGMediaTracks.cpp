// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "FFMPEGMediaTracks.h"
#include "FFMPEGMediaPrivate.h"
#include "LambdaFunctionRunnable.h"
#include "FFMPEGMediaSettings.h"

#include "FFMPEGDecoder.h"
#include "FFMPEGFrame.h"

#include "IMediaOptions.h"
#include "MediaHelpers.h"
#include "Misc/ScopeLock.h"
#include "UObject/Class.h"
#include "IMediaBinarySample.h"
#include "IMediaEventSink.h"
#include "MediaPlayerOptions.h"


#if WITH_ENGINE
	#include "Engine/Engine.h"
#endif

#include "FFMPEGMediaBinarySample.h"
#include "FFMPEGMediaOverlaySample.h"
#include "FFMPEGMediaAudioSample.h"
#include "FFMPEGMediaTextureSample.h"

extern  "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/hwcontext.h"
#include "libavutil/time.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"

}

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)

#define MIN_FRAMES 30

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1

#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* Minimum  audio buffer size, in samples. */
#define AUDIO_MIN_BUFFER_SIZE 512

/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define AUDIO_MAX_CALLBACKS_PER_SEC 30

/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10


#define LOCTEXT_NAMESPACE "FFMPEGMediaTracks"





/* FFFMPEGMediaTracks structors
 *****************************************************************************/

FFFMPEGMediaTracks::FFFMPEGMediaTracks()
	: AudioSamplePool(new FFFMPEGMediaAudioSamplePool)
	, FormatContext(NULL)
	, MediaSourceChanged(false)
	, SelectedAudioTrack(INDEX_NONE)
	, SelectedCaptionTrack(INDEX_NONE)
	, SelectedMetadataTrack(INDEX_NONE)
	, SelectedVideoTrack(INDEX_NONE)
	, SelectionChanged(false)
	, VideoSamplePool(new FFFMPEGMediaTextureSamplePool)
	, CurrentRate(0.0f)
  , CurrentState (EMediaState::Closed)
  , LastState (EMediaState::Closed)
	, CurrentTime(FTimespan::Zero())
  , Duration(FTimespan::Zero())
  , ShouldLoop(false)
  , bPrerolled(false)
	, imgConvertCtx(NULL)
	, readThread(nullptr)
	, audioThread(nullptr)
	, videoThread(nullptr)
	, subtitleThread(nullptr)
	, displayThread(nullptr)
	, audioStream(NULL)
	, videoStream(NULL)
	, subTitleStream(NULL)
	, video_ctx(NULL)
	, hw_device_ctx(NULL)
	, hw_frames_ctx(NULL)
	, swrContext(NULL)
	, aborted(false)
  , displayRunning(false)
  , step(false)  
  , seekReq(false)
  , seekPos(0)
  , seekRel(0)
  , seekFlags(0)
  , queueAttachmentsReq(false)
  , videoStreamIdx(-1)
  , audioStreamIdx(-1)
  , subtitleStreamIdx(-1)
  , forceRefresh(false)
  , frameDropsLate(0)
  , frameDropsEarly(0)
  , frameTimer(0.0)
  , maxFrameDuration(0.0)
  , realtime(false)
  , audioBuf(NULL)
  , audioBuf1(NULL)
  , audioBufSize(0)
  , audioBuf1Size(0)
  , audioClockSerial(0)
  , audioClock(0)
  , audioCallbackTime(0)
  , audioDiffAvgCoef(0)
  , audioDiffThreshold(0)
  , audioDiffAvgCount(0)
  , audioDiffCum(0)
  , totalStreams(0)
	, currentStreams(0)
	, hwAccelPixFmt(AV_PIX_FMT_NONE)
	, hwAccelDeviceType(AV_HWDEVICE_TYPE_NONE){
    
}


FFFMPEGMediaTracks::~FFFMPEGMediaTracks()
{
	Shutdown();

	delete AudioSamplePool;
	AudioSamplePool = nullptr;

	delete VideoSamplePool;
	VideoSamplePool = nullptr;
}


/* FFFMPEGMediaTracks interface
 *****************************************************************************/

void FFFMPEGMediaTracks::AppendStats(FString &OutStats) const
{
	FScopeLock Lock(&CriticalSection);

	// audio tracks
	OutStats += TEXT("Audio Tracks\n");
	
	if (AudioTracks.Num() == 0)
	{
		OutStats += TEXT("\tnone\n");
	}
	else
	{
		for (const FTrack& Track : AudioTracks)
		{
			OutStats += FString::Printf(TEXT("\t%s\n"), *Track.DisplayName.ToString());
			OutStats += TEXT("\t\tNot implemented yet");
		}
	}

	// video tracks
	OutStats += TEXT("Video Tracks\n");

	if (VideoTracks.Num() == 0)
	{
		OutStats += TEXT("\tnone\n");
	}
	else
	{
		for (const FTrack& Track : VideoTracks)
		{
			OutStats += FString::Printf(TEXT("\t%s\n"), *Track.DisplayName.ToString());
			OutStats += TEXT("\t\tNot implemented yet");
		}
	}
}


void FFFMPEGMediaTracks::ClearFlags()
{
	FScopeLock Lock(&CriticalSection);

	MediaSourceChanged = false;
	SelectionChanged = false;
}

void FFFMPEGMediaTracks::GetEvents(TArray<EMediaEvent>& OutEvents) {
    EMediaEvent Event;

    while (DeferredEvents.Dequeue(Event))
    {
        OutEvents.Add(Event);
    }
}



void FFFMPEGMediaTracks::GetFlags(bool& OutMediaSourceChanged, bool& OutSelectionChanged) const
{
	FScopeLock Lock(&CriticalSection);

	OutMediaSourceChanged = MediaSourceChanged;
	OutSelectionChanged = SelectionChanged;
}


void FFFMPEGMediaTracks::Initialize(AVFormatContext* ic, const FString& Url, const FMediaPlayerOptions* PlayerOptions )
{
	Shutdown();
    
    FMediaPlayerTrackOptions TrackOptions;
    if (PlayerOptions != nullptr)
    {
        TrackOptions = PlayerOptions->Tracks;
    }
    
   
	UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks: %p: Initializing (media source %p)"), this, ic);

	FScopeLock Lock(&CriticalSection);

	MediaSourceChanged = true;
	SelectionChanged = true;

	if (!ic)
	{
        CurrentState = EMediaState::Error;
		return;
	}


    if (pictq.Init(&videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0) {
        Shutdown();
        CurrentState = EMediaState::Error;
        return ;
    }

    realtime = IsRealtime(ic) != 0;

    if (subpq.Init(&subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0) {
        Shutdown();
        CurrentState = EMediaState::Error;
        return;
    }

    if (sampq.Init(&audioq, SAMPLE_QUEUE_SIZE, 1) < 0) {
        Shutdown();
        CurrentState = EMediaState::Error;
        return;
    }

    vidclk.Init(&videoq);
    audclk.Init(&audioq);
    extclk.Init(&extclk);
    
    this->FormatContext = ic;
    audioClockSerial = -1;

    CurrentState = EMediaState::Preparing;

    //

    bool AllStreamsAdded = true;

    for (int i = 0; i < (int)ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        bool streamAdded = AddStreamToTracks(i, false, TrackOptions, Info);
        AllStreamsAdded &= streamAdded;
        if ( streamAdded) {
            totalStreams++;
        }
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
    }

    if (!AllStreamsAdded)
    {
        UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks %p: Not all available streams were added to the track collection"), this);
    }

    int64_t duration = ic->duration + (ic->duration <= INT64_MAX - 5000 ? 5000 : 0);

    Duration = duration * 10;

    maxFrameDuration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    SetRate(0.0f);

    DeferredEvents.Enqueue(EMediaEvent::MediaOpened);
    //Start the read thread

    aborted = false;

    const auto Settings = GetDefault<UFFMPEGMediaSettings>();
    sychronizationType = Settings->SyncType;

    readThread = LambdaFunctionRunnable::RunThreaded(TEXT("ReadThread"), [this] {
        ReadThread();
    }); 
    

}

void FFFMPEGMediaTracks::ReInitialize()
{
	/*if (MediaSource != NULL)
	{
		TComPtr<IMFMediaSource> lMediaSource = WmfMedia::ResolveMediaSource(nullptr, SourceUrl, false);
		int32 lTrack = GetSelectedTrack(EMediaTrackType::Video);
		int32 lFormat = GetTrackFormat(EMediaTrackType::Video, lTrack);
		Initialize(lMediaSource, SourceUrl);
		SetTrackFormat(EMediaTrackType::Video, lTrack, lFormat);
		SelectTrack(EMediaTrackType::Video, lTrack);
	}*/
}

void FFFMPEGMediaTracks::Shutdown()
{
	UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks: %p: Shutting down (context %p)"), this, FormatContext);

    aborted = true;
    displayRunning = false;

    maxFrameDuration = 0.0;

    /* close each stream */
    if (SelectedAudioTrack >= 0)
        StreamComponentClose(audioStreamIdx);
    if (SelectedCaptionTrack >= 0)
        StreamComponentClose(subtitleStreamIdx);
    if (SelectedVideoTrack >= 0)
        StreamComponentClose(videoStreamIdx);

    videoStreamIdx = -1;
    audioStreamIdx = -1;
    subtitleStreamIdx = -1;
    
    if ( displayThread != nullptr) {
        displayThread->WaitForCompletion();
        displayThread = nullptr;
    }
    
    if ( readThread != nullptr) {
        readThread->WaitForCompletion();
        readThread = nullptr;
    }
    if ( audioThread != nullptr) {
        audioThread->WaitForCompletion();
        audioThread = nullptr;
    }
    if ( videoThread != nullptr) {
        videoThread->Kill(true);
        videoThread = nullptr;
    }
    if ( subtitleThread != nullptr) {
        subtitleThread->WaitForCompletion();
        subtitleThread = nullptr;
    }
    
    
    
    
    if ( imgConvertCtx ) {
        sws_freeContext(imgConvertCtx);
        imgConvertCtx = NULL;
    }
    
    hwaccel_retrieve_data = nullptr;
    auddec = MakeShareable(new FFMPEGDecoder());
    viddec = MakeShareable(new FFMPEGDecoder());
    subdec= MakeShareable(new FFMPEGDecoder());
   

     /*hw_device_ctx(NULL)
    , hw_frames_ctx(NULL)
    , hwaccel_ctx(NULL)
    , hwAccelPixFmt(AV_PIX_FMT_NONE)*/
    
	FScopeLock Lock(&CriticalSection);

    pictq.Destroy();
    subpq.Destroy();
    sampq.Destroy();

    audioq.Flush();
    videoq.Flush();
    subtitleq.Flush();
    
	AudioSamplePool->Reset();
	VideoSamplePool->Reset();

	SelectedAudioTrack = INDEX_NONE;
	SelectedCaptionTrack = INDEX_NONE;
	SelectedMetadataTrack = INDEX_NONE;
	SelectedVideoTrack = INDEX_NONE;

    frameDropsLate = 0;
    frameDropsEarly = 0;

	AudioTracks.Empty();
	MetadataTracks.Empty();
	CaptionTracks.Empty();
	VideoTracks.Empty();

	Info.Empty();

	MediaSourceChanged = false;
	SelectionChanged = false;
    bPrerolled = false;
    
    CurrentState =  EMediaState::Closed;

    currentStreams = 0;
    totalStreams = 0;
    frameTimer = 0.0;
    maxFrameDuration = 0.0;
    dataBuffer.Reset();
}

void FFFMPEGMediaTracks::TickInput(FTimespan DeltaTime, FTimespan Timecode) {
    TargetTime = Timecode;

    double time = Timecode.GetTotalSeconds();
    UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks: %p: TimeCode %.3f"), this, (float)time);
}


/* IMediaSamples interface
 *****************************************************************************/

bool FFFMPEGMediaTracks::FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample)
{
	TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe> Sample;

    FTimespan timeSpan = TimeRange.Size<FTimespan>();

    double time = timeSpan.GetTotalSeconds();
    UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks: %p: TimeCode %.3f"), this, (float)time);


	if (!AudioSampleQueue.Peek(Sample))
	{
		return false;
	}

	const FTimespan SampleTime = Sample->GetTime().Time;

	if (!TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
	{
        AudioSampleQueue.RequestFlush();
		return false;
	}

	if (!AudioSampleQueue.Dequeue(Sample))
	{
		return false;
	}


    

	OutSample = Sample;

	return true;
}


bool FFFMPEGMediaTracks::FetchCaption(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample)
{
	TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe> Sample;

	if (!CaptionSampleQueue.Peek(Sample))
	{
		return false;
	}

    const FTimespan SampleTime = Sample->GetTime().Time;

	if (!TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
	{
        CaptionSampleQueue.RequestFlush();
		return false;
	}

	if (!CaptionSampleQueue.Dequeue(Sample))
	{
		return false;
	}

	OutSample = Sample;

	return true;
}


bool FFFMPEGMediaTracks::FetchMetadata(TRange<FTimespan> TimeRange, TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>& OutSample)
{
	TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe> Sample;

	if (!MetadataSampleQueue.Peek(Sample))
	{
		return false;
	}

    const FTimespan SampleTime = Sample->GetTime().Time;

	if (!TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
	{
		return false;
	}

	if (!MetadataSampleQueue.Dequeue(Sample))
	{
		return false;
	}

	OutSample = Sample;

	return true;
}


bool FFFMPEGMediaTracks::FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample)
{
	TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;

	if (!VideoSampleQueue.Peek(Sample))
	{
		return false;
	}

	const FTimespan SampleTime = Sample->GetTime().Time;

	if (!TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
	{
        VideoSampleQueue.RequestFlush();
     	return false;
	}

	if (!VideoSampleQueue.Dequeue(Sample))
	{
		return false;
	}

	OutSample = Sample;

	return true;
}


void FFFMPEGMediaTracks::FlushSamples()
{
	AudioSampleQueue.RequestFlush();
	CaptionSampleQueue.RequestFlush();
	MetadataSampleQueue.RequestFlush();
	VideoSampleQueue.RequestFlush();
}

bool  FFFMPEGMediaTracks::PeekVideoSampleTime(FMediaTimeStamp& TimeStamp) {

    TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
    if (!VideoSampleQueue.Peek(Sample))
    {
        return false;
    }
    TimeStamp = FMediaTimeStamp(Sample->GetTime());
    return true;

}


/* IMediaTracks interface
 *****************************************************************************/

bool FFFMPEGMediaTracks::GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const
{
	FScopeLock Lock(&CriticalSection);

	const FFormat* Format = GetAudioFormat(TrackIndex, FormatIndex);
	
	if (Format == nullptr)
	{
		return false; // format not found
	}

	OutFormat.BitsPerSample = Format->Audio.FrameSize*8;
	OutFormat.NumChannels = Format->Audio.NumChannels;
	OutFormat.SampleRate = Format->Audio.SampleRate;
	OutFormat.TypeName = Format->TypeName;

	return true;
}


int32 FFFMPEGMediaTracks::GetNumTracks(EMediaTrackType TrackType) const
{
	FScopeLock Lock(&CriticalSection);

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		return AudioTracks.Num();

	case EMediaTrackType::Metadata:
		return MetadataTracks.Num();

	case EMediaTrackType::Caption:
		return CaptionTracks.Num();

	case EMediaTrackType::Video:
		return VideoTracks.Num();

	default:
		break; // unsupported track type
	}

	return 0;
}


int32 FFFMPEGMediaTracks::GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const
{
	FScopeLock Lock(&CriticalSection);

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			//return AudioTracks[TrackIndex].Formats.Num();
            return 1;
		}

	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return 1;
		}

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return 1;
		}

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			//return VideoTracks[TrackIndex].Formats.Num();
            return 1;
		}

	default:
		break; // unsupported track type
	}

	return 0;
}


int32 FFFMPEGMediaTracks::GetSelectedTrack(EMediaTrackType TrackType) const
{
	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		return SelectedAudioTrack;

	case EMediaTrackType::Caption:
		return SelectedCaptionTrack;

	case EMediaTrackType::Metadata:
		return SelectedMetadataTrack;

	case EMediaTrackType::Video:
		return SelectedVideoTrack;

	default:
		break; // unsupported track type
	}

	return INDEX_NONE;
}


FText FFFMPEGMediaTracks::GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	FScopeLock Lock(&CriticalSection);

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return AudioTracks[TrackIndex].DisplayName;
		}
		break;
	
	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return MetadataTracks[TrackIndex].DisplayName;
		}
		break;

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return CaptionTracks[TrackIndex].DisplayName;
		}
		break;

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return VideoTracks[TrackIndex].DisplayName;
		}
		break;

	default:
		break; // unsupported track type
	}

	return FText::GetEmpty();
}


int32 FFFMPEGMediaTracks::GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const
{
	FScopeLock Lock(&CriticalSection);

	const FTrack* Track = GetTrack(TrackType, TrackIndex);
	return (Track != nullptr) ? 0/*Track->SelectedFormat*/ : INDEX_NONE;
}



FString FFFMPEGMediaTracks::GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const
{
	FScopeLock Lock(&CriticalSection);

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return AudioTracks[TrackIndex].Language;
		}
		break;

	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return MetadataTracks[TrackIndex].Language;
		}
		break;

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return CaptionTracks[TrackIndex].Language;
		}
		break;

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return VideoTracks[TrackIndex].Language;
		}
		break;

	default:
		break; // unsupported track type
	}

	return FString();
}


FString FFFMPEGMediaTracks::GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	FScopeLock Lock(&CriticalSection);

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return AudioTracks[TrackIndex].Name;
		}
		break;

	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return MetadataTracks[TrackIndex].Name;
		}
		break;

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return CaptionTracks[TrackIndex].Name;
		}
		break;

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return VideoTracks[TrackIndex].Name;
		}
		break;

	default:
		break; // unsupported track type
	}

	return FString();
}


bool FFFMPEGMediaTracks::GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const
{
	FScopeLock Lock(&CriticalSection);

	const FFormat* Format = GetVideoFormat(TrackIndex, FormatIndex);
	
	if (Format == nullptr)
	{
		return false; // format not found
	}

	OutFormat.Dim = Format->Video.OutputDim;
	OutFormat.FrameRate = Format->Video.FrameRate;
	OutFormat.FrameRates = TRange<float>(Format->Video.FrameRate);
	OutFormat.TypeName = Format->TypeName;

	return true;
}



bool FFFMPEGMediaTracks::SelectTrack(EMediaTrackType TrackType, int32 TrackIndex)
{
	if (!FormatContext)
	{
		return false; // not initialized
	}

	UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks %p: Selecting %s track %i"), this, *MediaUtils::TrackTypeToString(TrackType), TrackIndex);

	FScopeLock Lock(&CriticalSection);

	int32* SelectedTrack = nullptr;
	TArray<FTrack>* Tracks = nullptr;

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		SelectedTrack = &SelectedAudioTrack;
		Tracks = &AudioTracks;
		break;

	case EMediaTrackType::Caption:
		SelectedTrack = &SelectedCaptionTrack;
		Tracks = &CaptionTracks;
		break;

	case EMediaTrackType::Metadata:
		SelectedTrack = &SelectedMetadataTrack;
		Tracks = &MetadataTracks;
		break;

	case EMediaTrackType::Video:
		SelectedTrack = &SelectedVideoTrack;
		Tracks = &VideoTracks;
		break;

	default:
		return false; // unsupported track type
	}

	check(SelectedTrack != nullptr);
	check(Tracks != nullptr);

	if (TrackIndex == *SelectedTrack)
	{
		return true; // already selected
	}

	if ((TrackIndex != INDEX_NONE) && !Tracks->IsValidIndex(TrackIndex))
	{
		return false; // invalid track
	}

	// deselect stream for old track
	if (*SelectedTrack != INDEX_NONE)
	{
        const int StreamIndex = (*Tracks)[*SelectedTrack].StreamIndex;
        StreamComponentClose(StreamIndex);

        UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks %p: Disabled stream %i"), this, StreamIndex);
			
		*SelectedTrack = INDEX_NONE;
		SelectionChanged = true;
        currentStreams--;
       
	}

	// select stream for new track
	if (TrackIndex != INDEX_NONE)
	{
		const int StreamIndex = (*Tracks)[TrackIndex].StreamIndex;
        const auto Settings = GetDefault<UFFMPEGMediaSettings>();

        if (TrackType != EMediaTrackType::Audio || (TrackType == EMediaTrackType::Audio && !Settings->DisableAudio) ) {
            StreamComponentOpen(StreamIndex);
            currentStreams++;
		    UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks %p: Enabled stream %i"), this, StreamIndex);
        }

		*SelectedTrack = TrackIndex;
		SelectionChanged = true;

        if (TrackType == EMediaTrackType::Video) {
            StartDisplayThread();
        }
        else if (TrackType == EMediaTrackType::Audio) {

            srcAudio =  (*Tracks)[TrackIndex].Format.Audio;
            targetAudio = srcAudio;

            audioDiffAvgCoef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
            audioDiffAvgCount = 0;
        
            audioDiffThreshold = (double)(targetAudio.HardwareSize) / targetAudio.BytesPerSec;

            StartAudioRenderThread();
        }
	}

	return true;
}


bool FFFMPEGMediaTracks::SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex)
{
	UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks %p: Setting format on %s track %i to %i"), this, *MediaUtils::TrackTypeToString(TrackType), TrackIndex, FormatIndex);

	FScopeLock Lock(&CriticalSection);

	TArray<FTrack>* Tracks = nullptr;

	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		Tracks = &AudioTracks;
		break;

	case EMediaTrackType::Caption:
		Tracks = &CaptionTracks;
		break;

	case EMediaTrackType::Metadata:
		Tracks = &MetadataTracks;
		break;

	case EMediaTrackType::Video:
		Tracks = &VideoTracks;
		break;

	default:
		return false; // unsupported track type
	};

	check(Tracks != nullptr);

	if (!Tracks->IsValidIndex(TrackIndex))
	{
		return false; // invalid track index
	}


	return true;
}


bool FFFMPEGMediaTracks::SetVideoTrackFrameRate(int32 TrackIndex, int32 FormatIndex, float FrameRate)
{
	UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks %p: Setting frame rate on format %i of video track %i to %f"), this, FormatIndex, TrackIndex, FrameRate);

	FScopeLock Lock(&CriticalSection);

	const FFormat* Format = GetVideoFormat(TrackIndex, FormatIndex);

	if (Format == nullptr)
	{
		return false; // format not found
	}

	if (Format->Video.FrameRate == FrameRate)
	{
		return true; // frame rate already set
	}

	
    return false;
}


/* IMediaControls interface
 *****************************************************************************/


bool FFFMPEGMediaTracks::CanControl(EMediaControl Control) const {
    if (!bPrerolled)
    {
        return false;
    }

    if (Control == EMediaControl::Pause)
    {
        return (CurrentState == EMediaState::Playing);
    }

    if (Control == EMediaControl::Resume)
    {
        return (CurrentState != EMediaState::Playing);
    }

    if ((Control == EMediaControl::Scrub) || (Control == EMediaControl::Seek))
    {
        return true;
    }

    return false;
}

FTimespan FFFMPEGMediaTracks::GetDuration() const
{
    return Duration;
}

float FFFMPEGMediaTracks::GetRate() const {
    return CurrentRate;
}
EMediaState FFFMPEGMediaTracks::GetState() const {
    return CurrentState;
}
EMediaStatus FFFMPEGMediaTracks::GetStatus() const
{
    //TODO: 
    return EMediaStatus::None;
}

TRangeSet<float> FFFMPEGMediaTracks::GetSupportedRates(EMediaRateThinning Thinning) const {
    TRangeSet<float> Result;

    //Result.Add(TRange<float>(PlayerItem.canPlayFastReverse ? -8.0f : -1.0f, 0.0f));
    //Result.Add(TRange<float>(0.0f, PlayerItem.canPlayFastForward ? 8.0f : 0.0f));

    Result.Add(TRange<float>(0.0f, 1.0f));

    return Result;
}

FTimespan FFFMPEGMediaTracks::GetTime() const {
    return CurrentTime;    
}

bool FFFMPEGMediaTracks::IsLooping() const {
    return ShouldLoop;
}

bool FFFMPEGMediaTracks::Seek(const FTimespan& Time) {
    int64_t pos = Time.GetTicks()/10;
    StreamSeek(pos, 0, 0);
    return true;
}

bool FFFMPEGMediaTracks::SetLooping(bool Looping) {
    ShouldLoop = Looping;   
    return true;
}

bool FFFMPEGMediaTracks::SetRate(float Rate) {
    CurrentRate = Rate;


    if (bPrerolled) {
        if (FMath::IsNearlyZero(Rate))
        {
            CurrentState = EMediaState::Paused;
            DeferredEvents.Enqueue(EMediaEvent::PlaybackSuspended);
        }
        else
        {
            CurrentState = EMediaState::Playing;
            DeferredEvents.Enqueue(EMediaEvent::PlaybackResumed);
        }
    }

    return true;
}


/* FFFMPEGMediaTracks implementation
 *****************************************************************************/

bool FFFMPEGMediaTracks::AddStreamToTracks(uint32 StreamIndex, bool IsVideoDevice, const FMediaPlayerTrackOptions& TrackOptions, FString& OutInfo)
{
	OutInfo += FString::Printf(TEXT("Stream %i\n"), StreamIndex);


    AVStream* StreamDescriptor = FormatContext->streams[StreamIndex];
    AVCodecParameters* CodecParams = StreamDescriptor->codecpar;
    AVMediaType MediaType = CodecParams->codec_type;
    
    if ( MediaType != AVMEDIA_TYPE_VIDEO && MediaType != AVMEDIA_TYPE_AUDIO && MediaType != AVMEDIA_TYPE_SUBTITLE) {
        UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Tracks %p: Unsupported major type %s of stream %i"), this,av_get_media_type_string(MediaType), StreamIndex);
        OutInfo += TEXT("\tUnsupported stream type\n");

        return false;
    }

    const auto Settings = GetDefault<UFFMPEGMediaSettings>();

   if ( Settings->DisableAudio && MediaType == AVMEDIA_TYPE_AUDIO ) {
       return false;
   }

	// create & add track
	FTrack* Track = nullptr;
	int32 TrackIndex = INDEX_NONE;
	int32* SelectedTrack = nullptr;

	if (MediaType == AVMEDIA_TYPE_AUDIO)
	{
		SelectedTrack = &SelectedAudioTrack;
		TrackIndex = AudioTracks.AddDefaulted();
		Track = &AudioTracks[TrackIndex];
	}
	else if (MediaType == AVMEDIA_TYPE_SUBTITLE)
	{
		SelectedTrack = &SelectedCaptionTrack;
		TrackIndex = CaptionTracks.AddDefaulted();
		Track = &CaptionTracks[TrackIndex];
	}
	else if (MediaType == AVMEDIA_TYPE_VIDEO)
	{
		SelectedTrack = &SelectedVideoTrack;
		TrackIndex = VideoTracks.AddDefaulted();
		Track = &VideoTracks[TrackIndex];
	}

	check(Track != nullptr);
	check(TrackIndex != INDEX_NONE);
	check(SelectedTrack != nullptr);
    
   

    const FString TypeName =  avcodec_get_name(CodecParams->codec_id);
    OutInfo += FString::Printf(TEXT("\t\tCodec: %s\n"), *TypeName);

    if ( FormatContext->metadata ) {
        AVDictionaryEntry *t = av_dict_get(FormatContext->metadata, "language", NULL, 0);
        if ( t ) {
            Track->Language = t->value;    
        } 
        
    }

    if (MediaType == AVMEDIA_TYPE_AUDIO)
    {
        int samples = FFMAX(AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(CodecParams->sample_rate / AUDIO_MAX_CALLBACKS_PER_SEC));

        Track->Format = { 
            MediaType,
            CodecParams->codec_id,
            TypeName, 
            {
                (uint32)av_samples_get_buffer_size(NULL, CodecParams->channels, 1, AV_SAMPLE_FMT_S16, 1),
                (uint32)CodecParams->channels,
                (uint32)CodecParams->sample_rate,
                CodecParams->channel_layout,
                AV_SAMPLE_FMT_S16,
                (uint32)av_samples_get_buffer_size(NULL, CodecParams->channels, CodecParams->sample_rate,  AV_SAMPLE_FMT_S16, 1),
                (uint32)av_samples_get_buffer_size(NULL, CodecParams->channels, samples, AV_SAMPLE_FMT_S16, 1)
            }, 
            {0} 
        };

        OutInfo += FString::Printf(TEXT("\t\tChannels: %i\n"), Track->Format.Audio.NumChannels);
        OutInfo += FString::Printf(TEXT("\t\tSample Rate: %i Hz\n"),  Track->Format.Audio.SampleRate);
        OutInfo += FString::Printf(TEXT("\t\tBits Per Sample: %i\n"),  Track->Format.Audio.FrameSize * 8);

    } else if ( MediaType == AVMEDIA_TYPE_VIDEO) {
              
        float fps = av_q2d(StreamDescriptor->r_frame_rate);
        if (fps < 0.000025) {
            fps = av_q2d(StreamDescriptor->avg_frame_rate);
        }

     

        OutInfo += FString::Printf(TEXT("\t\tFrame Rate: %g fps\n"), fps);

        int line_sizes[4] = {0};
        av_image_fill_linesizes(line_sizes, (AVPixelFormat)CodecParams->format, CodecParams->width);

        FIntPoint OutputDim = {CodecParams->width, CodecParams->height};

        OutInfo += FString::Printf(TEXT("\t\tDimensions: %i x %i\n"), OutputDim.X, OutputDim.Y);


        Track->Format = {
            MediaType,
            CodecParams->codec_id,
            TypeName,
            {
               0
            },
            {
                0
            }
        };

        Track->Format.Video.BitRate = CodecParams->bit_rate;
        Track->Format.Video.OutputDim = OutputDim;
        Track->Format.Video.FrameRate = fps;
        Track->Format.Video.LineSize[0] = line_sizes[0];
        Track->Format.Video.LineSize[1] = line_sizes[1];
        Track->Format.Video.LineSize[2] = line_sizes[2];
        Track->Format.Video.LineSize[3] = line_sizes[3];

    } else {
        Track->Format = {
            MediaType,
            CodecParams->codec_id,
            TypeName,
            {
               0
            },
            {
               0
            }
        };
    }
    

	Track->DisplayName = (Track->Name.IsEmpty())
		? FText::Format(LOCTEXT("UnnamedStreamFormat", "Unnamed Track (Stream {0})"), FText::AsNumber((uint32)StreamIndex))
		: FText::FromString(Track->Name);

	
	Track->StreamIndex = StreamIndex;
    
    if (MediaType == AVMEDIA_TYPE_SUBTITLE && TrackOptions.Caption == INDEX_NONE) {
        return false;
    }

	return true;
}




const FFFMPEGMediaTracks::FFormat* FFFMPEGMediaTracks::GetAudioFormat(int32 TrackIndex, int32 FormatIndex) const
{
	if (AudioTracks.IsValidIndex(TrackIndex))
	{
		const FTrack& Track = AudioTracks[TrackIndex];
		return &Track.Format;
	}

	return nullptr;
}


const FFFMPEGMediaTracks::FTrack* FFFMPEGMediaTracks::GetTrack(EMediaTrackType TrackType, int32 TrackIndex) const
{
	switch (TrackType)
	{
	case EMediaTrackType::Audio:
		if (AudioTracks.IsValidIndex(TrackIndex))
		{
			return &AudioTracks[TrackIndex];
		}

	case EMediaTrackType::Metadata:
		if (MetadataTracks.IsValidIndex(TrackIndex))
		{
			return &MetadataTracks[TrackIndex];
		}

	case EMediaTrackType::Caption:
		if (CaptionTracks.IsValidIndex(TrackIndex))
		{
			return &CaptionTracks[TrackIndex];
		}

	case EMediaTrackType::Video:
		if (VideoTracks.IsValidIndex(TrackIndex))
		{
			return &VideoTracks[TrackIndex];
		}

	default:
		break; // unsupported track type
	}

	return nullptr;
}


const FFFMPEGMediaTracks::FFormat* FFFMPEGMediaTracks::GetVideoFormat(int32 TrackIndex, int32 FormatIndex) const
{
	if (VideoTracks.IsValidIndex(TrackIndex))
	{
		const FTrack& Track = VideoTracks[TrackIndex];

		
		return &Track.Format;
		
	}

	return nullptr;
}


/************************************************************************/
/* FFMPEGMediaTracks Implementation                                    */
/************************************************************************/

bool FFFMPEGMediaTracks::isHwAccel(const AVCodec* codec) {
    const AVCodecHWConfig *config;
    enum AVHWDeviceType type;

     if (!avcodec_get_hw_config(codec, 0)) {
         return false;
     }
     bool ret = false;
     for (int i = 0; ; i++) {
         config = avcodec_get_hw_config(codec, i);
         if (!config)
             break;

         type = config->device_type;

         if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
             continue;

        
         const char* type_name = av_hwdevice_get_type_name(type);
         UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Format found: %s"), UTF8_TO_TCHAR(type_name) );
         ret = true;
     }

     return ret;
}

TArray<const AVCodec*>  FFFMPEGMediaTracks::FindDecoders(int codecId, bool hwaccell) {
   
    TArray<const AVCodec*> codecs;
    TArray<const AVCodec*> candidates;

    void* iter = NULL;
    const AVCodec* codec = av_codec_iterate(&iter);
    
    while (codec) {
        if (codec->id == codecId && av_codec_is_decoder(codec)) {
            candidates.Add(codec);
        }
        codec = av_codec_iterate(&iter);
    }

    if (hwaccell) {

        TArray<const AVCodec*> tmp;

        candidates.Sort([](const AVCodec a, const AVCodec b) {
            FString aname = a.name;
            FString bname = b.name;
            return aname < bname;
        });

        for ( int i = 0; i < candidates.Num(); i++) {
            FString name = candidates[i]->name;
            if ( name.Contains(TEXT("cuda"))  || name.Contains(TEXT("cuvid"))) {
                tmp.Add((candidates[i]));
                candidates.RemoveAt(i);
                i--;
            }
        }
        

        tmp.Insert(candidates, tmp.Num()); 
       
        for (const AVCodec* c : tmp) {
            if (isHwAccel(c)) {
                codecs.Add(c);
            }
        }
    }

    if (candidates.Num() > 0) {
        if (!hwaccell) {
            for (const AVCodec* c : candidates) {
                if (!isHwAccel(c)) {
                    codecs.Add(c);
                }
            }
        }
    }

    return codecs;
}

AVHWDeviceType FFFMPEGMediaTracks::FindBetterDeviceType(const AVCodec* codec, int& lastSelection) {
    const AVCodecHWConfig *config;
    enum AVHWDeviceType type;
    TArray<enum AVHWDeviceType> availableTypes;
    for (int i = 0; ; i++) {
        config = avcodec_get_hw_config(codec, i);
        if (!config)
            break;

        type = config->device_type;

        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
            continue;

        availableTypes.Add(type);
    }

    if (availableTypes.Num() == 0 ) {
        lastSelection = -1;
        return AV_HWDEVICE_TYPE_NONE;
    }


    const enum AVHWDeviceType* possibleType =  availableTypes.FindByKey(AV_HWDEVICE_TYPE_CUDA);
    if ( possibleType && (lastSelection < 1) ) {
        lastSelection = 1;
        return *possibleType;
    }

    possibleType = availableTypes.FindByKey(AV_HWDEVICE_TYPE_VIDEOTOOLBOX);
    if (possibleType && (lastSelection < 2)) {
        lastSelection = 2;
        return *possibleType;
    }

    possibleType = availableTypes.FindByKey(AV_HWDEVICE_TYPE_OPENCL);
    if (possibleType && (lastSelection < 3)) {
        lastSelection = 3;
        return *possibleType;
    }

    if ( lastSelection < 4) lastSelection = 4;
    else lastSelection++;

    if ( (lastSelection - 4) < availableTypes.Num() ) {
        return availableTypes[lastSelection - 4];
    }

    lastSelection = -1;
    return AV_HWDEVICE_TYPE_NONE;
}


int FFFMPEGMediaTracks::StreamHasEnoughPackets(AVStream *st, int stream_id, FFMPEGPacketQueue *queue) {
    return stream_id < 0 ||
        queue->IsAbortRequest() ||
        (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        queue->GetNumPackets() > MIN_FRAMES && (!queue->GetDuration() || av_q2d(st->time_base) * queue->GetDuration() > 1.0);
}


enum AVPixelFormat FFFMPEGMediaTracks::GetFormatCallback(AVCodecContext *s, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;
    //int ret;
    FFFMPEGMediaTracks* tracks = (FFFMPEGMediaTracks*)s->opaque;
    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const AVCodecHWConfig  *config = NULL;
        int i;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        for (i = 0;; i++) {
            config = avcodec_get_hw_config(s->codec, i);
            if (!config)
                break;
            if (!(config->methods &
                AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                continue;
            if (config->pix_fmt == *p)
                break;
        }

        if (config) {
            if (config->device_type != tracks->hwAccelDeviceType) {
                // Different hwaccel offered, ignore.
                continue;
            }

        } else {
            continue;
           
        }

      
        tracks->hwAccelPixFmt = *p;
        break;

    }

    return *p;
}


int FFFMPEGMediaTracks::StreamComponentOpen(int stream_index) {

    const auto Settings = GetDefault<UFFMPEGMediaSettings>();

    AVCodecContext *avctx;
    const AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts = NULL;

    AVDictionaryEntry *t = NULL;
    int ret = 0;
    
    int stream_lowres = 0;

    if (stream_index < 0 || stream_index >= (int)FormatContext->nb_streams)
        return -1;

    avctx = avcodec_alloc_context3(NULL);

    ret = avcodec_parameters_to_context(avctx, FormatContext->streams[stream_index]->codecpar);
    if (ret < 0) {
        avcodec_free_context(&avctx);
        return ret;
    }
    avctx->pkt_timebase = FormatContext->streams[stream_index]->time_base;
    
#ifdef UE_BUILD_DEBUG
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO ) {
        UE_LOG(LogFFMPEGMedia, Display, TEXT("Video Codec: %s "), UTF8_TO_TCHAR(avcodec_get_name( avctx->codec_id)));
    }
    
    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO ) {
        UE_LOG(LogFFMPEGMedia, Display, TEXT("Audio Codec: %s "), UTF8_TO_TCHAR(avcodec_get_name( avctx->codec_id)));
    }
#endif
   

    if (Settings->UseHardwareAcceleratedCodecs && avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        TArray<const AVCodec*> hwCodecs = FindDecoders(avctx->codec_id, true);
        codec = NULL;

        for (const AVCodec* hwCodec : hwCodecs) {
            AVCodecContext* avctx2 = avcodec_alloc_context3(NULL);
            avcodec_parameters_to_context(avctx2, FormatContext->streams[stream_index]->codecpar);
            if (avcodec_open2(avctx2, hwCodec, NULL) >= 0) {
                AVBufferRef *device_ref = NULL;
                int lastSelection = 0;
                while (lastSelection >= 0) {
                    enum AVHWDeviceType type = FindBetterDeviceType(hwCodec, lastSelection);
                    ret = av_hwdevice_ctx_create(&device_ref, type, NULL, NULL, 0);
                    if (ret < 0) {
                        hw_device_ctx = NULL;
                        hwAccelDeviceType = AV_HWDEVICE_TYPE_NONE;
                        hwaccel_retrieve_data = nullptr;
                    }
                    else {
                        hw_device_ctx = device_ref;
                        avctx->hw_device_ctx = av_buffer_ref(device_ref);

                        const char* type_name = av_hwdevice_get_type_name(type);
                        UE_LOG(LogFFMPEGMedia, Display, TEXT("Using hardware context type: %s"), UTF8_TO_TCHAR(type_name));

                        hwAccelDeviceType = type;
                        hwaccel_retrieve_data = HWAccelRetrieveDataCallback;
                        codec = hwCodec;

                        avctx->opaque = this;
                        avctx->get_format = GetFormatCallback;
                        avctx->thread_safe_callbacks = 1;

                        break;
                    }
                }
            }
            avcodec_free_context(&avctx2);
            if (codec) break;
        }
        if (!codec) {
            codec = avcodec_find_decoder(avctx->codec_id);
        }
    } else {
        codec = avcodec_find_decoder(avctx->codec_id);
    }

    if ( !codec) {
        const AVCodecDescriptor * desc = avcodec_descriptor_get(avctx->codec_id);
        UE_LOG(LogFFMPEGMedia, Error, TEXT("coudn't find a decoder for %s"), UTF8_TO_TCHAR(desc->long_name));
        return -1;
    }
    
    
    UE_LOG(LogFFMPEGMedia, Display, TEXT("Using codec: %s - %d"), UTF8_TO_TCHAR(codec->name), codec->id);
    

    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
        UE_LOG(LogFFMPEGMedia, Warning, TEXT("The maximum value for lowres supported by the decoder is %d"), codec->max_lowres);
        stream_lowres = codec->max_lowres;
    }
     avctx->lowres = stream_lowres;

    if (Settings->SpeedUpTricks)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;


    int thread_count = 0;
    if ( avctx->codec_type == AVMEDIA_TYPE_VIDEO ) thread_count = Settings->VideoThreads;
    if ( avctx->codec_type == AVMEDIA_TYPE_AUDIO ) thread_count = Settings->AudioThreads;

    if (!thread_count) {
        thread_count = av_cpu_count();
    }

    thread_count = FFMAX(1, FFMIN(thread_count, 16));
    avctx->thread_count = thread_count;
    av_dict_set(&opts, "threads", TCHAR_TO_UTF8(*FString::FromInt(thread_count)), 0);

    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_dict_set(&opts, "refcounted_frames", "1", 0);

    if (Settings->ZeroLatencyStreaming && avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        av_dict_set(&opts, "tune", "zerolatency", 0);
    }

    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        if (Settings->UseHardwareAcceleratedCodecs && avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            UE_LOG(LogFFMPEGMedia, Warning, TEXT("Coudn't open the hwaccel codec, trying a different one"));
            codec = avcodec_find_decoder(avctx->codec_id);
            avctx->codec_id = codec->id;
            if (stream_lowres > codec->max_lowres) {
                UE_LOG(LogFFMPEGMedia, Warning, TEXT("The maximum value for lowres supported by the decoder is %d"),  codec->max_lowres);
                stream_lowres =  codec->max_lowres;
            }
            avctx->lowres= stream_lowres;
            if ( thread_count > 0) {
                avctx->thread_count =thread_count;
            }

            if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
                avcodec_free_context(&avctx);
                return ret;
            }

        }
        else {
            avcodec_free_context(&avctx);
            return ret;
        }
    }
  
    eof = 0;
    FormatContext->streams[stream_index]->discard = AVDISCARD_DEFAULT;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        audioStream = FormatContext->streams[stream_index];
        audioStreamIdx = stream_index;
        auddec->Init(avctx, &audioq, &continueReadCond);
        if ((FormatContext->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !FormatContext->iformat->read_seek) {
            auddec->SetTime(audioStream->start_time, audioStream->time_base);
        }
        if ((ret = auddec->Start([this](void * data) {return AudioThread();}, NULL)) < 0) {
            av_dict_free(&opts);
            return ret;
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        videoStream = FormatContext->streams[stream_index];
        videoStreamIdx = stream_index;
        viddec->Init(avctx, &videoq, &continueReadCond);
        if ((ret = viddec->Start([this](void * data) {return VideoThread();}, NULL)) < 0) {
            av_dict_free(&opts);
            return ret;
        }
        queueAttachmentsReq = true;
        video_ctx = avctx;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        subTitleStream = FormatContext->streams[stream_index];
        subtitleStreamIdx = stream_index;
        subdec->Init(avctx, &subtitleq, &continueReadCond);
        if ((ret = subdec->Start([this](void * data) {return SubtitleThread();}, NULL)) < 0) {
            av_dict_free(&opts);
            return ret;
        }
        break;
    default:
        break;
    }

    av_dict_free(&opts);

    return ret;

}
void FFFMPEGMediaTracks::StreamComponentClose(int stream_index) {
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= (int)FormatContext->nb_streams)
        return;
    codecpar = FormatContext->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        auddec->Abort(&sampq);
        StopAudioRenderThread();
    
        auddec->Destroy();
        swr_free(&swrContext);
        swrContext = NULL;
        av_freep(&audioBuf1);
        audioBuf1Size = 0;
        audioBuf = NULL;
        audioClock = 0;
        break;
    case AVMEDIA_TYPE_VIDEO:
        viddec->Abort(&pictq);
        StopDisplayThread();

        video_ctx = NULL;
        viddec->Destroy();
        SelectedVideoTrack = -1;

        if ( hw_device_ctx ) {
            av_buffer_unref(&hw_device_ctx); 
        }
       
        hwaccel_retrieve_data = nullptr;
        hwAccelPixFmt = AV_PIX_FMT_NONE;
       

        break;
    case AVMEDIA_TYPE_SUBTITLE:
        subdec->Abort(&subpq);
        subdec->Destroy();
        break;
    default:
        break;
    }

    FormatContext->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        audioStream = NULL;
        SelectedAudioTrack = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        videoStream = NULL;
        SelectedVideoTrack = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        subTitleStream = NULL;
        SelectedCaptionTrack = -1;
        break;
    default:
        break;
    }
}

void FFFMPEGMediaTracks::StepToNextFrame() {
    if (CurrentState == EMediaState::Paused) {
        SetRate(1.0f);
        StreamTogglePause();
    }

    step = 1;
}

void FFFMPEGMediaTracks::StreamTogglePause() {
    if (CurrentState == EMediaState::Paused) {
        frameTimer += av_gettime_relative() / 1000000.0 - vidclk.GetLastUpdated();
        if (readPauseReturn != AVERROR(ENOSYS)) {
            vidclk.SetPaused(false);
        }
        vidclk.Set(vidclk.Get(), vidclk.GetSerial());
    }
    extclk.Set(extclk.Get(), extclk.GetSerial());
    bool paused = CurrentState == EMediaState::Paused || CurrentState == EMediaState::Stopped;
    paused = !paused;

    audclk.SetPaused(paused);
    vidclk.SetPaused(paused);
    extclk.SetPaused(paused);
}

double FFFMPEGMediaTracks::ComputeTargetDelay(double delay) {
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (getMasterSyncType() != ESynchronizationType::VideoMaster) {
        /* if video is slave, we try to correct big delays by
            duplicating or deleting a FFMPEGFrame */
        diff = vidclk.Get() - GetMasterClock();

        /* skip or repeat frame. We take into account the
            delay to compute the threshold. I still don't know
            if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < maxFrameDuration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    /*OFX_LOGP(ofx_trace,  "video: delay=%0.3f A-V=%f\n",
            delay, -diff);*/

    return delay;
}

ESynchronizationType FFFMPEGMediaTracks::getMasterSyncType() {

    if (sychronizationType == ESynchronizationType::VideoMaster) {
        if (videoStream)
            return ESynchronizationType::VideoMaster;
        else
            return ESynchronizationType::AudioMaster;
    }
    else if (sychronizationType == ESynchronizationType::AudioMaster) {
        if (audioStream)
            return ESynchronizationType::AudioMaster;
        else if (videoStream)
            return ESynchronizationType::VideoMaster;
    }
    
    return ESynchronizationType::ExternalClock;
}

void FFFMPEGMediaTracks::UpdateVideoPts(double pts, int64_t pos, int serial) {
    vidclk.Set(pts, serial);
    extclk.SyncToSlave(&vidclk);
}

void FFFMPEGMediaTracks::CheckExternalClockSpeed() {
    if (videoStreamIdx >= 0 && videoq.GetNumPackets() <= EXTERNAL_CLOCK_MIN_FRAMES ||
        audioStreamIdx >= 0 && audioq.GetNumPackets() <= EXTERNAL_CLOCK_MIN_FRAMES) {
        extclk.SetSpeed(FFMAX(EXTERNAL_CLOCK_SPEED_MIN, extclk.GetSpeed() - EXTERNAL_CLOCK_SPEED_STEP));
    }
    else if ((videoStreamIdx < 0 || videoq.GetNumPackets() > EXTERNAL_CLOCK_MAX_FRAMES) &&
        (audioStreamIdx < 0 || audioq.GetNumPackets() > EXTERNAL_CLOCK_MAX_FRAMES)) {
        extclk.SetSpeed(FFMIN(EXTERNAL_CLOCK_SPEED_MAX, extclk.GetSpeed() + EXTERNAL_CLOCK_SPEED_STEP));
    }
    else {
        double speed = extclk.GetSpeed();
        if (speed != 1.0)
            extclk.SetSpeed(speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
}

double FFFMPEGMediaTracks::GetMasterClock() {
    double val;

    switch (getMasterSyncType()) {
    case ESynchronizationType::VideoMaster:
        val = vidclk.Get();
        break;
    case ESynchronizationType::AudioMaster:
        val = audclk.Get();
        break;
    default:
        val = extclk.Get();
        break;
    }
    return val;
}

int FFFMPEGMediaTracks::UploadTexture(FFMPEGFrame* vp, AVFrame *frame, struct SwsContext **img_convert_ctx) {
    
    int size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame->width, frame->height, 1);

    int bufSize = size + 1;

    if (dataBuffer.Num() != bufSize) {
        dataBuffer.Reset();
        dataBuffer.AddUninitialized(bufSize);
        check(dataBuffer.Num() == bufSize);
    }
    int ret = 0;
    int pitch[4] = { 0, 0, 0, 0 };
    ret = av_image_fill_linesizes(pitch, AV_PIX_FMT_BGRA, frame->width);

    uint8_t* data[4] = { 0 };
    av_image_fill_pointers(data, AV_PIX_FMT_BGRA, frame->height, dataBuffer.GetData(), pitch);


    *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
        frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height, AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);


    if (*img_convert_ctx != NULL) {
        sws_scale(*img_convert_ctx, frame->data, frame->linesize, 0, frame->height, data, pitch);
    }
    else {
        UE_LOG(LogFFMPEGMedia, Error, TEXT("Cannot initialize the conversion context"));
        ret = -1;
        return ret;
    }


    FScopeLock Lock(&CriticalSection);
    const TSharedRef<FFFMPEGMediaTextureSample, ESPMode::ThreadSafe> TextureSample = VideoSamplePool->AcquireShared();

    FIntPoint Dim = {frame->width, frame->height};

    FTimespan time = FTimespan::FromSeconds(vp->GetPts());
    FTimespan duration = FTimespan::FromSeconds(vp->GetDuration());

    if (TextureSample->Initialize(
        dataBuffer.GetData(),
        dataBuffer.Num()-1,
        Dim,
        pitch[0],
        time,
        duration))
    {
        VideoSampleQueue.Enqueue(TextureSample);
    }


    return ret;
}

void FFFMPEGMediaTracks::VideoDisplay () {
    if (videoStream) {
            FFMPEGFrame *vp;
            FFMPEGFrame *sp = NULL;
            

            vp = pictq.PeekLast();
            if (subTitleStream) {
                if (subpq.GetNumRemaining() > 0) {
                    sp = subpq.Peek();

                    if (vp->GetPts() >= sp->GetPts() + ((float)sp->GetSub().start_display_time / 1000)) {
                        if (!sp->IsUploaded()) {
                            
                            int i;
                            if (!sp->GetWidth() || !sp->GetHeight()) {
                                sp->UpdateSize(vp);
                            }
                            
                            FTimespan Time = FTimespan::FromSeconds(sp->GetPts());
                            FTimespan CurrentDuration = FTimespan::FromSeconds(sp->GetDuration());

                            for (i = 0; i < (int)sp->GetSub().num_rects; i++) {
                                AVSubtitleRect *sub_rect = sp->GetSub().rects[i];
                                sub_rect->x = av_clip(sub_rect->x, 0, sp->GetWidth());
                                sub_rect->y = av_clip(sub_rect->y, 0, sp->GetHeight());
                                sub_rect->w = av_clip(sub_rect->w, 0, sp->GetWidth() - sub_rect->x);
                                sub_rect->h = av_clip(sub_rect->h, 0, sp->GetHeight() - sub_rect->y);

                                if ( sub_rect->type == SUBTITLE_TEXT ||  sub_rect->type == SUBTITLE_ASS) {
                                    FScopeLock Lock(&CriticalSection);

                                    const auto CaptionSample = MakeShared<FFFMPEGMediaOverlaySample, ESPMode::ThreadSafe>();

                                    if (CaptionSample->Initialize(sub_rect->type == SUBTITLE_TEXT?sub_rect->text:sub_rect->ass, FVector2D(sub_rect->x, sub_rect->y), Time, CurrentDuration))
                                    {
                                        CaptionSampleQueue.Enqueue(CaptionSample);
                                    }
                                }

                            }
                            sp->SetUploaded(true);
                        }
                    }
                    else
                        sp = NULL;
                }
            }

            if (!vp->IsUploaded()) {
                vp->SetVerticalFlip(vp->GetFrame()->linesize[0] < 0);
                if (UploadTexture(vp, vp->GetFrame(), &imgConvertCtx) < 0)
                    return;
                vp->SetUploaded(true);
                
            } 
        }   
}

void FFFMPEGMediaTracks::StreamSeek( int64_t pos, int64_t rel, int seek_by_bytes) {
    if (!seekReq) {
        seekPos = pos;
        seekRel = rel;
        seekFlags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
            seekFlags |= AVSEEK_FLAG_BYTE;
        seekReq = 1;
        continueReadCond.signal();
    }
}

int FFFMPEGMediaTracks::ReadThread() {

    const auto Settings = GetDefault<UFFMPEGMediaSettings>();

    FCriticalSection wait_mutex;
    AVPacket pkt1, *pkt = &pkt1;
    int64_t stream_start_time;
    int pkt_in_play_range = 0;

    int scan_all_pmts_set = 0;
    int64_t pkt_ts;
    int ret = 0;
    int read_pause_return = -1;

    bool paused = CurrentState == EMediaState::Paused;
    bool last_paused = false;

    

    for (;;) {
        if (aborted)
            break;

        if (currentStreams < totalStreams) {
            wait_mutex.Lock();
            continueReadCond.waitTimeout(wait_mutex, 10);
            wait_mutex.Unlock();
            continue;
        }

        paused = ( CurrentState == EMediaState::Paused || CurrentState == EMediaState::Stopped);

        

        if (paused != last_paused) {
            last_paused = paused;

            if (paused)
                read_pause_return = av_read_pause(FormatContext);
            else
                av_read_play(FormatContext);
        }      

        if (  seekReq ) {
            int64_t seek_target = seekPos;
            int64_t seek_min = seekRel > 0 ? seek_target - seekRel + 2 : INT64_MIN;
            int64_t seek_max = seekRel < 0 ? seek_target - seekRel - 2 : INT64_MAX;

            ret = avformat_seek_file(FormatContext, -1, seek_min, seek_target, seek_max, seekFlags);
            if (ret < 0) {
                UE_LOG(LogFFMPEGMedia, Error, TEXT("%s: error while seeking"), UTF8_TO_TCHAR(FormatContext->url));
            } else {
                if (SelectedAudioTrack  != INDEX_NONE) {
                    audioq.Flush();
                    audioq.PutFlush();
                }
                if (SelectedCaptionTrack != INDEX_NONE) {
                    subtitleq.Flush();
                    subtitleq.PutFlush();
                }
                if (SelectedVideoTrack != INDEX_NONE) {
                    videoq.Flush();
                    videoq.PutFlush();
                }
                if (seekFlags & AVSEEK_FLAG_BYTE) {
                    extclk.Set(NAN, 0);
                }
                else {
                    extclk.Set(seek_target / (double)AV_TIME_BASE, 0);
                }

                FlushSamples();
                DeferredEvents.Enqueue(EMediaEvent::SeekCompleted);
            }
            seekReq = false;
            queueAttachmentsReq = true;
            eof = 0;
            
            if (CurrentState == EMediaState::Paused)
                StepToNextFrame();
        }


        if (queueAttachmentsReq) {
            if (videoStream && videoStream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket copy = { 0 };
                if ((ret = av_packet_ref(&copy, &videoStream->attached_pic)) < 0) {
                    return -1;
                }

                videoq.Put(&copy);
                videoq.PutNullPacket(videoStreamIdx);
            }
            queueAttachmentsReq = false;
        }

        if (!Settings->UseInfiniteBuffer &&
            (audioq.GetSize() + videoq.GetSize() + subtitleq.GetSize() > MAX_QUEUE_SIZE
                || (StreamHasEnoughPackets(audioStream, audioStreamIdx, &audioq) &&
                    StreamHasEnoughPackets(videoStream, videoStreamIdx, &videoq) &&
                    StreamHasEnoughPackets(subTitleStream, subtitleStreamIdx, &subtitleq)))) {
            /* wait 20 ms */
            wait_mutex.Lock();
            continueReadCond.waitTimeout(wait_mutex, 20);
            wait_mutex.Unlock();
            continue;
        }

        if (!paused &&
            (!audioStream || (auddec->GetFinished() == audioq.GetSerial() && sampq.GetNumRemaining() == 0)) &&
            (!videoStream || (viddec->GetFinished() == videoq.GetSerial() && pictq.GetNumRemaining() == 0))) {
            
            FlushSamples();
            
            if (ShouldLoop) {
                DeferredEvents.Enqueue(EMediaEvent::PlaybackEndReached);
                StreamSeek(0, 0, 0);
            }
            else {
                CurrentState = EMediaState::Stopped;
                DeferredEvents.Enqueue(EMediaEvent::PlaybackEndReached);
                DeferredEvents.Enqueue(EMediaEvent::PlaybackSuspended);
                bPrerolled = false;
            }
        }
        try {
            ret = av_read_frame(FormatContext, pkt);
		} catch(...) {
		    ret = -1;
		}

        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(FormatContext->pb)) && !eof) {
                if (videoStreamIdx >= 0)
                    videoq.PutNullPacket(videoStreamIdx);
                if (audioStreamIdx >= 0)
                    audioq.PutNullPacket(audioStreamIdx);
                if (subtitleStreamIdx >= 0)
                    subtitleq.PutNullPacket(subtitleStreamIdx);
                eof = 1;
            }
            if (FormatContext->pb && FormatContext->pb->error)
                break;

            wait_mutex.Lock();
            continueReadCond.waitTimeout(wait_mutex, 5);
            wait_mutex.Unlock();
            continue;
        }
        else {
            eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = FormatContext->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;

        pkt_in_play_range = 1;
        if (pkt->stream_index == audioStreamIdx && pkt_in_play_range) {
            audioq.Put(pkt);
        }
        else if (pkt->stream_index == videoStreamIdx && pkt_in_play_range
            && !(videoStream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            videoq.Put(pkt);
        }
        else if (pkt->stream_index == subtitleStreamIdx && pkt_in_play_range) {
            subtitleq.Put(pkt);
        }
        else {
            av_packet_unref(pkt);
        }
    }
    return 0;
}

int FFFMPEGMediaTracks::AudioThread() {
  
    AVFrame *frame = av_frame_alloc();
    FFMPEGFrame *af;

    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        if ((got_frame = auddec->DecodeFrame(frame, NULL)) < 0) {
            av_frame_free(&frame);
            return ret;
        }

        if (got_frame) {
            tb = { 1, frame->sample_rate };
            af = sampq.PeekWritable();
            if (!af) {
                av_frame_free(&frame);
                return ret;
            }

            af->SetPts((frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb));
            af->SetPos(frame->pkt_pos);
            af->SetSerial(auddec->GetPktSerial());
            af->SetDuration(av_q2d({ frame->nb_samples, frame->sample_rate }));

            av_frame_move_ref(af->GetFrame(), frame);
            sampq.Push();

        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

    av_frame_free(&frame);
    return ret;
}

int FFFMPEGMediaTracks::SubtitleThread() {
    FFMPEGFrame *sp;
    int got_subtitle;
    double pts;

    for (;;) {
        sp = subpq.PeekWritable();
        if (!sp)
            return 0;

        if ((got_subtitle = subdec->DecodeFrame(NULL, &sp->GetSub())) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->GetSub().format == 0) {
            if (sp->GetSub().pts != AV_NOPTS_VALUE)
                pts = sp->GetSub().pts / (double)AV_TIME_BASE;


            sp->SetPts(pts);
            sp->SetSerial(subdec->GetPktSerial());
            sp->SetWidth(subdec->GetAvctx()->width);
            sp->SetHeight(subdec->GetAvctx()->height);
            sp->SetUploaded(false);

            /* now we can update the picture count */
            subpq.Push();
        }
        else if (got_subtitle) {
            avsubtitle_free(&sp->GetSub());
        }
    }
    return 0;
}

int FFFMPEGMediaTracks::SynchronizeAudio( int nb_samples) {
     int wanted_nb_samples = nb_samples;

        /* if not master, then we try to remove or add samples to correct the clock */
        if (getMasterSyncType() != ESynchronizationType::AudioMaster) {
            double diff, avg_diff;
            int min_nb_samples, max_nb_samples;

            diff = audclk.Get() - GetMasterClock();

            if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
                audioDiffCum = diff + audioDiffAvgCoef * audioDiffCum;
                if (audioDiffAvgCount < AUDIO_DIFF_AVG_NB) {
                    /* not enough measures to have a correct estimate */
                    audioDiffAvgCount++;
                } else {
                    /* estimate the A-V difference */
                    avg_diff = audioDiffCum * (1.0 - audioDiffAvgCoef);

                    if (fabs(avg_diff) >= audioDiffThreshold) {
                        wanted_nb_samples = nb_samples + (int)(diff * srcAudio.SampleRate);
                        min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                        max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                        wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                    }
                    /*OFX_LOGP(ofx_trace, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                            diff, avg_diff, wanted_nb_samples - nb_samples,
                            audioClock, audioDiffThreshold);*/
                }
            } else {
                /* too big difference : may be initial PTS errors, so
                   reset A-V filter */
                audioDiffAvgCount = 0;
                audioDiffCum       = 0;
            }
        }

        return wanted_nb_samples;
}

int FFFMPEGMediaTracks::AudioDecodeFrame(FTimespan& time, FTimespan& duration) {
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    FFMPEGFrame *af;

    if (CurrentState == EMediaState::Paused || CurrentState == EMediaState::Stopped)
        return -1;

    do {
#if defined(_WIN32)
        while (sampq.GetNumRemaining() == 0) {
            if ((av_gettime_relative() - audioCallbackTime) > 1000000LL * targetAudio.HardwareSize /  targetAudio.BytesPerSec / 2)
                return -1;
            av_usleep(1000);
        }
#endif
        af = sampq.PeekReadable();
        if (!af)
            return -1;
        sampq.Next();
    } while (af->GetSerial() != audioq.GetSerial());

    data_size = av_samples_get_buffer_size(NULL, af->GetFrame()->channels, af->GetFrame()->nb_samples, (AVSampleFormat)af->GetFrame()->format, 1);

    dec_channel_layout =
        (af->GetFrame()->channel_layout && af->GetFrame()->channels == av_get_channel_layout_nb_channels(af->GetFrame()->channel_layout)) ?
        af->GetFrame()->channel_layout : av_get_default_channel_layout(af->GetFrame()->channels);
    wanted_nb_samples = SynchronizeAudio(af->GetFrame()->nb_samples);

    if (getMasterSyncType() == ESynchronizationType::AudioMaster) {
        CurrentTime = FTimespan::FromSeconds(af->GetPts());
        //CurrentTime = FTimespan::FromSeconds(audclk.get());
    }

    if (af->GetFrame()->format != srcAudio.Format ||
        dec_channel_layout != srcAudio.ChannelLayout ||
        af->GetFrame()->sample_rate != srcAudio.SampleRate||
        (wanted_nb_samples != af->GetFrame()->nb_samples && !swrContext)) {
        swr_free(&swrContext);
        swrContext = swr_alloc_set_opts(NULL,
            targetAudio.ChannelLayout, targetAudio.Format,targetAudio.SampleRate,
            dec_channel_layout, (AVSampleFormat)af->GetFrame()->format, af->GetFrame()->sample_rate,
            0, NULL);
        if (!swrContext || swr_init(swrContext) < 0) {
            UE_LOG(LogFFMPEGMedia, Error, 
                TEXT("Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!"),
                af->GetFrame()->sample_rate, UTF8_TO_TCHAR( av_get_sample_fmt_name((AVSampleFormat)af->GetFrame()->format)), af->GetFrame()->channels,
                targetAudio.SampleRate,  UTF8_TO_TCHAR(av_get_sample_fmt_name(targetAudio.Format)), targetAudio.NumChannels);
            swr_free(&swrContext);
            return -1;
        }
        srcAudio.ChannelLayout = dec_channel_layout;
        srcAudio.NumChannels = af->GetFrame()->channels;
        srcAudio.SampleRate = af->GetFrame()->sample_rate;
        srcAudio.Format = (AVSampleFormat)af->GetFrame()->format;
    }

    if (swrContext) {
        const uint8_t **in = (const uint8_t **)af->GetFrame()->extended_data;
        uint8_t **out = &audioBuf1;
        int out_count = (int64_t)wanted_nb_samples * targetAudio.SampleRate / af->GetFrame()->sample_rate + 256;
        int out_size = av_samples_get_buffer_size(NULL, targetAudio.NumChannels, out_count,targetAudio.Format, 0);
        int len2;
        if (out_size < 0) {
            UE_LOG(LogFFMPEGMedia, Error,  TEXT("av_samples_get_buffer_size() failed"));
            return -1;
        }
        if (wanted_nb_samples != af->GetFrame()->nb_samples) {
            if (swr_set_compensation(swrContext, (wanted_nb_samples - af->GetFrame()->nb_samples) * targetAudio.SampleRate / af->GetFrame()->sample_rate,
                wanted_nb_samples *targetAudio.SampleRate / af->GetFrame()->sample_rate) < 0) {
                UE_LOG(LogFFMPEGMedia, Error,  TEXT("swr_set_compensation() failed"));
                return -1;
            }
        }
        av_fast_malloc(&audioBuf1, &audioBuf1Size, out_size);
        if (!audioBuf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(swrContext, out, out_count, in, af->GetFrame()->nb_samples);
        if (len2 < 0) {
            UE_LOG(LogFFMPEGMedia, Error,  TEXT("swr_convert() failed"));
            return -1;
        }
        if (len2 == out_count) {
            UE_LOG(LogFFMPEGMedia, Warning,  TEXT("audio buffer is probably too small"));
            if (swr_init(swrContext) < 0)
                swr_free(&swrContext);
        }
        audioBuf = audioBuf1;
        resampled_data_size = len2 * targetAudio.NumChannels * av_get_bytes_per_sample(targetAudio.Format);
    }
    else {
        audioBuf = af->GetFrame()->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = audioClock;
    /* update the audio clock with the pts */
    if (!isnan(af->GetPts()))
        audioClock = af->GetPts() + (double)af->GetFrame()->nb_samples / af->GetFrame()->sample_rate;
    else
        audioClock = NAN;
    audioClockSerial = af->GetSerial();

    time = FTimespan::FromSeconds(audioClock);
    duration = FTimespan::FromSeconds(af->GetDuration());
    
    return resampled_data_size;
}



void FFFMPEGMediaTracks::RenderAudio() {
    int audio_size, len1;

    audioCallbackTime = av_gettime_relative();
    FTimespan time = 0;
    FTimespan duration = 0;

    audio_size = AudioDecodeFrame(time, duration);
    if (audio_size < 0) {
        /* if error, just output silence */
        audioBuf = NULL;
        audioBufSize = AUDIO_MIN_BUFFER_SIZE / targetAudio.FrameSize * targetAudio.FrameSize;
    }
    else {
        audioBufSize = audio_size;
    }
    len1 = audioBufSize ;

    if (CurrentState == EMediaState::Paused || CurrentState == EMediaState::Stopped) {
        //Ignore the frame
    } else {
        if ( audioBuf != NULL ) {
            FScopeLock Lock(&CriticalSection);
            const TSharedRef<FFFMPEGMediaAudioSample, ESPMode::ThreadSafe> AudioSample = AudioSamplePool->AcquireShared();

            if (AudioSample->Initialize((uint8_t *)audioBuf, len1, targetAudio.NumChannels, targetAudio.SampleRate, time, duration))
            {
                AudioSampleQueue.Enqueue(AudioSample);
            }
        }
    }

    if (!isnan(audioClock)) {
        audclk.SetAt(audioClock - (double)(2 * targetAudio.HardwareSize + audioBufSize) / targetAudio.BytesPerSec, audioClockSerial, audioCallbackTime / 1000000.0);
        extclk.SyncToSlave(&audclk);
    }
      
}

void FFFMPEGMediaTracks::StartDisplayThread() {
    StopDisplayThread();
    displayRunning = true;
    displayThread = LambdaFunctionRunnable::RunThreaded("DisplayThread", [this](){
       DisplayThread(); 
    });
}

void FFFMPEGMediaTracks::StopDisplayThread() {
    if (displayRunning && displayThread) {        
        displayThread = nullptr;
    }
    displayRunning = false;
}

void FFFMPEGMediaTracks::StartAudioRenderThread() {
    StopDisplayThread();
    audioRunning = true;
    audioRenderThread = LambdaFunctionRunnable::RunThreaded("AudioRenderThread", [this]() {
        AudioRenderThread();
    });
}

void FFFMPEGMediaTracks::StopAudioRenderThread() {
    if (audioRunning && audioRenderThread) {
        audioRenderThread = nullptr;
    }
    audioRunning = false;
}

int FFFMPEGMediaTracks::DisplayThread() {
    double remaining_time = 0.0;

    while (displayRunning) {
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if (bPrerolled && CurrentState == EMediaState::Playing || forceRefresh  ) {
            VideoRefresh(&remaining_time);   
        }
    }
    return 0;
}

int FFFMPEGMediaTracks::AudioRenderThread() {
    double remaining_time = 0.0;

    
    int64_t startTime =  av_gettime_relative();
    while (audioRunning) {
        if ( bPrerolled) {
            RenderAudio();
            int64_t endTime =  av_gettime_relative();
            int64_t dif = endTime - startTime;
            if ( dif < 33333) {
                av_usleep(33333 - dif);
            }
            startTime = endTime;
        }
        
    }
    return 0;
}


void FFFMPEGMediaTracks::VideoRefresh(double *remaining_time) {
    const auto Settings = GetDefault<UFFMPEGMediaSettings>();
    double time;

    FFMPEGFrame *sp, *sp2;

    if (CurrentState == EMediaState::Playing && getMasterSyncType() == ESynchronizationType::ExternalClock && realtime)
        CheckExternalClockSpeed();

      if (videoStream) {
        bool retry = true;

        while (retry && displayRunning) {
            if (pictq.GetNumRemaining() == 0) {
                // nothing to do, no picture to display in the queue
            } else {
                double last_duration, duration, delay;
                FFMPEGFrame *vp, *lastvp;


                /* dequeue the picture */
                lastvp = pictq.PeekLast();
                vp = pictq.Peek();
                ESynchronizationType sync_type = getMasterSyncType();
                if ( sync_type  == ESynchronizationType::VideoMaster) {
                    //CurrentTime = FTimespan::FromSeconds(vidclk.get());
                    CurrentTime = FTimespan::FromSeconds(vp->GetPts());
                } else if (sync_type == ESynchronizationType::ExternalClock) {
                    CurrentTime = FTimespan::FromSeconds(extclk.Get());
                }


                if (vp->GetSerial() != videoq.GetSerial()) {
                    pictq.Next();
                    continue;
                }

                if (lastvp->GetSerial() != vp->GetSerial())
                    frameTimer = av_gettime_relative() / 1000000.0;

                if (CurrentState == EMediaState::Paused || CurrentState == EMediaState::Stopped) {
                    if (forceRefresh && pictq.GetIndexShown())
                        VideoDisplay();
                   
                    forceRefresh = false;
                    return;
                }

               
                /* compute nominal last_duration */
                last_duration = lastvp->GetDifference(vp, maxFrameDuration);
                delay = ComputeTargetDelay(last_duration);

                time= av_gettime_relative()/1000000.0;
                if (time < frameTimer + delay) {
                    *remaining_time = FFMIN(frameTimer + delay - time, *remaining_time);
                    if (forceRefresh && pictq.GetIndexShown())
                        VideoDisplay();

                    forceRefresh = false;
                    return;
                }


                frameTimer += delay;
                if (delay > 0 && time - frameTimer > AV_SYNC_THRESHOLD_MAX)
                    frameTimer = time;

                pictq.Lock();
                if (!isnan(vp->GetPts()))
                    UpdateVideoPts(vp->GetPts(), vp->GetPos(), vp->GetSerial());
                pictq.Unlock();

                if (pictq.GetNumRemaining() > 1) {
                    FFMPEGFrame *nextvp = pictq.PeekNext();
                    duration = vp->GetDifference(nextvp, maxFrameDuration);
                    if(!step && (Settings->AllowFrameDrop && getMasterSyncType() != ESynchronizationType::VideoMaster) && time > frameTimer + duration){
                        frameDropsLate++;
                        pictq.Next();
                        continue;
                    }
                }

                if (subTitleStream) {
                        while (subpq.GetNumRemaining() > 0) {
                            sp = subpq.Peek();

                            if (subpq.GetNumRemaining() > 1)
                                sp2 = subpq.PeekNext();
                            else
                                sp2 = NULL;

                            if (sp->GetSerial() != subtitleq.GetSerial()
                                    || (vidclk.GetPts() > (sp->GetPts() + ((float) sp->GetSub().end_display_time / 1000)))
                                    || (sp2 && vidclk.GetPts() > (sp2->GetPts() + ((float) sp2->GetSub().start_display_time / 1000))))
                            {
                                subpq.Next();
                            } else {
                                break;
                            }
                        }
                }

                pictq.Next();
                forceRefresh = true;

                if (step && CurrentState == EMediaState::Playing)
                    StreamTogglePause();

                retry = false;
            }
        }

        /* display picture */
        if (forceRefresh && pictq.GetIndexShown())
            VideoDisplay();
    }
    forceRefresh = false;
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
            if (audioStream)
                aqsize = audioq.get_size();
            if (videoStream)
                vqsize = videoq.get_size();
            if (subTitleStream)
                sqsize = subtitleq.get_size();
            av_diff = 0;
            if (audioStream && videoStream)
                av_diff = audclk.get() - vidclk.get();
            else if (videoStream)
                av_diff = get_master_clock(is) - vidclk.get();
            else if (audioStream)
                av_diff = get_master_clock(is) - audclk.get();
            / *av_log(NULL, AV_LOG_INFO,
                   "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
                   get_master_clock(is),
                   (audioStream && videoStream) ? "A-V" : (videoStream ? "M-V" : (audioStream ? "M-A" : "   ")),
                   av_diff,
                   frameDropsEarly + frameDropsLate,
                   aqsize / 1024,
                   vqsize / 1024,
                   sqsize,
                   videoStream ? viddec.avctx->pts_correction_num_faulty_dts : 0,
                   videoStream ? viddec.avctx->pts_correction_num_faulty_pts : 0);* /
            fflush(stdout);
            last_time = cur_time;
        }
    }*/



}


int FFFMPEGMediaTracks::GetVideoFrame(AVFrame *frame) {

    const auto Settings = GetDefault<UFFMPEGMediaSettings>();

    int got_picture = viddec->DecodeFrame(frame, NULL);

    if (got_picture < 0)
        return -1;

    if ( got_picture ) {
        if ( !bPrerolled) {
            bPrerolled = true;
            SetRate(CurrentRate);
        }

        if (hwaccel_retrieve_data && frame->format == hwAccelPixFmt) {
            int err = hwaccel_retrieve_data(video_ctx, frame);
            if (err < 0) {
                av_frame_unref(frame);
                return -1;
            }
        }

        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(videoStream->time_base) * frame->pts;

        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(FormatContext, videoStream, frame);


        if ((Settings->AllowFrameDrop && getMasterSyncType() != ESynchronizationType::VideoMaster)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - GetMasterClock();
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff /*- frame_last_filter_delay*/ < 0 &&
                    viddec->GetPktSerial() == vidclk.GetSerial() &&
                    videoq.GetNumPackets()) {
                    frameDropsEarly++;
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

int FFFMPEGMediaTracks::VideoThread() {

    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = videoStream->time_base;
    AVRational frame_rate = av_guess_frame_rate(FormatContext, videoStream, NULL);

    if (!frame) {
        return AVERROR(ENOMEM);
    }

    for (;;) {
        ret = GetVideoFrame(frame);
        if (ret < 0) {
            av_frame_free(&frame);
            return 0;
        }
        if (!ret)
            continue;

        duration = (frame_rate.num && frame_rate.den ? av_q2d({ frame_rate.den, frame_rate.num }) : 0);
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        ret = pictq.QueuePicture(frame, pts, duration, frame->pkt_pos, viddec->GetPktSerial());
        
        av_frame_unref(frame);

        if (ret < 0) {
            av_frame_free(&frame);
            return 0;
        }
    }

    av_frame_free(&frame);
    return 0;
}


int FFFMPEGMediaTracks::HWAccelRetrieveDataCallback(AVCodecContext *avctx, AVFrame *input) {
    FFFMPEGMediaTracks *ist = (FFFMPEGMediaTracks*)avctx->opaque;
    AVFrame *output = NULL;
    //enum AVPixelFormat output_format = ist->hwaccel_output_format;
    enum AVPixelFormat output_format = AV_PIX_FMT_NONE;
    int err;

    if (input->format == output_format) {
        // Nothing to do.
        return 0;
    }

    output = av_frame_alloc();
    if (!output)
        return AVERROR(ENOMEM);

    output->format = output_format;

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to transfer data to "
            "output frame: %d.\n", err);
        av_frame_free(&output);
        return err;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0) {
        av_frame_unref(output);
        av_frame_free(&output);
        return err;
    }

    av_frame_unref(input);
    av_frame_move_ref(input, output);
    av_frame_free(&output);

    return 0;

}

int FFFMPEGMediaTracks::IsRealtime(AVFormatContext *s) {
	if (!strcmp(s->iformat->name, "rtp")
		|| !strcmp(s->iformat->name, "rtsp")
		|| !strcmp(s->iformat->name, "sdp")
		)
		return 1;

	if (s->pb && (!strncmp(s->url, "rtp:", 4)
		|| !strncmp(s->url, "udp:", 4)
		)
		)
		return 1;
	return 0;
}



#undef LOCTEXT_NAMESPACE
