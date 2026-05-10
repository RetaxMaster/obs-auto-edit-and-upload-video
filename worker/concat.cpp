#include "concat.h"
#include "progress.h"
#include <sstream>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
#define POPEN  _popen
#define PCLOSE _pclose
#else
#define POPEN  popen
#define PCLOSE pclose
#endif

static std::string ffmpeg_encoder_name(const std::string &vcodec,
                                        const std::string &encoder)
{
    if (encoder == "nvenc")        return vcodec == "hevc" ? "hevc_nvenc"        : "h264_nvenc";
    if (encoder == "qsv")          return vcodec == "hevc" ? "hevc_qsv"          : "h264_qsv";
    if (encoder == "amf")          return vcodec == "hevc" ? "hevc_amf"          : "h264_amf";
    if (encoder == "videotoolbox") return vcodec == "hevc" ? "hevc_videotoolbox" : "h264_videotoolbox";
    return vcodec == "hevc" ? "libx265" : "libx264";
}

// Public: used by unit tests (assumes all inputs have audio).
std::vector<std::string> build_ffmpeg_args(const FfmpegSpec &spec)
{
    std::vector<std::string> args;
    args.push_back(spec.ffmpeg);

    std::vector<std::string> inputs;
    if (!spec.intro.empty()) inputs.push_back(spec.intro);
    inputs.push_back(spec.input);
    if (!spec.outro.empty()) inputs.push_back(spec.outro);

    for (const auto &inp : inputs) {
        args.push_back("-i");
        args.push_back(inp);
    }

    int n = static_cast<int>(inputs.size());
    bool do_scale = (spec.out_width > 0 && spec.out_height > 0);

    if (n > 1) {
        std::ostringstream filter;

        if (do_scale) {
            for (int i = 0; i < n; i++)
                filter << "[" << i << ":v]scale=" << spec.out_width << ":"
                       << spec.out_height << ",setsar=1[v_" << i << "];";
        }

        auto vref = [&](int i) -> std::string {
            return do_scale ? ("[v_" + std::to_string(i) + "]")
                            : ("[" + std::to_string(i) + ":v]");
        };

        for (int i = 0; i < n; i++)
            filter << vref(i) << "[" << i << ":a]";
        filter << "concat=n=" << n << ":v=1:a=1[v][a]";

        args.push_back("-filter_complex");
        args.push_back(filter.str());
        args.push_back("-map"); args.push_back("[v]");
        args.push_back("-map"); args.push_back("[a]");
    } else if (do_scale) {
        args.push_back("-vf");
        args.push_back("scale=" + std::to_string(spec.out_width) + ":" +
                        std::to_string(spec.out_height) + ",setsar=1");
    }

    args.push_back("-c:v");
    args.push_back(ffmpeg_encoder_name(spec.vcodec, spec.encoder));
    args.push_back("-b:v");
    args.push_back(std::to_string(spec.bitrate) + "k");
    args.push_back("-progress");
    args.push_back("pipe:1");
    args.push_back("-nostats");
    args.push_back("-y");

    if (!spec.out_format.empty()) {
        args.push_back("-f");
        args.push_back(spec.out_format == "mkv" ? "matroska" : spec.out_format);
    }

    args.push_back(spec.output);
    return args;
}

// ── Internal probe ─────────────────────────────────────────────────────────────

struct FileInfo {
    double duration;   // seconds, -1.0 if unknown
    bool   has_audio;
    int    width;      // 0 if unknown
    int    height;     // 0 if unknown
};

