#pragma once
#include <string>
#include <vector>

struct FfmpegSpec {
    std::string ffmpeg;
    std::string intro;
    std::string input;
    std::string outro;
    std::string output;
    std::string vcodec;
    std::string encoder;
    int         bitrate;
    int         out_width  = 0;  // 0 = do not force scale
    int         out_height = 0;
    std::string out_format;      // "mkv" | "mp4" | "" = infer from extension
};

// Returns the full FFmpeg argument list (first element is the ffmpeg binary path).
std::vector<std::string> build_ffmpeg_args(const FfmpegSpec &spec);

// Runs FFmpeg with the given spec. Reads stdout (via -progress pipe:1) for
// progress and writes 0-100 to the progress file. Returns ffmpeg exit code.
int run_ffmpeg(const FfmpegSpec &spec, const std::string &progress_path);
