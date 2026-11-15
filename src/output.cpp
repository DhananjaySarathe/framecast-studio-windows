#include "framecast/core.hpp"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <algorithm>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace framecast {
namespace {
void checked(int result, const char* operation) {
    if (result >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]; av_strerror(result, message, sizeof(message));
    throw std::runtime_error(std::string(operation)+": "+message);
}
bool softwareEncoder(const std::string& name) { return name == "libopenh264" || name == "libx264"; }
std::string avError(int code) {
    char message[AV_ERROR_MAX_STRING_SIZE]; av_strerror(code, message, sizeof(message));
    return message;
}
// One way of encoding H.264: an FFmpeg encoder plus the options that select a variant of it.
struct EncoderChoice {
    std::string label, codec;
    std::vector<std::pair<std::string, std::string>> options;
};
std::vector<EncoderChoice> automaticChoices() {
#ifdef _WIN32
    // Vendor encoders first, then Media Foundation, which reaches the GPU's encoder on any vendor through
    // Windows itself, then Microsoft's software encoder, then OpenH264 (the order Sunshine uses).
    return {{"h264_nvenc", "h264_nvenc", {}},
            {"h264_qsv", "h264_qsv", {}},
            {"h264_amf", "h264_amf", {}},
            {"h264_mf hardware", "h264_mf", {{"hw_encoding", "1"}}},
            {"h264_mf", "h264_mf", {}},
            {"libopenh264", "libopenh264", {}},
            {"libx264", "libx264", {{"preset", "veryfast"}, {"tune", "zerolatency"}}}};
#else
    return {{"libx264", "libx264", {{"preset", "veryfast"}, {"tune", "zerolatency"}}}, {"libopenh264", "libopenh264", {}}};
#endif
}
// The choice that passed a test encode for a canvas size, so later recordings open it directly. Probing
// (QSV sessions, AMF and CUDA DLLs, a test encode each) can take longer than a recording queue lasts.
// encoderProbeMutex lets one thread probe while the others wait and then find the answer cached.
std::mutex encoderCacheMutex, encoderProbeMutex;
std::map<std::string, std::string> encoderCache;
std::string encoderKey(const OutputSpec& spec) {
    return std::to_string(spec.width)+"x"+std::to_string(spec.height)+"@"+std::to_string(spec.fps);
}
// Without a hardware encoder, two 1080p30 OpenH264 encodes do not keep up on a laptop. In automatic mode
// OpenH264 and x264 record at most 1280x720 / 720x1280; the canvas and the studio stay at full size.
std::pair<int, int> encodeSize(const OutputSpec& spec, const EncoderChoice& choice) {
    if (spec.encoder != "auto" || !softwareEncoder(choice.codec) || int64_t(spec.width)*spec.height <= 1280LL*720)
        return {spec.width, spec.height};
    const double scale = std::sqrt(1280.*720/(double(spec.width)*spec.height));
    return {std::max(2, static_cast<int>(std::lround(spec.width*scale/2))*2),
            std::max(2, static_cast<int>(std::lround(spec.height*scale/2))*2)};
}
AVCodecContext* createEncoder(const OutputSpec& spec, const EncoderChoice& choice, bool globalHeader, std::string& why) {
    const AVCodec* codec = avcodec_find_encoder_by_name(choice.codec.c_str());
    if (!codec || codec->id != AV_CODEC_ID_H264) { why = "not in this FFmpeg build"; return nullptr; }
    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c) throw std::bad_alloc();
    const auto [width, height] = encodeSize(spec, choice);
    c->width = width; c->height = height;
    c->time_base = {1, spec.fps}; c->framerate = {spec.fps, 1};
    c->pix_fmt = softwareEncoder(choice.codec) ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
    c->bit_rate = int64_t(spec.videoKbps)*1000;
    c->rc_max_rate = c->bit_rate; c->rc_buffer_size = static_cast<int>(c->bit_rate*2);
    c->gop_size = spec.fps*2; c->max_b_frames = 0;
    c->color_range = AVCOL_RANGE_MPEG; c->colorspace = AVCOL_SPC_BT709;
    c->color_primaries = AVCOL_PRI_BT709; c->color_trc = AVCOL_TRC_BT709;
    // Software encoders get more threads; two outputs together use about all cores.
    c->thread_count = softwareEncoder(choice.codec)
        ? static_cast<int>(std::clamp(std::thread::hardware_concurrency()/2, 2u, 4u)) : 2;
    if (globalHeader) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = nullptr;
    for (const auto& [key, value] : choice.options) av_dict_set(&options, key.c_str(), value.c_str(), 0);
    const int result = avcodec_open2(c, codec, &options); av_dict_free(&options);
    if (result < 0) { why = "does not open: " + avError(result); avcodec_free_context(&c); return nullptr; }
    return c;
}
// An encoder that opens is not yet an encoder that works: drivers accept a session and then reject the
// first frame. Push a few grey frames through and require a packet back (OBS and Sunshine both test
// encoders before use rather than trusting their names).
bool testEncode(AVCodecContext* c, std::string& why) {
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    if (!frame || !packet) { av_frame_free(&frame); av_packet_free(&packet); throw std::bad_alloc(); }
    frame->format = c->pix_fmt; frame->width = c->width; frame->height = c->height;
    int result = av_frame_get_buffer(frame, 32), packets = 0;
    auto drain = [&](bool flushing) {
        for (int n = 0; n < 256 && result >= 0; ++n) {
            const int received = avcodec_receive_packet(c, packet);
            if (received == AVERROR_EOF || (received == AVERROR(EAGAIN) && !flushing)) break;
            if (received == AVERROR(EAGAIN)) continue;
            if (received < 0) { result = received; break; }
            ++packets; av_packet_unref(packet);
        }
    };
    for (int n = 0; n < 3 && result >= 0; ++n) {
        result = av_frame_make_writable(frame);
        if (result < 0) break;
        std::memset(frame->data[0], 96, static_cast<size_t>(frame->linesize[0])*frame->height);
        for (int plane = 1; plane < 3 && frame->data[plane]; ++plane)
            std::memset(frame->data[plane], 128, static_cast<size_t>(frame->linesize[plane])*(frame->height/2));
        frame->pts = n;
        result = avcodec_send_frame(c, frame);
        drain(false);
    }
    if (result >= 0) { result = avcodec_send_frame(c, nullptr); drain(true); }
    av_frame_free(&frame); av_packet_free(&packet);
    if (result < 0) { why = "fails its test encode: " + avError(result); return false; }
    if (!packets) { why = "produced no packets in its test encode"; return false; }
    return true;
}
void forgetEncoder(const OutputSpec& spec) {
    std::lock_guard guard(encoderCacheMutex);
    encoderCache.erase(encoderKey(spec));
}
// Opens an H.264 encoder that is known to work for this output. label names it, with the encoded size
// when that differs from the canvas.
AVCodecContext* openVideoEncoder(const OutputSpec& spec, bool globalHeader, std::string& label) {
    auto named = [&](const EncoderChoice& choice, const AVCodecContext* c) {
        return c->width == spec.width && c->height == spec.height ? choice.label
            : choice.label+" at "+std::to_string(c->width)+"x"+std::to_string(c->height);
    };
    std::string why;
    if (spec.encoder != "auto") {
        // An encoder the person asked for by name is used as given, so its own error is what they see.
        const EncoderChoice choice{spec.encoder, spec.encoder,
            spec.encoder == "libx264" ? std::vector<std::pair<std::string, std::string>>{{"preset", "veryfast"}, {"tune", "zerolatency"}}
                                      : std::vector<std::pair<std::string, std::string>>{}};
        if (AVCodecContext* c = createEncoder(spec, choice, globalHeader, why)) { label = named(choice, c); return c; }
        throw std::runtime_error("H.264 encoder " + spec.encoder + " " + why);
    }
    std::lock_guard probing(encoderProbeMutex);
    auto choices = automaticChoices();
    const std::string key = encoderKey(spec);
    std::string trusted;
    {
        std::lock_guard guard(encoderCacheMutex);
        const auto cached = encoderCache.find(key);
        if (cached != encoderCache.end()) trusted = cached->second;
    }
    std::stable_partition(choices.begin(), choices.end(), [&](const auto& choice) { return choice.label == trusted; });
    for (const auto& choice : choices) {
        if (choice.label != trusted) {
            AVCodecContext* probe = createEncoder(spec, choice, false, why);
            const bool works = probe && testEncode(probe, why);
            avcodec_free_context(&probe);
            if (!works) { logLine("encoder " + choice.label + " " + why); continue; }
            logLine("encoder " + choice.label + " passed its test encode for " + key);
            std::lock_guard guard(encoderCacheMutex);
            encoderCache[key] = choice.label;
        }
        if (AVCodecContext* c = createEncoder(spec, choice, globalHeader, why)) { label = named(choice, c); return c; }
        logLine("encoder " + choice.label + " " + why);
        forgetEncoder(spec);
    }
    throw std::runtime_error("No usable H.264 encoder. Install an FFmpeg build with OpenH264 or a supported hardware encoder.");
}
struct Session {
    AVFormatContext* format = nullptr;
    AVCodecContext *video = nullptr, *audio = nullptr;
    AVStream *vs = nullptr, *as = nullptr;
    AVFrame *vf = nullptr;
    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    AVAudioFifo* fifo = nullptr;
    std::atomic<bool>& cancel;
    std::atomic<int64_t> deadline{0};
    int64_t baseFrame = 0, audioPts = 0, expectedSample = 0;
    bool header = false;
    std::string encoder;
    int sourceWidth = 0, sourceHeight = 0;
    double convertTotal = 0, encodeTotal = 0; // milliseconds, over timedFrames
    uint64_t timedFrames = 0;
    explicit Session(std::atomic<bool>& stop) : cancel(stop) {}
    ~Session() {
        if (format && format->pb) avio_closep(&format->pb);
        avformat_free_context(format);
        avcodec_free_context(&video); avcodec_free_context(&audio);
        av_frame_free(&vf); sws_freeContext(scaler); swr_free(&resampler);
        if (fifo) av_audio_fifo_free(fifo);
    }
    static int interrupt(void* opaque) {
        auto& s = *static_cast<Session*>(opaque);
        const auto d = s.deadline.load();
        return s.cancel.load() || (d != 0 && hostNow() > d);
    }
    void timeout() { deadline = hostNow()+3000000000LL; }
    void beginAt(const OutputBatch& first) {
        baseFrame = first.frame; expectedSample = first.sample;
    }
    void open(const OutputSpec& spec, const OutputBatch& first) {
        beginAt(first);
        sourceWidth = spec.width; sourceHeight = spec.height;
        checked(avformat_alloc_output_context2(&format, nullptr, spec.streaming ? "flv" : nullptr, spec.destination.c_str()), "Choose output container");
        if (!format) throw std::runtime_error("Unsupported output container");
        format->interrupt_callback = {interrupt, this};
        video = openVideoEncoder(spec, format->oformat->flags & AVFMT_GLOBALHEADER, encoder);
        const AVCodec* aac = avcodec_find_encoder_by_name("aac");
        if (!aac) throw std::runtime_error("AAC encoder unavailable");
        audio = avcodec_alloc_context3(aac);
        if (!audio) throw std::bad_alloc();
        audio->sample_rate = sampleRate; audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
        audio->time_base = {1, sampleRate}; audio->bit_rate = 160000;
        av_channel_layout_default(&audio->ch_layout, 2);
        if (format->oformat->flags & AVFMT_GLOBALHEADER) audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        checked(avcodec_open2(audio, aac, nullptr), "Start AAC encoder");
        vs = avformat_new_stream(format, nullptr); as = avformat_new_stream(format, nullptr);
        if (!vs || !as) throw std::bad_alloc();
        vs->time_base = video->time_base; as->time_base = audio->time_base;
        vs->avg_frame_rate = video->framerate;
        checked(avcodec_parameters_from_context(vs->codecpar, video), "Copy video configuration");
        checked(avcodec_parameters_from_context(as->codecpar, audio), "Copy audio configuration");
        vf = av_frame_alloc(); if (!vf) throw std::bad_alloc();
        vf->width = video->width; vf->height = video->height; vf->format = video->pix_fmt;
        vf->color_range = video->color_range; vf->colorspace = video->colorspace;
        vf->color_primaries = video->color_primaries; vf->color_trc = video->color_trc;
        checked(av_frame_get_buffer(vf, 32), "Allocate video frame");
        scaler = sws_getContext(spec.width, spec.height, AV_PIX_FMT_BGRA, video->width, video->height,
                                video->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scaler) throw std::runtime_error("Create colour converter");
        checked(sws_setColorspaceDetails(scaler, sws_getCoefficients(SWS_CS_ITU709), 1,
                sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1<<16, 1<<16), "Set Rec.709 conversion");
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        checked(swr_alloc_set_opts2(&resampler, &audio->ch_layout, audio->sample_fmt, sampleRate,
                                    &stereo, AV_SAMPLE_FMT_FLT, sampleRate, 0, nullptr), "Create audio converter");
        checked(swr_init(resampler), "Start audio converter");
        fifo = av_audio_fifo_alloc(audio->sample_fmt, 2, audio->frame_size*4);
        if (!fifo) throw std::bad_alloc();
        timeout();
        AVDictionary* options = nullptr;
        if (spec.streaming) av_dict_set(&options, "rw_timeout", "3000000", 0);
        int result = avio_open2(&format->pb, spec.destination.c_str(), AVIO_FLAG_WRITE, &format->interrupt_callback, &options);
        av_dict_free(&options); checked(result, "Open output");
        if (!spec.streaming && (std::string(format->oformat->name).find("mp4") != std::string::npos ||
                               std::string(format->oformat->name).find("mov") != std::string::npos))
            // Delay the initial moov until AAC priming timestamps are known. An empty moov
            // can stretch the first video frame when the muxer later shifts negative AAC DTS.
            av_dict_set(&options, "movflags", "frag_keyframe+delay_moov+default_base_moof", 0);
        timeout(); result = avformat_write_header(format, &options);
        av_dict_free(&options); checked(result, "Write output header"); header = true;
    }
    void encode(AVCodecContext* codec, AVStream* stream, AVFrame* frame) {
        checked(avcodec_send_frame(codec, frame), "Encode frame");
        AVPacket* packet = av_packet_alloc();
        if (!packet) throw std::bad_alloc();
        try {
            while (true) {
                int result = avcodec_receive_packet(codec, packet);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
                checked(result, "Read encoded packet");
                av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
                packet->stream_index = stream->index;
                timeout(); checked(av_interleaved_write_frame(format, packet), "Write media packet");
            }
        } catch (...) { av_packet_free(&packet); throw; }
        av_packet_free(&packet);
    }
    void drainAudio(bool final) {
        while (av_audio_fifo_size(fifo) >= audio->frame_size || (final && av_audio_fifo_size(fifo) > 0)) {
            AVFrame* af = av_frame_alloc(); if (!af) throw std::bad_alloc();
            try {
                af->nb_samples = audio->frame_size; af->format = audio->sample_fmt; af->sample_rate = sampleRate;
                checked(av_channel_layout_copy(&af->ch_layout, &audio->ch_layout), "Copy channel layout");
                checked(av_frame_get_buffer(af, 0), "Allocate audio frame");
                checked(av_samples_set_silence(af->data, 0, af->nb_samples, 2, audio->sample_fmt), "Pad audio tail");
                const int n = std::min(af->nb_samples, av_audio_fifo_size(fifo));
                if (av_audio_fifo_read(fifo, reinterpret_cast<void**>(af->data), n) != n) throw std::runtime_error("Read audio FIFO");
                af->pts = audioPts; audioPts += af->nb_samples;
                encode(audio, as, af);
            } catch (...) { av_frame_free(&af); throw; }
            av_frame_free(&af);
        }
    }
    void write(const OutputBatch& batch) {
        if ((batch.video && (batch.video->width != sourceWidth || batch.video->height != sourceHeight)) || batch.sample != expectedSample)
            throw std::runtime_error("Output frame size or audio timeline discontinuity");
        // A batch without a picture is a skipped frame: its audio is still written, and the previous
        // picture stays on screen for that interval.
        if (batch.video) {
            checked(av_frame_make_writable(vf), "Reuse video frame");
            const uint8_t* source[] = {batch.video->bgra.data()}; int stride[] = {sourceWidth*4};
            const Nanoseconds started = hostNow();
            if (sws_scale(scaler, source, stride, 0, sourceHeight, vf->data, vf->linesize) != video->height)
                throw std::runtime_error("Convert video frame");
            const Nanoseconds convertDone = hostNow();
            vf->pts = batch.frame-baseFrame;
            encode(video, vs, vf);
            convertTotal += (convertDone-started)/1e6; encodeTotal += (hostNow()-convertDone)/1e6; ++timedFrames;
        }
        const int frames = static_cast<int>(batch.audio->size()/2);
        expectedSample += frames;
        uint8_t** converted = nullptr;
        checked(av_samples_alloc_array_and_samples(&converted, nullptr, 2, frames+32, audio->sample_fmt, 0), "Allocate converted audio");
        try {
            const uint8_t* input[] = {reinterpret_cast<const uint8_t*>(batch.audio->data())};
            const int n = swr_convert(resampler, converted, frames+32, input, frames);
            checked(n, "Convert audio");
            checked(av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo)+n), "Grow audio FIFO");
            if (av_audio_fifo_write(fifo, reinterpret_cast<void**>(converted), n) != n) throw std::runtime_error("Write audio FIFO");
        } catch (...) { av_freep(&converted[0]); av_freep(&converted); throw; }
        av_freep(&converted[0]); av_freep(&converted);
        drainAudio(false);
    }
    void finish() {
        if (!header) return;
        drainAudio(true); encode(video, vs, nullptr); encode(audio, as, nullptr);
        timeout(); checked(av_write_trailer(format), "Finalize output"); header = false;
    }
};
}
struct OutputWorker::Impl {
    OutputSpec spec;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<OutputBatch> queue;
    size_t queuedPictures = 0;
    OutputStatus status;
    bool closing = false, restart = false, failed = false;
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::thread thread;
    explicit Impl(OutputSpec s) : spec(std::move(s)), thread([this] { run(); }) {}
    void run() {
        std::unique_ptr<Session> session;
        int attempts = 0;
        try {
            while (true) {
                OutputBatch batch;
                bool resetSession = false;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [&] { return closing || failed || !queue.empty(); });
                    if (failed || (closing && (queue.empty() || spec.streaming))) break;
                    if (restart) { resetSession = true; restart = false; status.state = "reconnecting"; ++status.reconnects; }
                    batch = std::move(queue.front()); queue.pop_front();
                    if (batch.video && queuedPictures) --queuedPictures;
                }
                if (resetSession) session.reset();
                try {
                    if (!session) {
                        session = std::make_unique<Session>(cancel);
                        session->open(spec, batch);
                        std::lock_guard lock(mutex);
                        // submit() can fail the recording queue while encoder/file
                        // startup is blocked. That failure must remain authoritative.
                        if (failed) throw std::runtime_error(status.error);
                        if (spec.streaming) {
                            // Connection setup can take longer than the bounded queue's
                            // duration. Start this encoder's clock at the newest batch
                            // before encoding anything, without reopening the connection.
                            if (!queue.empty()) {
                                status.dropped += queue.size();
                                batch = std::move(queue.back()); queue.clear();
                            }
                            restart = false;
                            session->beginAt(batch);
                        }
                        status.encoder = session->encoder; status.state = "live"; status.error.clear();
                    }
                    session->write(batch);
                    std::lock_guard lock(mutex); attempts = 0;
                    if (batch.video) ++status.frames;
                    if (session->timedFrames) {
                        status.convertMs = session->convertTotal/session->timedFrames;
                        status.encodeMs = session->encodeTotal/session->timedFrames;
                    }
                } catch (const std::exception& e) {
                    session.reset();
                    // Whatever the encoder was, it is no longer trusted: the next output probes again.
                    if (spec.encoder == "auto") forgetEncoder(spec);
                    logLine(std::string("warning: output ") + (spec.streaming ? "stream" : "recording") + " error: " + e.what());
                    std::unique_lock lock(mutex);
                    status.error = e.what();
                    if (!spec.streaming) { failed = true; status.state = "failed"; queue.clear(); queuedPictures = 0; break; }
                    if (closing) break;
                    status.state = "reconnecting"; ++status.reconnects;
                    status.dropped += queue.size()+1; queue.clear(); restart = false;
                    const int delay[] = {1, 2, 3, 5};
                    changed.wait_for(lock, std::chrono::seconds(delay[std::min(attempts++, 3)]), [&] { return closing; });
                    // Begin the new connection at the latest available complete A/V batch.
                    if (queue.size() > 1) { status.dropped += queue.size()-1; auto latest = std::move(queue.back()); queue.clear(); queue.push_back(std::move(latest)); }
                    restart = false; // Backoff backlog is not a failure of the next connection.
                }
            }
            if (session && !cancel) session->finish();
            std::lock_guard lock(mutex);
            if (!failed) status.state = "stopped";
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex); failed = true; status.state = "failed"; status.error = e.what(); queue.clear();
        }
        session.reset();
        done = true;
    }
};
std::vector<std::string> availableH264Encoders() {
    std::vector<std::string> out;
    void* state = nullptr;
    while (const AVCodec* codec = av_codec_iterate(&state))
        if (codec->id == AV_CODEC_ID_H264 && av_codec_is_encoder(codec)) out.push_back(codec->name);
    return out;
}
void warmEncoders(int width, int height, int fps) {
    for (auto canvas : {Canvas::horizontal, Canvas::vertical}) {
        OutputSpec spec;
        spec.canvas = canvas; spec.fps = fps;
        spec.width = canvas == Canvas::horizontal ? width : height;
        spec.height = canvas == Canvas::horizontal ? height : width;
        try {
            std::string label;
            AVCodecContext* c = openVideoEncoder(spec, false, label);
            avcodec_free_context(&c);
            logLine("encoder for " + encoderKey(spec) + ": " + label);
        } catch (const std::exception& e) {
            logLine(std::string("warning: ") + e.what()); // The recording reports the same error if nothing works.
        }
    }
}
OutputWorker::OutputWorker(OutputSpec spec) {
    if (spec.width < 2 || spec.height < 2 || spec.width%2 || spec.height%2 || spec.fps < 1 || spec.fps > 120 ||
        spec.videoKbps < 100 || spec.videoKbps > 100000 || spec.destination.empty()) throw std::invalid_argument("Invalid output specification");
    impl_ = std::make_unique<Impl>(std::move(spec));
}
OutputWorker::~OutputWorker() { finish(); }
void OutputWorker::submit(OutputBatch batch) {
    auto& p = *impl_;
    if (!batch.video || !batch.audio || batch.frame < 0 ||
        batch.sample != samplesAtFrame(batch.frame, p.spec.fps) ||
        batch.audio->size() != static_cast<size_t>(samplesAtFrame(batch.frame+1, p.spec.fps)-batch.sample)*2 ||
        batch.video->width != p.spec.width || batch.video->height != p.spec.height ||
        batch.video->bgra.size() != static_cast<size_t>(p.spec.width)*p.spec.height*4)
        throw std::invalid_argument("Invalid output A/V batch");
    std::lock_guard lock(p.mutex);
    if (p.closing || p.failed) return;
    if (p.spec.streaming) {
        if (p.queue.size() >= 8) { p.status.dropped += p.queue.size(); p.queue.clear(); p.queuedPictures = 0; p.restart = true; }
    } else {
        // A recording that falls behind loses pictures, never the file (OBS does the same): once eight
        // pictures are waiting, further frames are queued with their audio only, so sound stays continuous
        // and in sync and the previous picture is held. Only a writer that has stopped altogether, with
        // twenty seconds queued, is given up on.
        if (p.queue.size() >= static_cast<size_t>(p.spec.fps)*20) {
            p.status.state = "failed"; p.status.error = "Recording cannot keep up; the disk or the encoder stopped responding.";
            p.failed = true; p.queue.clear(); p.queuedPictures = 0; p.changed.notify_all();
            logLine("warning: recording failed: twenty seconds of frames were waiting to be written");
            return;
        }
        // Until the encoder session and file are open, allow up to a second of pictures. On the laptop two
        // NVENC sessions opening together took longer than eight frames, so every recording began by
        // skipping pictures and the studio warned that the computer could not keep up.
        const size_t pictureLimit = p.status.state == "live" ? 8 : std::max<size_t>(8, static_cast<size_t>(p.spec.fps));
        if (p.queuedPictures >= pictureLimit) {
            batch.video.reset();
            if (p.status.dropped++ % 150 == 0)
                logLine("warning: recording encoder is behind; skipping pictures (" + std::to_string(p.status.dropped) + " so far)");
        }
    }
    if (batch.video) ++p.queuedPictures;
    p.queue.push_back(std::move(batch)); p.changed.notify_one();
}
void OutputWorker::requestFinish() {
    if (!impl_) return;
    auto& p = *impl_;
    { std::lock_guard lock(p.mutex); p.closing = true; if (p.spec.streaming) p.cancel = true; }
    p.changed.notify_all();
}
bool OutputWorker::finished() const { return impl_->done.load(); }
void OutputWorker::finish() {
    requestFinish();
    if (!impl_) return;
    auto& p = *impl_;
    if (p.thread.joinable()) p.thread.join();
}
OutputStatus OutputWorker::status() const { std::lock_guard lock(impl_->mutex); return impl_->status; }
}
