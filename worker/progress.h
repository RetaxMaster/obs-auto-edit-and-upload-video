#pragma once
#include <string>

// Parses a Duration line from ffmpeg stderr output.
// Returns total seconds, or -1.0 if not found.
// Input example: "  Duration: 00:01:23.45, start: 0.000000, bitrate: ..."
double parse_duration_seconds(const std::string &line);

// Writes progress updates to a file read by the plugin.
class ProgressWriter {
public:
    explicit ProgressWriter(const std::string &path);

    // Writes an integer 0-100.
    void write(int percent);

    // Writes "error: <message>".
    void write_error(const std::string &message);

private:
    std::string path_;
};
