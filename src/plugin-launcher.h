#pragma once
#include <string>
#include "plugin-settings.h"
#include "plugin-recorder.h"

class PluginLauncher {
public:
    // Returns the progress file path (available after a successful launch()).
    const std::string &progress_path() const { return progress_path_; }

    // Returns the expanded output path used for the current job.
    const std::string &output_path() const { return output_path_; }

    // Spawns rizzytos-worker as a detached process.
    // Returns true on success.
    bool launch(const std::string    &input_path,
                const PluginSettings &settings,
                const OBSEncoderInfo &encoder_info);

private:
    std::string progress_path_;
    std::string output_path_;

    std::string find_worker_binary() const;
    std::string find_ffmpeg_binary() const;
};
