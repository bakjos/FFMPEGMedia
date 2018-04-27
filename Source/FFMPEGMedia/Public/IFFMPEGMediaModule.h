// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Modules/ModuleInterface.h"

class IMediaEventSink;
class IMediaPlayer;


/**
 * Interface for the WmfMedia module.
 */
class IFFMPEGMediaModule
	: public IModuleInterface
{
public:

	/**
	 * Creates a Windows Media Foundation based media player.
	 *
	 * @param EventSink The object that receives media events from the player.
	 * @return A new media player, or nullptr if a player couldn't be created.
	 */
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) = 0;

    virtual TArray<FString> GetSupportedFileExtensions() = 0;

    virtual TArray<FString> GetSupportedUriSchemes() = 0;

public:

	/** Virtual destructor. */
	virtual ~IFFMPEGMediaModule() { }
};
