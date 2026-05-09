#pragma once
#include <string>

struct PluginSettings {
    std::string output_dir;
    std::string output_name_template = "rizzytos_%Y-%m-%d_%H-%M-%S";
    std::string intro_path;
    std::string outro_path;
};

// Load from JSON file. Returns defaults if file does not exist.
PluginSettings settings_load(const char *config_path);

// Save to JSON file (overwrites).
void settings_save(const PluginSettings &s, const char *config_path);
