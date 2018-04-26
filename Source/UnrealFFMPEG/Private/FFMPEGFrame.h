#pragma once

extern "C" {
    #include <libavcodec/avcodec.h>
}

class FFMPEGFrame
{
public:
    FFMPEGFrame();
    ~FFMPEGFrame();

    int init ();
    void destroy();
    void unref();

    int get_serial();
    int64_t get_pos();
    double get_pts();
    double get_duration();
    AVFrame* get_frame();
    int  get_width();
    int  get_height();
    int  get_format();
    AVRational  get_sar();
    int  get_uploaded();
    int  get_flip_v();
    AVSubtitle& get_sub();
    
    void update_frame(AVFrame* src_frame, double pts, double duration, int64_t pos, int serial);
    void update_size(FFMPEGFrame *vp);

    
    void set_pos(int64_t pos);
    void set_duration(double duration);
    void set_pts(double pts);
    void set_serial(int serial);
    void set_width(int width);
    void set_height(int height);
    void set_uploaded(int u);
    void set_flip_v ( int fv);

    double get_difference( FFMPEGFrame* nextvp, double max );

private:
    AVFrame *frame;
    AVSubtitle sub;
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t pos;          /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    AVRational sar;
    int uploaded;
    int flip_v;
};
