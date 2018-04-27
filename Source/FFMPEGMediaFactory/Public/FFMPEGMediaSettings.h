// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "FFMPEGMediaSettings.generated.h"


UENUM()
enum class ESynchronizationType : uint8 {
    AudioMaster = 0,
    VideoMaster,
    ExternalClock
};

/**
 * Settings for the WmfMedia plug-in.
 */
UCLASS(config=Engine)
class FFMPEGMEDIAFACTORY_API UFFMPEGMediaSettings
	: public UObject
{
	GENERATED_BODY()

public:
	 
	/** Default constructor. */
	UFFMPEGMediaSettings();
   

public:

	
	UPROPERTY(config, EditAnywhere, Category=Media)
	bool UseInfiniteBuffer;

    UPROPERTY(config, EditAnywhere, Category = Media)
    bool AllowFrameDrop;

    UPROPERTY(config, EditAnywhere, Category = Media)
    bool UseHardwareAcceleratedCodecs;

	UPROPERTY(config, EditAnywhere, Category = Media)
	ESynchronizationType SyncType;
};
