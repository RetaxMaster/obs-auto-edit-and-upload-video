#include "plugin-settings.h"
#include <obs-data.h>

PluginSettings settings_load(const char *config_path)
{
    PluginSettings s;
    obs_data_t *data = obs_data_create_from_json_file(config_path);
    if (!data) return s;

    const char *out_dir  = obs_data_get_string(data, "output_dir");
    const char *out_name = obs_data_get_string(data, "output_name_template");
    const char *intro    = obs_data_get_string(data, "intro_path");
    const char *outro    = obs_data_get_string(data, "outro_path");

    if (out_dir  && *out_dir)  s.output_dir           = out_dir;
    if (out_name && *out_name) s.output_name_template = out_name;
    if (intro    && *intro)    s.intro_path           = intro;
    if (outro    && *outro)    s.outro_path           = outro;

    obs_data_release(data);
    return s;
}

void settings_save(const PluginSettings &s, const char *config_path)
{
    obs_data_t *data = obs_data_create();
    obs_data_set_string(data, "output_dir",           s.output_dir.c_str());
    obs_data_set_string(data, "output_name_template", s.output_name_template.c_str());
    obs_data_set_string(data, "intro_path",           s.intro_path.c_str());
    obs_data_set_string(data, "outro_path",           s.outro_path.c_str());
    obs_data_save_json(data, config_path);
    obs_data_release(data);
}
