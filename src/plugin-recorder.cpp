#include "plugin-recorder.h"
#include <obs-frontend-api.h>
#include <obs.h>
#include <ctime>
#include <cstring>

void recorder_start_our_recording(bool &our_recording_active)
{
    if (obs_frontend_recording_active()) return;
    our_recording_active = true;
    obs_frontend_recording_start();
}

void recorder_stop_our_recording()
{
    obs_frontend_recording_stop();
}

std::string recorder_get_last_recording_path()
{
    char *path = obs_frontend_get_last_recording();
    std::string result = path ? path : "";
    bfree(path);
    return result;
}

static std::string map_obs_vcodec(const char *id)
{
    if (!id) return "h264";
    std::string sid = id;
    if (sid.find("hevc") != std::string::npos ||
        sid.find("h265") != std::string::npos)
        return "hevc";
    if (sid.find("av1") != std::string::npos)
        return "av1";
    return "h264";
}

static std::string map_obs_encoder(const char *id)
{
    if (!id) return "software";
    std::string sid = id;
    if (sid == "jim_nvenc"      || sid == "ffmpeg_nvenc" ||
        sid == "jim_hevc_nvenc" || sid == "ffmpeg_hevc_nvenc")
        return "nvenc";
    if (sid.rfind("obs_qsv", 0) == 0)              return "qsv";
    if (sid.rfind("amd_amf",  0) == 0)              return "amf";
    if (sid.rfind("com.apple.videotoolbox", 0) == 0) return "videotoolbox";
    return "software";
}

OBSEncoderInfo recorder_read_encoder_info(const std::string &recording_path)
{
    OBSEncoderInfo info;

    // Derive extension from the recording file path
    size_t dot = recording_path.rfind('.');
    if (dot != std::string::npos)
        info.ext = recording_path.substr(dot + 1);
    if (info.ext.empty()) info.ext = "mp4";

    obs_output_t *output = obs_frontend_get_recording_output();
    if (!output) return info;

    obs_encoder_t *enc = obs_output_get_video_encoder(output);
    if (!enc) { obs_output_release(output); return info; }

    const char *enc_id = obs_encoder_get_id(enc);
    info.vcodec  = map_obs_vcodec(enc_id);
    info.encoder = map_obs_encoder(enc_id);

    obs_data_t *enc_settings = obs_encoder_get_settings(enc);
    if (enc_settings) {
        long long br = obs_data_get_int(enc_settings, "bitrate");
        if (br > 0) info.bitrate_kbps = static_cast<int>(br);
        obs_data_release(enc_settings);
    }

    obs_output_release(output);
    return info;
}

std::string recorder_expand_template(const std::string &tmpl)
{
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    char buf[512];
    strftime(buf, sizeof(buf), tmpl.c_str(), tm_info);
    return buf;
}
