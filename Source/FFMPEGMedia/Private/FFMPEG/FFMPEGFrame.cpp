#include "FFMPEGFrame.h"

FFMPEGFrame::FFMPEGFrame()
{
    frame = NULL;
    serial = 0;
    pts = 0.0;           
    duration = 0.0;      
    pos = 0;          
    width = 0;
    height = 0;
    format = 0;    
    uploaded = 0;
    flip_v = 0;
    sub = {0};
}

FFMPEGFrame::~FFMPEGFrame()
{
    destroy();
}

int FFMPEGFrame::init () {
    destroy();
    frame = av_frame_alloc();
    return frame == 0? 0: 1;
}

void FFMPEGFrame::destroy() {
    if (frame != NULL) {
        unref();
        av_frame_free(&frame);
    }
    frame = NULL;
}

void FFMPEGFrame::unref() {
    if ( frame != NULL) {
        av_frame_unref(frame);
        avsubtitle_free(&sub);
    }
}

int FFMPEGFrame::get_serial() {
    return serial;
}

int64_t FFMPEGFrame::get_pos() {
    return pos;
}

double FFMPEGFrame::get_pts() {
    return pts;

}

double FFMPEGFrame::get_duration() {
    return duration;
}

AVFrame* FFMPEGFrame::get_frame() {
    return frame;
}

int FFMPEGFrame::get_width() {
    return width;
}

int FFMPEGFrame::get_height() {
    return height;
}

int FFMPEGFrame::get_format() {
    return format;
}

AVRational FFMPEGFrame::get_sar() {
    return sar;
}

int FFMPEGFrame::get_uploaded() {
    return uploaded;
}

int FFMPEGFrame::get_flip_v() {
    return flip_v;
}

AVSubtitle& FFMPEGFrame::get_sub() {
    return sub;
}

double FFMPEGFrame::get_difference(FFMPEGFrame* nextvp, double max) {

    if (serial == nextvp->serial) {
        double duration = nextvp->pts - pts;
        if (isnan(duration) || duration <= 0 || duration > max)
            return get_duration();
        else
            return duration;
    }
    else {
        return 0.0;
    }
}

void FFMPEGFrame::update_frame(AVFrame* src_frame, double pts, double duration, int64_t pos, int serial) {
    this->sar = src_frame->sample_aspect_ratio;
    this->uploaded = 0;
    
    this->width = src_frame->width;
    this->height = src_frame->height;
    this->format = src_frame->format;
    
    this->pts = pts;
    this->duration = duration;
    this->pos = pos;
    this->serial = serial;
}


void FFMPEGFrame::update_size(FFMPEGFrame *vp) {
    width = vp->width;
    height = vp->height;
}

void FFMPEGFrame::set_pts(double pts) {
    this->pts = pts;
}

void FFMPEGFrame::set_serial(int serial) {
    this->serial = serial;
}

void FFMPEGFrame::set_width(int width) {
    this->width = width;
}

void FFMPEGFrame::set_height(int height) {
    this->height = height;
}

void FFMPEGFrame::set_uploaded(int u) {
    this->uploaded = u;
}

void FFMPEGFrame::set_flip_v(int fv) {
    this->flip_v = fv;
}

void FFMPEGFrame::set_pos(int64_t pos) {
    this->pos = pos;
}

void FFMPEGFrame::set_duration(double duration) {
    this->duration = duration;
}

