#pragma once
#include <string>

struct PluginSettings {
    std::string output_dir;
    std::string output_name_template = "rizzytos_%Y-%m-%d_%H-%M-%S";
    std::string intro_path;
    std::string outro_path;
    std::string output_resolution = "1080p"; // "720p" | "1080p" | "2k" | "4k"
    std::string output_format     = "mkv";   // "mkv" | "mp4"
};

struct YouTubeSettings {
    bool        upload_enabled = false;
    std::string privacy        = "private"; // "private" | "public"
    std::string title;
    std::string description =
        "Mira mis streams en https://www.twitch.tv/nansulli \xF0\x9F\x92\x96";
};

PluginSettings  settings_load(const char *config_path);
void            settings_save(const PluginSettings &s, const char *config_path);

YouTubeSettings youtube_settings_load(const char *config_path);
void            youtube_settings_save(const YouTubeSettings &ys, const char *config_path);
