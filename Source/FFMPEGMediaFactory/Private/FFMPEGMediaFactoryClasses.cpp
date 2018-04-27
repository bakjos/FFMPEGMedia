// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "FFMPEGMediaSettings.h"


UFFMPEGMediaSettings::UFFMPEGMediaSettings()
    : UseInfiniteBuffer (false)
    , UseHardwareAcceleratedCodecs (true)
    , AllowFrameDrop (false)
    , SyncType  (ESynchronizationType::AudioMaster)
{ }
