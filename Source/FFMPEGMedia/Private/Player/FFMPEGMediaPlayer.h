// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FFMPEGMediaPrivate.h"



#include "Containers/UnrealString.h"
#include "Containers/Queue.h"
#include "IMediaCache.h"
#include "IMediaPlayer.h"
#include "IMediaView.h"
#include "Misc/Timespan.h"


class FFFMPEGMediaTracks;
class IMediaEventSink;


struct AVIOContext;
struct AVFormatContext;
struct AVClass;

/**
 * Implements a media player using the Windows Media Foundation framework.
 */
class FFFMPEGMediaPlayer
	: public IMediaPlayer
	, protected IMediaCache
    , protected IMediaView
    
{
public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param InEventSink The object that receives media events from this player.
	 */
	FFFMPEGMediaPlayer(IMediaEventSink& InEventSink);

	/** Virtual destructor. */
	virtual ~FFFMPEGMediaPlayer();

public:

	//~ IMediaPlayer interface

	virtual void Close() override;
	virtual IMediaCache& GetCache() override;
	virtual IMediaControls& GetControls() override;
	virtual FString GetInfo() const override;
	virtual FName GetPlayerName() const;

	virtual FGuid GetPlayerPluginGUID() const override;
	virtual IMediaSamples& GetSamples() override;
	virtual FString GetStats() const override;
	virtual IMediaTracks& GetTracks() override;
	virtual FString GetUrl() const override;
	virtual IMediaView& GetView() override;
    virtual bool Open(const FString& Url, const IMediaOptions* Options, const FMediaPlayerOptions* PlayerOptions) override;
	virtual bool Open(const FString& Url, const IMediaOptions* Options) override;
	virtual bool Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options) override;
	virtual void TickFetch(FTimespan DeltaTime, FTimespan Timecode) override;
	virtual void TickInput(FTimespan DeltaTime, FTimespan Timecode) override;



protected:

	/**
	 * Initialize the native AvPlayer instance.
	 *
	 * @param Archive The archive being used as a media source (optional).
	 * @param Url The media URL being opened.
	 * @param Precache Whether to precache media into RAM if InURL is a local file.
	 * @return true on success, false otherwise.
	 */
	bool InitializePlayer(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive, const FString& Url, bool Precache, const FMediaPlayerOptions* PlayerOptions);

    

private:

    
	/** The media event handler. */
	IMediaEventSink& EventSink;

	/** The URL of the currently opened media. */
	FString MediaUrl;

    /** Tasks to be executed on the player thread. */
    TQueue<TFunction<void()>> PlayerTasks;

	
	/** Media streams collection. */
	TSharedPtr<FFFMPEGMediaTracks, ESPMode::ThreadSafe> Tracks;

    /** FFMPEG Callbacks */
    /** Returns 1 when we would like to stop the application */
    static int DecodeInterruptCallback(void *ctx);

    /** This is called when it's reading an Archive instead of an url*/
    static int ReadtStreamCallback(void* ptr, uint8_t* buf, int buf_size);

    /** This is called when it's reading an Archive instead of an url*/
    static int64_t SeekStreamCallback(void *opaque, int64_t offset, int whence);

    /** FFMPEG Functions */

    AVFormatContext*  ReadContext(const TSharedPtr<FArchive, ESPMode::ThreadSafe>& Archive, const FString& Url, bool Precache);
    
    static void dumpOptions(const AVClass *clazz);
    
    void dumpFFMPEGInfo();

    /** FFMPEG Structs */
    AVFormatContext     *FormatContext;
    AVIOContext         *IOContext;        
    bool                 stopped;

    TSharedPtr<FArchive, ESPMode::ThreadSafe> CurrentArchive;
    

};



