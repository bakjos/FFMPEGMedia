// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "FFMPEGMediaPrivate.h"

#include "IMediaCaptureSupport.h"
#include "Modules/ModuleManager.h"

#include "IFFMPEGMediaModule.h"
#include "Core.h"
#include "Interfaces/IPluginManager.h"

#include "IMediaModule.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

#if PLATFORM_ANDROID
    #include "Android/AndroidJNI.h"
#endif

extern  "C" {
#include "libavformat/avformat.h"

#if PLATFORM_ANDROID
#include "libavcodec/jni.h"
#endif

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
		: Initialized(false) {
        AVDeviceLibrary = nullptr;
        AVFilterLibrary = nullptr;
        PostProcLibrary = nullptr;
        SWScaleLibrary = nullptr;
        AVFormatLibrary = nullptr;
        AVCodecLibrary = nullptr;
        SWResampleLibrary = nullptr;
        AVUtilLibrary = nullptr;
	}

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

        void *ofmt_opaque = NULL;

        const AVOutputFormat *ofmt = av_muxer_iterate(&ofmt_opaque);
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
            ofmt = av_muxer_iterate(&ofmt_opaque);
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

    static void  log_callback(void*, int level , const char* format, va_list arglist ) {

        char buffer[2048];
#if PLATFORM_WINDOWS
        vsprintf_s(buffer, 2048, format, arglist);
#else
        vsnprintf(buffer, 2048, format, arglist);
#endif
        FString str = TEXT("FFMPEG - ");
        str += buffer;

        switch (level) {
        case AV_LOG_TRACE:
            UE_LOG(LogFFMPEGMedia, VeryVerbose, TEXT("%s"), *str);
            break;
        case AV_LOG_DEBUG:
            UE_LOG(LogFFMPEGMedia, VeryVerbose,  TEXT("%s"), *str );
            break;
        case AV_LOG_VERBOSE:
            UE_LOG(LogFFMPEGMedia, Verbose,  TEXT("%s"), *str );
            break;
        case AV_LOG_INFO:
            UE_LOG(LogFFMPEGMedia, Display,  TEXT("%s"), *str );
            break;
        case AV_LOG_WARNING:
            UE_LOG(LogFFMPEGMedia, Warning,  TEXT("%s"), *str );
            break;
        case AV_LOG_ERROR:
            UE_LOG(LogFFMPEGMedia, Error,  TEXT("%s"), *str );
            break;
        case AV_LOG_FATAL:
            UE_LOG(LogFFMPEGMedia, Fatal,  TEXT("%s"), *str );
            break;
        default:
            UE_LOG(LogFFMPEGMedia, Display,  TEXT("%s"), *str );
        }


    }

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
#if PLATFORM_ANDROID
        UE_LOG(LogFFMPEGMedia, Verbose, TEXT("Avoid load the libraries once again on android"));
#else
        AVUtilLibrary = LoadLibrary(TEXT("avutil"), TEXT("56"));
        SWResampleLibrary = LoadLibrary(TEXT("swresample"), TEXT("3"));
        AVCodecLibrary = LoadLibrary(TEXT("avcodec"), TEXT("58"));
        AVFormatLibrary = LoadLibrary(TEXT("avformat"), TEXT("58"));
        SWScaleLibrary = LoadLibrary(TEXT("swscale"), TEXT("5"));
        PostProcLibrary = LoadLibrary(TEXT("postproc"), TEXT("55"));
        AVFilterLibrary = LoadLibrary(TEXT("avfilter"), TEXT("7"));
        AVDeviceLibrary = LoadLibrary(TEXT("avdevice"), TEXT("58"));
#endif

        #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
            av_register_all();
        #endif

        avformat_network_init();
        av_log_set_level(AV_LOG_INFO);

        av_log_set_callback(&log_callback);

        UE_LOG(LogFFMPEGMedia, Display, TEXT("FFmpeg AVCodec version: %d.%d.%d"), LIBAVFORMAT_VERSION_MAJOR, LIBAVFORMAT_VERSION_MINOR, LIBAVFORMAT_VERSION_MICRO);
        UE_LOG(LogFFMPEGMedia, Display, TEXT("FFmpeg license: %s"), UTF8_TO_TCHAR(avformat_license()));


		// register capture device support
		auto MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
            //TODO: Implement Capture support
			//MediaModule->RegisterCaptureSupport(*this);
		} else {
		    UE_LOG(LogFFMPEGMedia, Error, TEXT("Coudn't find the media module"));
		}

#if PLATFORM_ANDROID
        // hopefully this is early enough; we don't have a way add into JNI_OnLoad in AndroidJNI.cpp

        if (GJavaVM == NULL) {
            UE_LOG(LogFFMPEGMedia, Error,  TEXT("The global vm hasn't been initialized"));
        } else {
            UE_LOG(LogFFMPEGMedia, Display,  TEXT("Initialize virtual machine for FFMPEG"));
            av_jni_set_java_vm(GJavaVM, NULL);
        }
#endif

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

        if (AVDeviceLibrary) FPlatformProcess::FreeDllHandle(AVDeviceLibrary);
        if (AVFilterLibrary) FPlatformProcess::FreeDllHandle(AVFilterLibrary);
        if (PostProcLibrary) FPlatformProcess::FreeDllHandle(PostProcLibrary);
        if (SWScaleLibrary) FPlatformProcess::FreeDllHandle(SWScaleLibrary);
        if (AVFormatLibrary) FPlatformProcess::FreeDllHandle(AVFormatLibrary);
        if (AVCodecLibrary) FPlatformProcess::FreeDllHandle(AVCodecLibrary);
        if (SWResampleLibrary) FPlatformProcess::FreeDllHandle(SWResampleLibrary);
        if (AVUtilLibrary) FPlatformProcess::FreeDllHandle(AVUtilLibrary);

		Initialized = false;


	}

protected:
    void* LoadLibrary(const  FString& name, const FString& version) {

        FString BaseDir = IPluginManager::Get().FindPlugin("FFMPEGMedia")->GetBaseDir();

        FString LibDir;
        FString extension;
        FString prefix;
        FString separator;
#if PLATFORM_MAC
        LibDir = FPaths::Combine(*BaseDir, TEXT("ThirdParty/ffmpeg/lib/osx"));
        extension = TEXT(".dylib");
        prefix = "lib";
        separator = ".";
#elif PLATFORM_WINDOWS
        extension = TEXT(".dll");
        prefix = "";
        separator = "-";
#if PLATFORM_64BITS
        LibDir = FPaths::Combine(*BaseDir, TEXT("ThirdParty/ffmpeg/bin/vs/x64"));
#else
        LibDir = FPaths::Combine(*BaseDir, TEXT("ThirdParty/ffmpeg/bin/vs/win32"));
#endif
#endif
        if (!LibDir.IsEmpty()) {
            FString LibraryPath = FPaths::Combine(*LibDir, prefix + name + separator + version + extension);
            return FPlatformProcess::GetDllHandle(*LibraryPath);
        }
        return nullptr;
    }


private:

    void* AVUtilLibrary;
    void* SWResampleLibrary;
    void* AVCodecLibrary;
    void* SWScaleLibrary;
    void* AVFormatLibrary;
    void* PostProcLibrary;
    void* AVFilterLibrary;
    void* AVDeviceLibrary;

	/** Whether the module has been initialized. */
	bool Initialized;
};


IMPLEMENT_MODULE(FFFMPEGMediaModule, FFMPEGMedia);
