#include "AbstractAudioDevice.h"



AbstractAudioDevice::AbstractAudioDevice()
{
    nChannels = 2;
    samples = 2048;
    sample_size = 4;
    sampleRate = 48000;
    sampleFormat =  SAMPLE_FMT_S16;
    buffer.resize(samples*sample_size);
}


AbstractAudioDevice::~AbstractAudioDevice()
{
}

void AbstractAudioDevice::setAudioCallBack(AudioCallBack callback) {
    audioCallback = callback;    
}

void AbstractAudioDevice::setConfiguration(int nChannels, int samples, int sampleSize, int sampleRate, SampleFormat sampleFormat) {
    this->nChannels = nChannels;
    this->samples = samples;
    this->sample_size = sampleSize;
    this->sampleRate = sampleRate;
    this->sampleFormat = sampleFormat;
    buffer.resize(samples*sample_size);
}