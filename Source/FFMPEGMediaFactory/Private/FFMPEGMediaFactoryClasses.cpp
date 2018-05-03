// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "FFMPEGMediaSettings.h"


UFFMPEGMediaSettings::UFFMPEGMediaSettings()
    : UseInfiniteBuffer (false)
    , UseHardwareAcceleratedCodecs (true)
    , AllowFrameDrop (true)
    , DisableAudio (false)
    , SpeedUpTricks (false)
    , AudioThreads(0)
    , VideoThreads(0)
    , SyncType  (ESynchronizationType::AudioMaster)
{ }
