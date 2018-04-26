#pragma once

#include <functional>
#include <vector>


enum SampleFormat {
    SAMPLE_FMT_NONE = -1,
    SAMPLE_FMT_U8,          ///< unsigned 8 bits
    SAMPLE_FMT_S16,         ///< signed 16 bits
    SAMPLE_FMT_S32,         ///< signed 32 bits
    SAMPLE_FMT_FLT,         ///< float
    SAMPLE_FMT_DBL,         ///< double

    SAMPLE_FMT_U8P,         ///< unsigned 8 bits, planar
    SAMPLE_FMT_S16P,        ///< signed 16 bits, planar
    SAMPLE_FMT_S32P,        ///< signed 32 bits, planar
    SAMPLE_FMT_FLTP,        ///< float, planar
    SAMPLE_FMT_DBLP,        ///< double, planar
    SAMPLE_FMT_S64,         ///< signed 64 bits
    SAMPLE_FMT_S64P,        ///< signed 64 bits, planar

    SAMPLE_FMT_NB           ///< Number of sample formats. DO NOT USE if linking dynamically
};


class AbstractAudioDevice
{
public:
    typedef std::function<void(unsigned char *stream, int len)> AudioCallBack;
    AbstractAudioDevice();
    virtual ~AbstractAudioDevice();
    virtual bool openAudioDevice(const std::string& name = "") = 0;
    virtual void close() = 0;
    virtual float getVolume() = 0;
    virtual void setVolume(float volume) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void setConfiguration(int nChannels, int samples, int sampleSize, int sampleRate, SampleFormat sampleFormat);    
    virtual void setAudioCallBack(AudioCallBack callback);
protected:
    AudioCallBack audioCallback;
    
    int samples;
    int sample_size;
    int	sampleRate;
    int nChannels;
    SampleFormat	sampleFormat;
    std::vector<unsigned char> buffer;


};