static FileInfo probe_file(const std::string &ffmpeg_path, const std::string &file)
{
    FileInfo info = { -1.0, false, 0, 0 };

    // On Windows, cmd.exe strips the leading " and last " when the command
    // starts with ". Wrapping in an extra outer pair works around this.
#ifdef _WIN32
    std::string cmd = "\"\"" + ffmpeg_path + "\" -i \"" + file + "\" 2>&1\"";
#else
    std::string cmd = "\"" + ffmpeg_path + "\" -i \"" + file + "\" 2>&1";
#endif

    FILE *fp = POPEN(cmd.c_str(), "r");
    if (!fp) return info;

    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        if (info.duration < 0.0) {
            double d = parse_duration_seconds(buf);
            if (d >= 0.0) info.duration = d;
        }
        if (!info.has_audio && strstr(buf, " Audio: "))
            info.has_audio = true;
        if (info.width == 0 && strstr(buf, " Video: ")) {
            const char *p = buf;
            while (*p) {
                int w = 0, h = 0;
                if (sscanf(p, "%dx%d", &w, &h) == 2 &&
                    w > 64 && h > 64 && w <= 7680 && h <= 4320) {
                    info.width  = w;
                    info.height = h;
                    break;
                }
                p++;
            }
        }
    }
    PCLOSE(fp);
    return info;
}

// Parse "out_time_ms=N" line from ffmpeg -progress output (microseconds → seconds).
static double parse_out_time_ms(const char *line)
{
    if (strncmp(line, "out_time_ms=", 12) != 0) return -1.0;
    try {
        return std::stod(line + 12) / 1e6;
    } catch (...) {
        return -1.0;
    }
}

// ── run_ffmpeg ─────────────────────────────────────────────────────────────────

