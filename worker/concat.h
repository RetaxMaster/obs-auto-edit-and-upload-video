#pragma once
#include <string>
#include <vector>

struct FfmpegSpec {
    std::string ffmpeg;   // path to ffmpeg binary
    std::string intro;    // optional; empty = skip
    std::string input;    // required; main recording
    std::string outro;    // optional; empty = skip
    std::string output;   // required; output file path
    std::string vcodec;   // "h264" | "hevc" | "av1"
    std::string encoder;  // "nvenc" | "qsv" | "amf" | "videotoolbox" | "software"
    int         bitrate;  // kbps
};

// Returns the full FFmpeg argument list (first element is the ffmpeg binary path).
std::vector<std::string> build_ffmpeg_args(const FfmpegSpec &spec);

// Runs FFmpeg with the given spec. Reads stdout (via -progress pipe:1) for
// progress and writes 0-100 to the progress file. Returns ffmpeg exit code.
int run_ffmpeg(const FfmpegSpec &spec, const std::string &progress_path);
