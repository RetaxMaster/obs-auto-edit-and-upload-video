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
    const char *res      = obs_data_get_string(data, "output_resolution");
    const char *fmt      = obs_data_get_string(data, "output_format");

    if (out_dir  && *out_dir)  s.output_dir           = out_dir;
    if (out_name && *out_name) s.output_name_template = out_name;
    if (intro    && *intro)    s.intro_path           = intro;
    if (outro    && *outro)    s.outro_path           = outro;
    if (res      && *res)      s.output_resolution    = res;
    if (fmt      && *fmt)      s.output_format        = fmt;

    obs_data_release(data);
    return s;
}

void settings_save(const PluginSettings &s, const char *config_path)
{
    // Load first so we don't wipe YouTube keys that live in the same file.
    obs_data_t *data = obs_data_create_from_json_file(config_path);
    if (!data) data = obs_data_create();

    obs_data_set_string(data, "output_dir",           s.output_dir.c_str());
    obs_data_set_string(data, "output_name_template", s.output_name_template.c_str());
    obs_data_set_string(data, "intro_path",           s.intro_path.c_str());
    obs_data_set_string(data, "outro_path",           s.outro_path.c_str());
    obs_data_set_string(data, "output_resolution",    s.output_resolution.c_str());
    obs_data_set_string(data, "output_format",        s.output_format.c_str());

    obs_data_save_json(data, config_path);
    obs_data_release(data);
}

YouTubeSettings youtube_settings_load(const char *config_path)
{
    YouTubeSettings ys;
    obs_data_t *data = obs_data_create_from_json_file(config_path);
    if (!data) return ys;

    obs_data_t *yt = obs_data_get_obj(data, "youtube");
    if (yt) {
        ys.upload_enabled     = obs_data_get_bool(yt, "upload_enabled");
        const char *privacy   = obs_data_get_string(yt, "privacy");
        const char *title     = obs_data_get_string(yt, "title");
        const char *desc      = obs_data_get_string(yt, "description");
        if (privacy && *privacy) ys.privacy     = privacy;
        if (title   && *title)   ys.title       = title;
        if (desc    && *desc)    ys.description = desc;
        obs_data_release(yt);
    }

    obs_data_release(data);
    return ys;
}

void youtube_settings_save(const YouTubeSettings &ys, const char *config_path)
{
    // Load first so we don't wipe plugin settings that live in the same file.
    obs_data_t *data = obs_data_create_from_json_file(config_path);
    if (!data) data = obs_data_create();

    obs_data_t *yt = obs_data_create();
    obs_data_set_bool(yt,   "upload_enabled", ys.upload_enabled);
    obs_data_set_string(yt, "privacy",        ys.privacy.c_str());
    obs_data_set_string(yt, "title",          ys.title.c_str());
    obs_data_set_string(yt, "description",    ys.description.c_str());
    obs_data_set_obj(data, "youtube", yt);
    obs_data_release(yt);

    obs_data_save_json(data, config_path);
    obs_data_release(data);
}
