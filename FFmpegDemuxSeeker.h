#ifndef FFMPEG_DEMUX_SEEKER_H
#define FFMPEG_DEMUX_SEEKER_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/md5.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
// #include <libavutil/avutil.h>
// #include <termios.h>
// #include <unistd.h>
// #include <sys/select.h>
}

#include <string>
// #include <iostream>
// #include <thread>
#include <mutex>
#include <atomic>

#define SEEK_STEP 5 // in seconds

enum DecoderType {
    SOFTWARE,
    HARDWARE
};

class FFmpegDemuxSeeker {
public:
    FFmpegDemuxSeeker(const std::string& filename, DecoderType decoder_type = SOFTWARE);
    ~FFmpegDemuxSeeker();

    void run();

private:
    DecoderType decoder_type;
    AVFormatContext* fmt_ctx;
    AVCodecContext* codec_ctx;
    int video_stream_index;
    int64_t current_pos;
    int64_t duration;
    int64_t frame_number;

    std::atomic<bool> quit_flag;
    std::atomic<bool> seek_requested;
    std::mutex seek_mutex;
    int64_t seek_offset; // in microseconds

    void demuxLoop();
    void inputLoop();
    void requestSeek(int64_t offset);
    void printFrameInfo(AVFrame* frame);
    char getch();
    char getch_select(int timeout_ms = 0);
};

#endif // FFMPEG_DEMUX_SEEKER_H

