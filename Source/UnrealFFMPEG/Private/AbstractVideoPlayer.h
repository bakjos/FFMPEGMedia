// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <Engine/Texture2D.h>
#include <functional>
#include "Runnable.h"
#include "RunnableThread.h"
#include "AbstractAudioDevice.h"
#include <mutex>

enum LoopType {
  LOOP_NONE = 0x01,
  LOOP_PALINDROME = 0x02,
  LOOP_NORMAL = 0x03
};

DECLARE_LOG_CATEGORY_EXTERN(LogVideoPlayer,Log, All)


/**
 * 
 */
class AbstractVideoPlayer: protected FRunnable 
{
  
      typedef std::function<void(AbstractVideoPlayer* eventCode, const FString& fileName)> VideoEventCallBack;
      typedef std::function<void(AbstractVideoPlayer* eventCode, const FString& fileName, int frame)> VideoFrameCallBack;

public:
	AbstractVideoPlayer();
	virtual ~AbstractVideoPlayer();

        virtual FString	                       getName() = 0;
        virtual FString                        getThreadName()=0;
        virtual void                           setFrameByFrame(bool val) = 0;
        virtual bool                           isFrameByFrame() = 0;
        virtual bool                           hasAudio() = 0;

        virtual bool                            loadMovie(const FString& name) = 0;
        virtual void                            close() = 0;
        virtual void                            update() = 0;

        virtual void                            play() = 0;
        virtual void                            stop() = 0;

        virtual bool                            isFrameNew() = 0;

        virtual bool                            isPaused() = 0;
        virtual bool                            isLoaded() = 0;
        virtual bool                            isPlaying() = 0;
        virtual bool                            isImage() = 0;

        //should imple                          
        virtual float                           getPosition();
        virtual float                           getSpeed();
        virtual float                           getDuration();
        virtual bool                            getIsMovieDone();
        virtual float                           getVolume();

        virtual void                            setPaused(bool bPause);
        virtual void                            setPosition(float pct);
        virtual void                            setVolume(float volume);
        virtual void                            setLoopState(LoopType state);
        virtual void                            setSpeed(float speed);
        virtual void                            setFrame(int frame);  // frame 0 = first frame...

        virtual int	                             getCurrentFrame();
        virtual int	                             getTotalNumFrames();
        virtual int	                             getLoopState();

        virtual void                            firstFrame();
        virtual void                            nextFrame();
        virtual void                            previousFrame();    

        virtual void                            setAudioDevice(AbstractAudioDevice* audioDevice);

        virtual void                            setEnableAudio(bool b);
        virtual bool                            isAudioEnabled();

        virtual UTexture*                       getTexture();

protected:
        uint32                                  Run();
        void                                    startThread();
        void                                    stopThread();
        void                                    resizeTexture(int w, int h, EPixelFormat InFormat = PF_R8G8B8A8);
        void                                    allocateTexture(int w, int h, EPixelFormat InFormat = PF_R8G8B8A8);
        void                                    copyDataToTexture(unsigned char * pData, int TextureWidth, int TextureHeight, int pitch,  int numColors);
        
        TWeakObjectPtr<UTexture2D>              videoTexture;
        TSharedPtr<AbstractAudioDevice>         audioDevice;
        VideoFrameCallBack                      videoFrameCallback;
        VideoEventCallBack                      videoStartedCallback;
        VideoEventCallBack                      videoFinishedCallback;
        bool                                    _running;
        FRunnableThread*                        runnableThread;
        bool                                    bAudioEnabled;
        LoopType                                state;
};
