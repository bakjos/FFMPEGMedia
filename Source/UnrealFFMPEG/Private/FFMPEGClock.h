#pragma once

class FFMPEGPacketQueue;

class FFMPEGClock
{
public:
    FFMPEGClock();
    ~FFMPEGClock();

    void init(FFMPEGPacketQueue* queue);
    void init(FFMPEGClock* clock);
    double get();
    void set (double pts, int serial);
    void set_at( double pts, int serial, double time);
    void set_speed(double speed);
    void set_paused(int paused);
    double get_pts();
    int get_serial();
    double get_speed();
    double get_last_updated();

    void sync_to_slave(FFMPEGClock *slave);
private:

    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
    double speed;
    int serial;           /* clock is based on a packet with this serial */
    int paused;

    int* queue_serial;

    //int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
};

