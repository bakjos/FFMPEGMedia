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
#include "libavutil/opt.h"
#include "libavutil/time.h"
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
    
    IOContext = nullptr;
    FormatContext = nullptr;
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

    if (FormatContext) {
        FormatContext->video_codec = NULL;
        FormatContext->audio_codec = NULL;
        avformat_close_input(&FormatContext);
        FormatContext = nullptr;
    }

    if (IOContext) {
        av_free(IOContext->buffer);
        av_free(IOContext);
        IOContext = nullptr;
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

FGuid FFFMPEGMediaPlayer::GetPlayerPluginGUID() const 
{
    // {938BEEB4-2E88-450E-9C1C-6109456279B3}
    static FGuid PlayerPluginGUID(0x938beeb4, 0x2e88450e, 0x9c1c6109, 0x456279b3);
    return PlayerPluginGUID;
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

bool FFFMPEGMediaPlayer::Open(const FString& Url, const IMediaOptions* Options, const FMediaPlayerOptions* PlayerOptions) {
    Close();

    if (Url.IsEmpty())
    {
        return false;
    }

    const bool Precache = (Options != nullptr) ? Options->GetMediaOption("PrecacheFile", false) : false;

    return InitializePlayer(nullptr, Url, Precache, PlayerOptions);
}


bool FFFMPEGMediaPlayer::Open(const FString& Url, const IMediaOptions* Options)
{
    return Open(Url, Options, nullptr);
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

    return InitializePlayer(Archive, OriginalUrl, false, nullptr);
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

bool FFFMPEGMediaPlayer::InitializePlayer(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive, const FString& Url, bool Precache, const FMediaPlayerOptions* PlayerOptions )
{
	UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Player %llx: Initializing %s (archive = %s, precache = %s)"), this, *Url, Archive.IsValid() ? TEXT("yes") : TEXT("no"), Precache ? TEXT("yes") : TEXT("no"));

	const auto Settings = GetDefault<UFFMPEGMediaSettings>();
	check(Settings != nullptr);

	
	MediaUrl = Url;

	// initialize presentation on a separate thread
	const EAsyncExecution Execution = Precache ? EAsyncExecution::Thread : EAsyncExecution::ThreadPool;
    
    
    TFunction <void()>  Task =  [Archive, Url, Precache, PlayerOptions, TracksPtr = TWeakPtr<FFFMPEGMediaTracks, ESPMode::ThreadSafe>(Tracks), ThisPtr=this]()
    {
        TSharedPtr<FFFMPEGMediaTracks, ESPMode::ThreadSafe> PinnedTracks = TracksPtr.Pin();
        
        if (PinnedTracks.IsValid() )
        {
            AVFormatContext* context = ThisPtr->ReadContext(Archive, Url, Precache);
            if (context) {
                PinnedTracks->Initialize(context, Url, PlayerOptions);
            }
        }
    };
    Async(Execution, Task);
	return true;
}

int FFFMPEGMediaPlayer::DecodeInterruptCallback(void *ctx) {
    FFFMPEGMediaPlayer* player = static_cast<FFFMPEGMediaPlayer*>(ctx);
    return player->stopped?1:0;
}

int FFFMPEGMediaPlayer::ReadtStreamCallback(void* opaque, uint8_t* buf, int buf_size) {
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

int64_t FFFMPEGMediaPlayer::SeekStreamCallback(void *opaque, int64_t offset, int whence) {
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

    

    FormatContext = avformat_alloc_context();

    stopped = false;

    FormatContext->interrupt_callback.callback = DecodeInterruptCallback;
    FormatContext->interrupt_callback.opaque = this;

    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    
    const auto Settings = GetDefault<UFFMPEGMediaSettings>();
    if ( Settings->RtspTransport != ERTSPTransport::Default) {
        switch (Settings->RtspTransport) {
            case ERTSPTransport::Udp:
                av_dict_set(&format_opts, "rtsp_transport", "udp", 0);
                break;
            case ERTSPTransport::Tcp:
                av_dict_set(&format_opts, "rtsp_transport", "tcp", 0);
                break;
                
            case ERTSPTransport::UdpMulticast:
                av_dict_set(&format_opts, "rtsp_transport", "udp_multicast", 0);
                break;
            case ERTSPTransport::Http:
                av_dict_set(&format_opts, "rtsp_transport", "http", 0);
                break;
            case ERTSPTransport::Https:
                av_dict_set(&format_opts, "rtsp_transport", "https", 0);
                break;
            default:
                break;
        }
        
    }
    
    if ( Settings->ZeroLatencyStreaming) {
        av_dict_set(&format_opts, "fflags", "nobuffer", 0);
    }
    
    int err = 0;
    if (!Archive.IsValid()) {
        if (Url.StartsWith(TEXT("file://")))
        {
            const TCHAR* FilePath = &Url[7];
            err = avformat_open_input(&FormatContext, TCHAR_TO_UTF8(FilePath), NULL, &format_opts);
        } else {
            err = avformat_open_input(&FormatContext, TCHAR_TO_UTF8(*Url), NULL, &format_opts);
        }
    } else {
        CurrentArchive = Archive;
        const int ioBufferSize = 32768;
        unsigned char * ioBuffer = (unsigned char *)av_malloc(ioBufferSize + FF_INPUT_BUFFER_PADDING_SIZE);
        IOContext = avio_alloc_context(ioBuffer, ioBufferSize, 0, this, ReadtStreamCallback, NULL, SeekStreamCallback);
        FormatContext->pb = IOContext;
        err = avformat_open_input(&FormatContext, "InMemoryFile", NULL, &format_opts);
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

        if ( FormatContext ) {
            avformat_close_input(&FormatContext);
            FormatContext = nullptr;
        }
        stopped = true;
        return FormatContext;
    }

    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    AVDictionaryEntry *t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX);
    if (t) {
        UE_LOG(LogFFMPEGMedia, Error, TEXT("Option %s not found"), UTF8_TO_TCHAR(t->key));
        
        PlayerTasks.Enqueue([=]() {
            EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
        });
        if ( FormatContext ) {
            avformat_close_input(&FormatContext);
            FormatContext = nullptr;
        }
        stopped = true;
        return FormatContext;
    }

    av_dict_free(&format_opts);


    av_format_inject_global_side_data(FormatContext);

    err = avformat_find_stream_info(FormatContext, NULL);

    if (FormatContext->pb)
        FormatContext->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end
    
#ifdef UE_BUILD_DEBUG
    dumpFFMPEGInfo();
#endif

    return FormatContext;
}

static void print_option(const AVClass *clazz, const AVOption *o) {
       FString type;
       switch (o->type) {
       case AV_OPT_TYPE_BINARY:   type = TEXT("hexadecimal string"); break;
       case AV_OPT_TYPE_STRING:   type = TEXT("string");             break;
       case AV_OPT_TYPE_INT:
       case AV_OPT_TYPE_INT64:    type = TEXT("integer");            break;
       case AV_OPT_TYPE_FLOAT:
       case AV_OPT_TYPE_DOUBLE:   type = TEXT("float");              break;
       case AV_OPT_TYPE_RATIONAL: type = TEXT("rational number");    break;
       case AV_OPT_TYPE_FLAGS:    type = TEXT("flags");              break;
       case AV_OPT_TYPE_BOOL:     type = TEXT("bool");              break;
       case AV_OPT_TYPE_SAMPLE_FMT:type = TEXT("SampleFmt");              break;
       default:                   type = TEXT("value");              break;
       }
   
       FString flags;


       if (o->flags & AV_OPT_FLAG_ENCODING_PARAM) {
           flags = TEXT("input");
           if (o->flags & AV_OPT_FLAG_ENCODING_PARAM)
               flags += TEXT("/");
       }
       if (o->flags & AV_OPT_FLAG_ENCODING_PARAM)
           flags += TEXT("output");

   
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
               }
               u = av_opt_next(&clazz, u);
           }
       
       }
       if ( o->unit) {
           UE_LOG(LogFFMPEGMedia, Display, TEXT("\t%s - %s: %s %s"), *type, UTF8_TO_TCHAR(o->name), *help,  *possibleValues);
       } else {
          UE_LOG(LogFFMPEGMedia, Display, TEXT( "\t%s - %s: %s"), *type, UTF8_TO_TCHAR(o->name), *help);
       }
  
   }


