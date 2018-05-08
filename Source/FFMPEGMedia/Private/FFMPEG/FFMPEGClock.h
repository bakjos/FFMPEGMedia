#pragma once

class FFMPEGPacketQueue;

class FFMPEGClock
{
public:
    FFMPEGClock();
    ~FFMPEGClock();

    void Init(FFMPEGPacketQueue* queue);
    void Init(FFMPEGClock* clock);
    double Get();
    void Set (double pts, int serial);
    void SetAt( double pts, int serial, double time);
    void SetSpeed(double speed);
    void SetPaused(bool paused);
    double GetPts();
    int GetSerial();
    double GetSpeed();
    double GetLastUpdated();

    void SyncToSlave(FFMPEGClock *slave);
private:

    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
    double speed;
    int serial;           /* clock is based on a packet with this serial */
    bool paused;

    int* queue_serial;

    //int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
};

