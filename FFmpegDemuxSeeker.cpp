#include "FFmpegDemuxSeeker.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#include <iomanip>

FFmpegDemuxSeeker::FFmpegDemuxSeeker(const std::string& filename, DecoderType decoder_type)
    : decoder_type(decoder_type), fmt_ctx(nullptr), codec_ctx(nullptr),
      video_stream_index(-1), current_pos(0), duration(0), frame_number(0),
      quit_flag(false), seek_requested(false), seek_offset(0) {

    if (avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, nullptr) < 0)
        throw std::runtime_error("Failed to open file");

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
        throw std::runtime_error("Failed to find stream info");

    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1)
        throw std::runtime_error("No video stream found");

    const AVCodec *codec = nullptr;
    if (decoder_type == SOFTWARE) {
        codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
    } else if (decoder_type == HARDWARE) {
        codec = avcodec_find_decoder_by_name("h264_v4l2m2m");
        if (!codec) {
            std::cerr << "[Error] v4l2_m2m decoder not found, falling back to SW decoder\n";
            codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
        }
    }

    if (!codec)
        throw std::runtime_error("Unsupported codec");

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0)
        throw std::runtime_error("Failed to open codec");

    duration = fmt_ctx->duration;
    std::cout << "Loaded: " << filename << ", duration: " << (duration / AV_TIME_BASE) << " sec\n";

    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_index]->codecpar;

    const char* codec_name = codec ? codec->long_name : "unknown";
    double duration_sec = (fmt_ctx->duration != AV_NOPTS_VALUE)
        ? fmt_ctx->duration * av_q2d(AV_TIME_BASE_Q) : 0;

    std::cout << "Video stream index: " << video_stream_index << "\n";
    std::cout << "Encoded format: " << codec_name << "\n";
    std::cout << "Codec ID: " << codecpar->codec_id << "\n";
    std::cout << "Resolution: " << codecpar->width << "x" << codecpar->height << "\n";
    std::cout << "Pixel format: " << av_get_pix_fmt_name((AVPixelFormat)codecpar->format) << "\n";
    std::cout << "Duration: " << duration_sec << " seconds\n";
    std::cout << "Overall Bitrate: " << (fmt_ctx->bit_rate / 1000) << " kbps\n";
    std::cout << "Video stream Bitrate: " << (codecpar->bit_rate / 1000) << " kbps\n";
    std::cout << "Decoder used: " << codec->name << "\n";
}

FFmpegDemuxSeeker::~FFmpegDemuxSeeker() {
    if (codec_ctx)
        avcodec_free_context(&codec_ctx);
    if (fmt_ctx)
        avformat_close_input(&fmt_ctx);
}

void FFmpegDemuxSeeker::run() {
    std::thread demux_thread(&FFmpegDemuxSeeker::demuxLoop, this);
    std::thread input_thread(&FFmpegDemuxSeeker::inputLoop, this);
    demux_thread.join();
    input_thread.join();
}

void FFmpegDemuxSeeker::demuxLoop() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (!quit_flag) {
        if (seek_requested) {
            std::lock_guard<std::mutex> lock(seek_mutex);
            int64_t new_pos = current_pos + seek_offset;
            if (new_pos < 0) new_pos = 0;
            if (new_pos > duration) new_pos = duration;
            current_pos = new_pos;

            int64_t ts = av_rescale_q(current_pos, AV_TIME_BASE_Q,
                                      fmt_ctx->streams[video_stream_index]->time_base);
            if (av_seek_frame(fmt_ctx, video_stream_index, ts, AVSEEK_FLAG_BACKWARD) < 0) {
                std::cerr << "[Seek] Failed\n";
            } else {
                avcodec_flush_buffers(codec_ctx);
                std::cout << "[Seek] Jumped to " << current_pos / AV_TIME_BASE << " sec\n";
            }

            seek_requested = false;
        }

        int ret = av_read_frame(fmt_ctx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                std::cout << "[EOF reached]\n";
                quit_flag = true;
            } else {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                std::cerr << "[Error reading frame: "
                          << av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, ret)
                          << "]\n";
            }
            break;
        }

        if (packet->stream_index == video_stream_index) {
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    printFrameInfo(frame);
                }
            }
        }
        av_packet_unref(packet);
        usleep(5000); // reduce CPU usage
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
}

void FFmpegDemuxSeeker::inputLoop() {
    std::cout << "Controls:\n"
              << "  s - Seek forward 5s\n"
              << "  a - Seek backward 5s\n"
              << "  q - Quit\n";

    while (!quit_flag) {
        char c = getch_select();
        if (c == 'q') {
            quit_flag = true;
            std::cout << "[Quit]\n";
            break;
        } else if (c == 's') {
            requestSeek(SEEK_STEP * AV_TIME_BASE);
        } else if (c == 'a') {
            requestSeek(-SEEK_STEP * AV_TIME_BASE);
        }
    }
}

void FFmpegDemuxSeeker::requestSeek(int64_t offset) {
    std::lock_guard<std::mutex> lock(seek_mutex);
    seek_offset = offset;
    seek_requested = true;
}

void FFmpegDemuxSeeker::printFrameInfo(AVFrame* frame) {
    AVMD5* md5 = av_md5_alloc();
    if (!md5) {
        std::cerr << "Failed to allocate MD5 context\n";
        return;
    }

    av_md5_init(md5);

    for (int plane = 0; plane < AV_NUM_DATA_POINTERS && frame->data[plane]; plane++) {
        int linesize = frame->linesize[plane];
        int height = (plane == 0 || frame->format == AV_PIX_FMT_GRAY8) ? frame->height : frame->height / 2;

        for (int y = 0; y < height; y++) {
            av_md5_update(md5, frame->data[plane] + y * linesize, linesize);
        }
    }

    uint8_t digest[16];
    av_md5_final(md5, digest);
    av_free(md5);

    char md5string[33];
    for (int i = 0; i < 16; i++) {
        snprintf(md5string + i * 2, 3, "%02x", digest[i]);
    }

    char pict_type_char = av_get_picture_type_char(frame->pict_type);
    double timestamp = (frame->pts != AV_NOPTS_VALUE)
        ? frame->pts * av_q2d(fmt_ctx->streams[video_stream_index]->time_base)
        : -1;

    const char* pix_fmt_name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format));

    std::cout << "Frame #" << frame_number++
              << " | Type: " << pict_type_char
              << " | PTS: " << frame->pts
              << " | DTS: " << frame->pkt_dts
              << " | Timestamp: " << std::fixed << std::setprecision(3) << timestamp << "s"
              << " | Resolution: " << frame->width << "x" << frame->height
              << " | Pixel fmt: " << (pix_fmt_name ? pix_fmt_name : "unknown")
              << " | Decoded frm MD5: " << md5string
              << "\n";

    if (frame->pts != AV_NOPTS_VALUE) {
        current_pos = av_rescale_q(frame->pts,
                                   fmt_ctx->streams[video_stream_index]->time_base,
                                   AV_TIME_BASE_Q);
    }
}

char FFmpegDemuxSeeker::getch() {
    char buf = 0;
    struct termios old = {0};
    tcgetattr(STDIN_FILENO, &old);
    old.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    read(STDIN_FILENO, &buf, 1);
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    return buf;
}
//----------------------------

char FFmpegDemuxSeeker::getch_select(int timeout_ms) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    char ch = -1;
    int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, (timeout_ms >= 0 ? &tv : NULL));

    if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
        char buf;
        if (read(STDIN_FILENO, &buf, 1) == 1) {
            ch = static_cast<unsigned char>(buf);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