int run_ffmpeg(const FfmpegSpec &spec, const std::string &progress_path)
{
    ProgressWriter pw(progress_path);
    pw.write(0);

    // Ordered input list
    std::vector<std::string> inputs;
    if (!spec.intro.empty()) inputs.push_back(spec.intro);
    inputs.push_back(spec.input);
    if (!spec.outro.empty()) inputs.push_back(spec.outro);

    int n = static_cast<int>(inputs.size());

    // Probe every input for duration and audio stream presence
    std::vector<FileInfo> infos;
    double total_duration = 0.0;
    for (const auto &f : inputs) {
        FileInfo fi = probe_file(spec.ffmpeg, f);
        infos.push_back(fi);
        if (fi.duration > 0.0) total_duration += fi.duration;
    }

    bool any_audio = false;
    bool all_audio = true;
    for (const auto &fi : infos) {
        if ( fi.has_audio) any_audio = true;
        if (!fi.has_audio) all_audio = false;
    }

    // Use user-specified output resolution; fall back to recording's detected
    // resolution if not specified (backward-compat for callers that don't set out_width).
    int target_w = spec.out_width;
    int target_h = spec.out_height;
    if (target_w == 0) {
        for (int i = 0; i < n; i++) {
            if (inputs[i] == spec.input && infos[i].width > 0) {
                target_w = infos[i].width;
                target_h = infos[i].height;
                break;
            }
        }
        if (target_w == 0) {
            for (const auto &fi : infos) {
                if (fi.width > 0) { target_w = fi.width; target_h = fi.height; break; }
            }
        }
    }
    // When a target resolution is set, always scale all inputs so mixed-format
    // inputs (e.g. MP4 intro + MKV recording) are brought to the same dimensions.
    bool need_scale = (target_w > 0);

    // Build ffmpeg argument list with audio-aware filtergraph
    std::vector<std::string> args;
    args.push_back(spec.ffmpeg);

    for (const auto &inp : inputs) {
        args.push_back("-i");
        args.push_back(inp);
    }

    if (n > 1) {
        std::ostringstream filter;

        // Normalize video resolutions when inputs differ (e.g. 1080p intro + 1440p recording)
        if (need_scale) {
            for (int i = 0; i < n; i++)
                filter << "[" << i << ":v]scale=" << target_w << ":" << target_h
                       << ",setsar=1[v_" << i << "];";
        }
        // Returns the video pad label for input i (scaled or raw)
        auto vref = [&](int i) -> std::string {
            return need_scale ? ("[v_" + std::to_string(i) + "]")
                              : ("[" + std::to_string(i) + ":v]");
        };

        if (all_audio) {
            for (int i = 0; i < n; i++)
                filter << vref(i) << "[" << i << ":a]";
            filter << "concat=n=" << n << ":v=1:a=1[v][a]";

            args.push_back("-filter_complex"); args.push_back(filter.str());
            args.push_back("-map"); args.push_back("[v]");
            args.push_back("-map"); args.push_back("[a]");

        } else if (any_audio) {
            // Mixed: some inputs lack audio — normalise real audio and fill
            // missing tracks with anullsrc so concat gets consistent streams.
            for (int i = 0; i < n; i++) {
                if (infos[i].has_audio) {
                    filter << "[" << i << ":a]"
                           << "aformat=sample_fmts=fltp"
                           << ":sample_rates=48000"
                           << ":channel_layouts=stereo"
                           << "[a_" << i << "];";
                } else {
                    // Bound the silent stream to the video duration so concat
                    // doesn't stall waiting for EOF from an infinite source.
                    filter << "anullsrc=r=48000:cl=stereo";
                    if (infos[i].duration > 0.0)
                        filter << ":d=" << infos[i].duration;
                    filter << ",aformat=sample_fmts=fltp"
                           << ":sample_rates=48000"
                           << ":channel_layouts=stereo"
                           << "[a_" << i << "];";
                }
            }
            for (int i = 0; i < n; i++)
                filter << vref(i) << "[a_" << i << "]";
            filter << "concat=n=" << n << ":v=1:a=1[v][a]";

            args.push_back("-filter_complex"); args.push_back(filter.str());
            args.push_back("-map"); args.push_back("[v]");
            args.push_back("-map"); args.push_back("[a]");

        } else {
            // No input has audio — video-only concat
            for (int i = 0; i < n; i++)
                filter << vref(i);
            filter << "concat=n=" << n << ":v=1:a=0[v]";

            args.push_back("-filter_complex"); args.push_back(filter.str());
            args.push_back("-map"); args.push_back("[v]");
            args.push_back("-an");
        }
    }

    args.push_back("-c:v");
    args.push_back(ffmpeg_encoder_name(spec.vcodec, spec.encoder));
    args.push_back("-b:v");
    args.push_back(std::to_string(spec.bitrate) + "k");
    args.push_back("-progress");
    args.push_back("pipe:1");
    args.push_back("-nostats");
    args.push_back("-y");

    if (!spec.out_format.empty()) {
        args.push_back("-f");
        args.push_back(spec.out_format == "mkv" ? "matroska" : spec.out_format);
    }

    args.push_back(spec.output);

    // Build shell command
    std::ostringstream cmd;
#ifdef _WIN32
    cmd << "\"";  // outer quote: cmd.exe strips first/last " when command starts with "
#endif
    for (const auto &a : args)
        cmd << "\"" << a << "\" ";
    cmd << "2>&1";
#ifdef _WIN32
    cmd << "\"";
#endif

    // Write the ffmpeg command to a debug log; keep it open to capture all output
    std::ofstream dlog(progress_path + ".log");
    if (dlog.is_open())
        dlog << "ffmpeg_cmd: " << cmd.str() << "\nffmpeg_output:\n";

    FILE *fp = POPEN(cmd.str().c_str(), "r");
    if (!fp) {
        pw.write_error(std::string("popen failed (errno=")
                       + std::to_string(errno) + "): " + cmd.str());
        return 2;
    }

    char buf[512];
    std::string last_nonempty_line;
    std::string last_error_line;
    while (fgets(buf, sizeof(buf), fp)) {
        if (dlog.is_open()) dlog << buf;

        double current = parse_out_time_ms(buf);
        if (current >= 0.0 && total_duration > 0.0) {
            int pct = static_cast<int>((current / total_duration) * 100.0);
            if (pct > 100) pct = 100;
            pw.write(pct);
        }
        if (buf[0] != '\n' && strlen(buf) > 1) {
            last_nonempty_line = buf;
            // Track the most recent line that looks like an error for display
            if (strstr(buf, "Error") || strstr(buf, "Invalid") || strstr(buf, "No such"))
                last_error_line = buf;
        }
    }

    int ret = PCLOSE(fp);
    if (ret != 0) {
        std::string msg = !last_error_line.empty() ? last_error_line
                        : (!last_nonempty_line.empty() ? last_nonempty_line : "ffmpeg failed");
        pw.write_error(msg);
        return 2;
    }

    pw.write(100);
    return 0;
}
