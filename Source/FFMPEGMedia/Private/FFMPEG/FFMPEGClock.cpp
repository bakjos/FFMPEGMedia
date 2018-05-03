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
    paused = 0;
    queue_serial = NULL;
}


FFMPEGClock::~FFMPEGClock()
{
}

void FFMPEGClock::init(FFMPEGPacketQueue* queue) {
    speed = 1.0;
    paused = 0;
    this->queue_serial = &queue->serial;
    set(NAN, -1);
}

void FFMPEGClock::init(FFMPEGClock* clock) {
    speed = 1.0;
    paused = 0;
    this->queue_serial = &clock->serial;
    set(NAN, -1);
}

double FFMPEGClock::get() {
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

void FFMPEGClock::set(double pts, int serial) {
    double time = av_gettime_relative() / 1000000.0;
    set_at(pts, serial, time);
}

void  FFMPEGClock::set_at( double pts, int serial, double time) {
    this->pts = pts;
    this->last_updated = time;
    this->pts_drift = this->pts - time;
    this->serial = serial;
}

void FFMPEGClock::set_paused(int p) {
    paused = p;
}

void FFMPEGClock::set_speed(double speed) {
    set(get(), serial);
    this->speed = speed;
}

int FFMPEGClock::get_serial() {
    return serial;
}

double FFMPEGClock::get_speed() {
    return speed;
}

double FFMPEGClock::get_pts() {
    return pts;
}

double  FFMPEGClock::get_last_updated() {
    return last_updated;
}

void  FFMPEGClock::sync_to_slave(FFMPEGClock *slave) {
    double clock = get();
    double slave_clock = slave->get();
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set(slave_clock, slave->get_serial());
}