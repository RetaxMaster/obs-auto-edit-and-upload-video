#pragma once
#include <string>
#include <stdexcept>

struct WorkerArgs {
    std::string input;
    std::string intro;    // optional; empty = skip
    std::string outro;    // optional; empty = skip
    std::string output;
    std::string ffmpeg;
    std::string vcodec;   // "h264" | "hevc" | "av1"
    std::string encoder;  // "nvenc" | "qsv" | "amf" | "videotoolbox" | "software"
    int         bitrate;  // kbps
    std::string progress; // path to progress file
};

// Parses CLI arguments into WorkerArgs.
// Throws std::invalid_argument if a required field is missing.
WorkerArgs parse_args(int argc, char *argv[]);
