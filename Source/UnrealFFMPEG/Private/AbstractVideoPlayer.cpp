// Fill out your copyright notice in the Description page of Project Settings.

#include "AbstractVideoPlayer.h"
#include <Async.h>
#include <Public/RHIUtilities.h>

DEFINE_LOG_CATEGORY(LogVideoPlayer)

AbstractVideoPlayer::AbstractVideoPlayer()
{
    _running=false;
    runnableThread = NULL;
    bAudioEnabled=true;
    state = LOOP_NONE;

}

AbstractVideoPlayer::~AbstractVideoPlayer()
{
}



float AbstractVideoPlayer::getPosition() {

  UE_LOG(LogVideoPlayer, Warning, TEXT("AbstractVideoPlayer::getPosition not implemented"));
  return 0.0;
}

//---------------------------------------------------------------------------
float AbstractVideoPlayer::getSpeed() {
  UE_LOG(LogVideoPlayer, Warning, TEXT("AbstractVideoPlayer::getSpeed not implemented"));
  return 0.0;
}

//---------------------------------------------------------------------------
float AbstractVideoPlayer::getDuration() {
  UE_LOG(LogVideoPlayer, Warning, TEXT("AbstractVideoPlayer::getDuration not implemented"));
  return 0.0;
}

float AbstractVideoPlayer::getVolume() {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::getVolume not implemented"));
  return 0.0;
}

//---------------------------------------------------------------------------
bool AbstractVideoPlayer::getIsMovieDone() {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::getIsMovieDone not implemented"));
  return false;
}

//---------------------------------------------------------------------------
void AbstractVideoPlayer::setPaused(bool bPause) {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::setPaused not implemented"));
}

//---------------------------------------------------------------------------
void AbstractVideoPlayer::setPosition(float pct) {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::setPosition not implemented"));
}

//---------------------------------------------------------------------------
void AbstractVideoPlayer::setVolume(float volume) {
  UE_LOG(LogVideoPlayer, Warning, TEXT("AbstractVideoPlayer::setVolume not implemented"));
}

//---------------------------------------------------------------------------
void AbstractVideoPlayer::setLoopState(LoopType state) {
 UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::setLoopState not implemented"));
}

//---------------------------------------------------------------------------
void   AbstractVideoPlayer::setSpeed(float speed) {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::setSpeed not implemented"));
}

//---------------------------------------------------------------------------
void AbstractVideoPlayer::setFrame(int frame) {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::setFrame not implemented"));
}

//---------------------------------------------------------------------------
int	AbstractVideoPlayer::getCurrentFrame() {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::nextFrame not implemented"));
  return 0;
}

//---------------------------------------------------------------------------
int	AbstractVideoPlayer::getTotalNumFrames() {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::getTotalNumFrames not implemented"));
  return 0;
}

//---------------------------------------------------------------------------
int	AbstractVideoPlayer::getLoopState() {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::getLoopState not implemented"));
  return 0;
}

//---------------------------------------------------------------------------
void AbstractVideoPlayer::firstFrame() {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::firstFrame not implemented"));
}

//---------------------------------------------------------------------------
void AbstractVideoPlayer::nextFrame() {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::nextFrame not implemented"));
}

//---------------------------------------------------------------------------
void AbstractVideoPlayer::previousFrame() {
  UE_LOG(LogVideoPlayer, Warning, TEXT( "AbstractVideoPlayer::previousFrame not implemented"));
}

//---------------------------------------------------------------------------
uint32 AbstractVideoPlayer::Run() {
  while (_running)
  {
    update();
    FPlatformProcess::Sleep(0.016);
  }

  return 0;
}

void AbstractVideoPlayer::startThread() {
  stopThread();
  _running = true;
  runnableThread = FRunnableThread::Create(this,  *getThreadName());
}

void AbstractVideoPlayer::stopThread() {

  if (_running && runnableThread) {
    _running = false;
    runnableThread->Kill();
    runnableThread = NULL;
  }
}

void  AbstractVideoPlayer::setAudioDevice(AbstractAudioDevice* audioDevice) {
    this->audioDevice = MakeShareable(audioDevice);
}

void AbstractVideoPlayer::setEnableAudio(bool b) {
    bAudioEnabled = b;
}

bool AbstractVideoPlayer::isAudioEnabled() {
    return bAudioEnabled;
}

void AbstractVideoPlayer::resizeTexture(int w, int h, EPixelFormat InFormat) {
    
    FEvent* fSemaphore = FGenericPlatformProcess::GetSynchEventFromPool(false);
    AsyncTask(ENamedThreads::GameThread, [this, w, h, fSemaphore, InFormat]() {
        if (!videoTexture.IsValid()) {
            videoTexture = UTexture2D::CreateTransient(w, h, InFormat);
            videoTexture->UpdateResource();
        } else {
            videoTexture->ReleaseResource();

            int32 NumBlocksX = w / GPixelFormats[InFormat].BlockSizeX;
            int32 NumBlocksY = h / GPixelFormats[InFormat].BlockSizeY;
            FTexture2DMipMap& Mip = videoTexture->PlatformData->Mips[0];
            Mip.SizeX = w;
            Mip.SizeY = h;
            Mip.BulkData.Lock(LOCK_READ_WRITE);
            Mip.BulkData.Realloc(NumBlocksX * NumBlocksY * GPixelFormats[InFormat].BlockBytes);
            Mip.BulkData.Unlock();

            videoTexture->UpdateResource();

        }
        fSemaphore->Trigger();
    });
    fSemaphore->Wait();
    FGenericPlatformProcess::ReturnSynchEventToPool(fSemaphore);
}

void AbstractVideoPlayer::allocateTexture(int w, int h, EPixelFormat InFormat) {
    FEvent* fSemaphore = FGenericPlatformProcess::GetSynchEventFromPool(false);
    AsyncTask(ENamedThreads::GameThread, [this, w, h, fSemaphore, InFormat]() {
        videoTexture = UTexture2D::CreateTransient(w, h, InFormat);
        videoTexture->UpdateResource();
    });
    fSemaphore->Wait();
    FGenericPlatformProcess::ReturnSynchEventToPool(fSemaphore);
}

void AbstractVideoPlayer::copyDataToTexture(unsigned char * pData, int TextureWidth, int TextureHeight, int pitch, int numColors) {
    if ( videoTexture.IsValid() && videoTexture->Resource) {
        FUpdateTextureRegion2D region(0, 0, 0, 0, TextureWidth, TextureHeight);
        FEvent* fSemaphore = FGenericPlatformProcess::GetSynchEventFromPool(false);
        videoTexture->UpdateTextureRegions(0, 1, &region, pitch, numColors,pData, [fSemaphore](uint8* SrcData, const FUpdateTextureRegion2D* Regions ) {
            fSemaphore->Trigger();
        });
        fSemaphore->Wait();
        FGenericPlatformProcess::ReturnSynchEventToPool(fSemaphore);
    }
}


UTexture* AbstractVideoPlayer::getTexture() {
    return videoTexture.Get();
}