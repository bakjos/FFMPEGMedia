// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "FFMPEGMediaPrivate.h"

#include "IMediaCaptureSupport.h"
#include "Modules/ModuleManager.h"

#include "IFFMPEGMediaModule.h"


#include "IMediaModule.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

extern  "C" {
#include "libavformat/avformat.h"

}

#include "FFMPEGMediaPlayer.h"



DEFINE_LOG_CATEGORY(LogFFMPEGMedia);


/**
 * Implements the FFMPEGMedia module.
 */
class FFFMPEGMediaModule
	: /*public IMediaCaptureSupport
	,*/ public IFFMPEGMediaModule
{
public:

	/** Default constructor. */
	FFFMPEGMediaModule()
		: Initialized(false)
	{ }

public:



	//~ IWmfMediaModule interface

	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override
	{

		if (!Initialized)
		{
			return nullptr;
		}

		return MakeShareable(new FFFMPEGMediaPlayer(EventSink));

	}


    virtual TArray<FString> GetSupportedFileExtensions() override {
	    TMap<FString, FString> extensionMap;

        AVOutputFormat *ofmt = av_oformat_next(NULL);
        while (ofmt) {
            FString ext = ofmt->extensions;
            TArray<FString> supportedExts;
            ext.ParseIntoArray(supportedExts, TEXT(","));
            for ( const FString& s: supportedExts) {
                if ( extensionMap.Contains(s)) {
                    extensionMap[s] += TEXT(",") + FString(ofmt->name);
                } else {
                    extensionMap.Add(s, ofmt->name );
                }
            }
            //extensionMap.Add()
            ofmt = av_oformat_next(ofmt);
        }

        TArray<FString> extensions;
	    extensionMap.GetKeys(extensions);
        return extensions;
	}

    virtual TArray<FString> GetSupportedUriSchemes() override {
        void *opaque = NULL;
	    const char *name = avio_enum_protocols(&opaque, 1);
        TArray<FString> protocols;
        while (name) {
            protocols.Add(name);
            name = avio_enum_protocols(&opaque, 1);
        }
        return protocols;
	}

public:

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{

        av_register_all();
        avformat_network_init();
        av_log_set_level(AV_LOG_INFO);

		// register capture device support
		auto MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
            //TODO: Implement Capture support
			//MediaModule->RegisterCaptureSupport(*this);
		} else {
		    UE_LOG(LogFFMPEGMedia, Error, TEXT("Coudn't find the media module"));
		}

		Initialized = true;


	}

	virtual void ShutdownModule() override
	{

		if (!Initialized)
		{
			return;
		}

		// unregister capture support
		auto MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			//MediaModule->UnregisterCaptureSupport(*this);
		}

		
		Initialized = false;


	}

protected:



private:

	/** Whether the module has been initialized. */
	bool Initialized;
};


IMPLEMENT_MODULE(FFFMPEGMediaModule, FFMPEGMedia);
