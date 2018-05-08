#include "FFMPEGClock.h"
#include "FFMPEGPacketQueue.h"

extern "C" {
#include "libavutil/time.h"
}

/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0


FFMPEGClock::FFMPEGClock()
{
    pts = 0.0;           
    pts_drift = 0.0;     
    last_updated = 0.0;
    speed = 0.0;
    serial = 0;           
    paused = false;
    queue_serial = NULL;
}


FFMPEGClock::~FFMPEGClock()
{
}

void FFMPEGClock::Init(FFMPEGPacketQueue* queue) {
    speed = 1.0;
    paused = false;
    this->queue_serial = &queue->serial;
    Set(NAN, -1);
}

void FFMPEGClock::Init(FFMPEGClock* clock) {
    speed = 1.0;
    paused = 0;
    this->queue_serial = &clock->serial;
    Set(NAN, -1);
}

double FFMPEGClock::Get() {
    if ( queue_serial == NULL || *queue_serial != serial)
        return NAN;
    if (paused) {
        return pts;
    }
    else {
        double time = av_gettime_relative() / 1000000.0;
        return pts_drift + time - (time - last_updated) * (1.0 - speed);
    }
}

void FFMPEGClock::Set(double pts, int serial) {
    double time = av_gettime_relative() / 1000000.0;
    SetAt(pts, serial, time);
}

void  FFMPEGClock::SetAt( double pts, int serial, double time) {
    this->pts = pts;
    this->last_updated = time;
    this->pts_drift = this->pts - time;
    this->serial = serial;
}

void FFMPEGClock::SetPaused(bool p) {
    paused = p;
}

void FFMPEGClock::SetSpeed(double speed) {
    Set(Get(), serial);
    this->speed = speed;
}

int FFMPEGClock::GetSerial() {
    return serial;
}

double FFMPEGClock::GetSpeed() {
    return speed;
}

double FFMPEGClock::GetPts() {
    return pts;
}

double  FFMPEGClock::GetLastUpdated() {
    return last_updated;
}

void  FFMPEGClock::SyncToSlave(FFMPEGClock *slave) {
    double clock = Get();
    double slave_clock = slave->Get();
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        Set(slave_clock, slave->GetSerial());
}