void FFFMPEGMediaPlayer::dumpOptions(const AVClass *clazz) {
   const AVOption *o = av_opt_next(&clazz, NULL);

   while (o != NULL) {
       if (o->type != AV_OPT_TYPE_CONST) {
           print_option(clazz, o);
       }
       o = av_opt_next(clazz, o);
   }
}



void FFFMPEGMediaPlayer::dumpFFMPEGInfo() {
  
   UE_LOG(LogFFMPEGMedia, Display, TEXT("AVFormat configuration: %s"), UTF8_TO_TCHAR(avformat_configuration()));
   
   if ( FormatContext ) {
       if(FormatContext->iformat)
           UE_LOG(LogFFMPEGMedia, Display, TEXT("Format Name: %s"),  UTF8_TO_TCHAR(FormatContext->iformat->name));

       FString sz_duration = TEXT("  Duration: ");
       if (FormatContext->duration != AV_NOPTS_VALUE) {
           int hours, mins, secs, us;
           int64_t duration = FormatContext->duration + (FormatContext->duration <= INT64_MAX - 5000 ? 5000 : 0);
           secs = duration / AV_TIME_BASE;
           us = duration % AV_TIME_BASE;
           mins = secs / 60;
           secs %= 60;
           hours = mins / 60;
           mins %= 60;
           sz_duration += FString::Printf(TEXT("%02d:%02d:%02d.%02d"), hours, mins, secs,(100 * us) / AV_TIME_BASE);
       }
       else {
           sz_duration += TEXT("N/A");
       }

       if (FormatContext->start_time != AV_NOPTS_VALUE) {
           int secs, us;
           sz_duration +=  ", start: ";
           secs = llabs(FormatContext->start_time / AV_TIME_BASE);
           us = llabs(FormatContext->start_time % AV_TIME_BASE);
           sz_duration += FString::Printf(TEXT("%s%d.%06d"),FormatContext->start_time >= 0 ? TEXT("") : TEXT("-"),secs,(int)av_rescale(us, 1000000, AV_TIME_BASE));
       }
       sz_duration += TEXT(", bitrate: ");
       if (FormatContext->bit_rate)
       {
           int64_t br = FormatContext->bit_rate / 1000;
           sz_duration += FString::Printf(TEXT("0x%08x%08x kb/s"), (uint32_t)(br >> 32),(uint32_t)(br & 0xFFFFFFFF));
       }

       else
           sz_duration +=  TEXT("N/A");
       
       sz_duration +=  TEXT("\n");
   

       for (unsigned int i = 0; i < FormatContext->nb_chapters; i++) {
           AVChapter *ch = FormatContext->chapters[i];
           sz_duration += FString::Printf(TEXT("    Chapter %d: "), i);
           sz_duration += FString::Printf(TEXT("start %f, "), ch->start * av_q2d(ch->time_base));
           sz_duration += FString::Printf(TEXT("end %f\n"), ch->end * av_q2d(ch->time_base));
       }

       if (FormatContext->nb_programs) {
           unsigned int  total = 0;
           for (unsigned int j = 0; j < FormatContext->nb_programs; j++) {
               AVDictionaryEntry *name = av_dict_get(FormatContext->programs[j]->metadata,
                   "name", NULL, 0);
               sz_duration += FString::Printf(TEXT("  Program %d %s\n"), FormatContext->programs[j]->id,
                   name ? name->value : "");
               
               total += FormatContext->programs[j]->nb_stream_indexes;
           }
           if (total < FormatContext->nb_streams)
               sz_duration += TEXT("  No Program\n");
       }


       UE_LOG(LogFFMPEGMedia, Display, TEXT("%s"), *sz_duration);
       
//       UE_LOG(LogFFMPEGMedia, Display, TEXT("\n\nDefault format options"));
//       dumpOptions(avformat_get_class());
//
//       if (FormatContext->iformat && FormatContext->iformat->priv_class) {
//           UE_LOG(LogFFMPEGMedia, Display, TEXT("\n\nFormat Options\n"));
//           dumpOptions(FormatContext->iformat->priv_class);
//       }
//
//       UE_LOG(LogFFMPEGMedia, Display, TEXT("\n\nDefault codec options\n"));
//       dumpOptions(avcodec_get_class());
//
//       if ( FormatContext->video_codec && FormatContext->video_codec->priv_class ) {
//           UE_LOG(LogFFMPEGMedia, Display, TEXT("\n\nVideo Codec\n"));
//           dumpOptions( FormatContext->video_codec->priv_class);
//       }
//
//       if (FormatContext->audio_codec && FormatContext->audio_codec->priv_class) {
//           UE_LOG(LogFFMPEGMedia, Display, TEXT("\n\nAudio Codec\n"));
//           dumpOptions(FormatContext->audio_codec->priv_class);
//       }

   }
}
