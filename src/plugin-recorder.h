#pragma once
#include <string>

struct OBSEncoderInfo {
    std::string vcodec;          // "h264" | "hevc" | "av1"
    std::string encoder;         // "nvenc" | "qsv" | "amf" | "videotoolbox" | "software"
    int         bitrate_kbps = 8000;
    std::string ext;             // file extension without dot, e.g. "mp4" or "mkv"
};

// Start recording via OBS. Sets our_recording_active = true.
// Does nothing if OBS is already recording.
void recorder_start_our_recording(bool &our_recording_active);

// Stop recording via OBS.
void recorder_stop_our_recording();

// Get the path of the file just recorded.
// Call after OBS_FRONTEND_EVENT_RECORDING_STOPPED. Returns "" on failure.
std::string recorder_get_last_recording_path();

// Read the current recording output encoder settings from OBS.
OBSEncoderInfo recorder_read_encoder_info(const std::string &recording_path);

// Expand a strftime-style template with the current local time.
std::string recorder_expand_template(const std::string &tmpl);
