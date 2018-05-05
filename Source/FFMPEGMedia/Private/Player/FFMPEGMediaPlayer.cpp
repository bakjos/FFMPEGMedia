// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "FFMPEGMediaPlayer.h"


#include "Async/Async.h"
#include "IMediaEventSink.h"
#include "IMediaOptions.h"
#include "Misc/Optional.h"
#include "UObject/Class.h"

#include "FFMPEGMediaTracks.h"
#include "FFMPEGMediaSettings.h"

extern  "C" {
#include "libavformat/avformat.h"
}


#define FF_INPUT_BUFFER_PADDING_SIZE 32

/* FWmfVideoPlayer structors
 *****************************************************************************/

FFFMPEGMediaPlayer::FFFMPEGMediaPlayer(IMediaEventSink& InEventSink)
	:
      EventSink(InEventSink)
	, Tracks(MakeShared<FFFMPEGMediaTracks, ESPMode::ThreadSafe>())
{
	check(Tracks.IsValid());
    
    io_ctx = nullptr;
    ic = nullptr;
    stopped = true;
}


FFFMPEGMediaPlayer::~FFFMPEGMediaPlayer()
{
	Close();
}


/* IMediaPlayer interface
 *****************************************************************************/

void FFFMPEGMediaPlayer::Close()
{
	if (Tracks->GetState() == EMediaState::Closed)
	{
		return;
	}

    
	// reset player
	stopped = true;
	MediaUrl = FString();
	Tracks->Shutdown();

    if (ic) {
        ic->video_codec = NULL;
        ic->audio_codec = NULL;
        avformat_close_input(&ic);
        ic = nullptr;
    }

    if (io_ctx) {
        av_free(io_ctx->buffer);
        av_free(io_ctx);
        io_ctx = nullptr;
    }

	// notify listeners
	EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
	EventSink.ReceiveMediaEvent(EMediaEvent::MediaClosed);
}


IMediaCache& FFFMPEGMediaPlayer::GetCache()
{
	return *this;
}


IMediaControls& FFFMPEGMediaPlayer::GetControls()
{
	return *Tracks;
}


FString FFFMPEGMediaPlayer::GetInfo() const
{
	return Tracks->GetInfo();
}


FName FFFMPEGMediaPlayer::GetPlayerName() const
{
	static FName PlayerName(TEXT("FFMPEGMedia"));
	return PlayerName;
}


IMediaSamples& FFFMPEGMediaPlayer::GetSamples()
{
	return *Tracks;
}


FString FFFMPEGMediaPlayer::GetStats() const
{
	FString Result;
	Tracks->AppendStats(Result);

	return Result;
}


IMediaTracks& FFFMPEGMediaPlayer::GetTracks()
{
	return *Tracks;
}


FString FFFMPEGMediaPlayer::GetUrl() const
{
	return MediaUrl;
}


IMediaView& FFFMPEGMediaPlayer::GetView()
{
	return *this;
}


bool FFFMPEGMediaPlayer::Open(const FString& Url, const IMediaOptions* Options)
{
	Close();

	if (Url.IsEmpty())
	{
		return false;
	}

	const bool Precache = (Options != nullptr) ? Options->GetMediaOption("PrecacheFile", false) : false;

	return InitializePlayer(nullptr, Url, Precache);
}


bool FFFMPEGMediaPlayer::Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* /*Options*/)
{
	Close();

    if (Archive->TotalSize() == 0)
    {
        UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Player %p: Cannot open media from archive (archive is empty)"), this);
        return false;
    }

    if (OriginalUrl.IsEmpty())
    {
        UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Player %p: Cannot open media from archive (no original URL provided)"), this);
        return false;
    }

    return InitializePlayer(Archive, OriginalUrl, false);
}


void FFFMPEGMediaPlayer::TickFetch(FTimespan DeltaTime, FTimespan Timecode)
{
	bool MediaSourceChanged = false;
	bool TrackSelectionChanged = false;

	Tracks->GetFlags(MediaSourceChanged, TrackSelectionChanged);

	if (MediaSourceChanged)
	{
		EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
	}

	if (TrackSelectionChanged)
	{
		/*/// less than windows 10, seem to be a problem switching stream
		if (!FWindowsPlatformMisc::VerifyWindowsVersion(10, 0) / * Anything < Windows 10.0 * /)
		{
			const auto Settings = GetDefault<UFFMPEGMediaSettings>();
			check(Settings != nullptr);

			Session->Initialize(Settings->LowLatency);
			Tracks->ReInitialize();
		}
	
		if (!Tracks->IsInitialized() / *|| !Session->SetTopology(Tracks->CreateTopology(), Tracks->GetDuration())* /)
		{
			Session->Shutdown();
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
		}*/
	}

	if (MediaSourceChanged || TrackSelectionChanged)
	{
		Tracks->ClearFlags();
	}
}


void FFFMPEGMediaPlayer::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
    Tracks->TickInput(DeltaTime, Timecode);
	
    // forward session events
    TArray<EMediaEvent> OutEvents;
    Tracks->GetEvents(OutEvents);

    for (const auto& Event : OutEvents)
    {
        EventSink.ReceiveMediaEvent(Event);
    }

    // process deferred tasks
    TFunction<void()> Task;

    while (PlayerTasks.Dequeue(Task))
    {
        Task();
    }
}



/* FFFMPEGMediaPlayer implementation
 *****************************************************************************/

bool FFFMPEGMediaPlayer::InitializePlayer(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive, const FString& Url, bool Precache)
{
	UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Player %llx: Initializing %s (archive = %s, precache = %s)"), this, *Url, Archive.IsValid() ? TEXT("yes") : TEXT("no"), Precache ? TEXT("yes") : TEXT("no"));

	const auto Settings = GetDefault<UFFMPEGMediaSettings>();
	check(Settings != nullptr);

	
	MediaUrl = Url;


	// initialize presentation on a separate thread
	const EAsyncExecution Execution = Precache ? EAsyncExecution::Thread : EAsyncExecution::ThreadPool;

	Async<void>(Execution, [this, Archive, Url, Precache, TracksPtr = TWeakPtr<FFFMPEGMediaTracks, ESPMode::ThreadSafe>(Tracks)]()
	{
		TSharedPtr<FFFMPEGMediaTracks, ESPMode::ThreadSafe> PinnedTracks = TracksPtr.Pin();

		if (PinnedTracks.IsValid())
		{
			AVFormatContext* context  = ReadContext(Archive, Url, Precache);
            if ( context ) {
 			    PinnedTracks->Initialize(context, Url);
            }
		}
	});

	return true;
}

int FFFMPEGMediaPlayer::decode_interrupt_cb(void *ctx) {
    FFFMPEGMediaPlayer* player = static_cast<FFFMPEGMediaPlayer*>(ctx);
    return player->stopped?1:0;
}

int FFFMPEGMediaPlayer::read_stream(void* opaque, uint8_t* buf, int buf_size) {
    FFFMPEGMediaPlayer* player = static_cast<FFFMPEGMediaPlayer*>(opaque);
    int64 Position =  player->CurrentArchive->Tell();
    int64 Size =  player->CurrentArchive->TotalSize();
    int64 BytesToRead = buf_size;
    if (BytesToRead > (int64)Size)
    {
        BytesToRead = Size;
    }

    if ((Size - BytesToRead) <  player->CurrentArchive->Tell())
    {
        BytesToRead = Size - Position;
    }
    if (BytesToRead > 0)
    {
         player->CurrentArchive->Serialize(buf, BytesToRead);
    }

    player->CurrentArchive->Seek(Position + BytesToRead);

    return BytesToRead;
}

int64_t FFFMPEGMediaPlayer::seek_stream(void *opaque, int64_t offset, int whence) {
    FFFMPEGMediaPlayer* player = static_cast<FFFMPEGMediaPlayer*>(opaque);
    if (whence == AVSEEK_SIZE) {
        return player->CurrentArchive->TotalSize();
    }
    int64_t pos =  player->CurrentArchive->Tell();
    player->CurrentArchive->Seek(pos + offset);
    return player->CurrentArchive->Tell();

}

AVFormatContext* FFFMPEGMediaPlayer::ReadContext(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive, const FString& Url, bool Precache) {
    AVDictionary *format_opts = NULL;
    int scan_all_pmts_set = 0;

    

    ic = avformat_alloc_context();

    stopped = false;

    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = this;

    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    int err = 0;
    if (!Archive.IsValid()) {
        if (Url.StartsWith(TEXT("file://")))
        {
            const TCHAR* FilePath = &Url[7];
            err = avformat_open_input(&ic, TCHAR_TO_UTF8(FilePath), NULL, &format_opts);
        } else {
            err = avformat_open_input(&ic, TCHAR_TO_UTF8(*Url), NULL, &format_opts);
        }
    } else {
        CurrentArchive = Archive;
        const int ioBufferSize = 32768;
        unsigned char * ioBuffer = (unsigned char *)av_malloc(ioBufferSize + FF_INPUT_BUFFER_PADDING_SIZE);
        io_ctx = avio_alloc_context(ioBuffer, ioBufferSize, 0, this, read_stream, NULL, seek_stream);
        ic->pb = io_ctx;
        err = avformat_open_input(&ic, "InMemoryFile", NULL, &format_opts);
    }

    if (err < 0) {
        char errbuf[128];
        const char *errbuf_ptr = errbuf;
#if PLATFORM_WINDOWS
        if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
            strerror_s(errbuf, 128, AVUNERROR(err));
#else
        if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
            errbuf_ptr = strerror(AVUNERROR(err));
#endif
        PlayerTasks.Enqueue([=]() {
            EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
        });

        if ( ic ) {
            avformat_close_input(&ic);
            ic = nullptr;
        }
        stopped = true;
        return ic;
    }

    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    AVDictionaryEntry *t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX);
    if (t) {
        UE_LOG(LogFFMPEGMedia, Error, TEXT("Option %s not found"), UTF8_TO_TCHAR(t->key));
        
        PlayerTasks.Enqueue([=]() {
            EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
        });
        if ( ic ) {
            avformat_close_input(&ic);
            ic = nullptr;
        }
        stopped = true;
        return ic;
    }

    av_dict_free(&format_opts);


    av_format_inject_global_side_data(ic);

    err = avformat_find_stream_info(ic, NULL);

    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    //int64_t duration = ic->duration + (ic->duration <= INT64_MAX - 5000 ? 5000 : 0);
    
    

    return ic;
}
