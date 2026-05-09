#include "concat.h"
#include "progress.h"
#include <sstream>
#include <cstdio>
#include <cstring>

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

std::vector<std::string> build_ffmpeg_args(const FfmpegSpec &spec)
{
    std::vector<std::string> args;
    args.push_back(spec.ffmpeg);

    // Collect inputs in order: intro (optional), main recording, outro (optional)
    std::vector<std::string> inputs;
    if (!spec.intro.empty()) inputs.push_back(spec.intro);
    inputs.push_back(spec.input);
    if (!spec.outro.empty()) inputs.push_back(spec.outro);

    for (const auto &inp : inputs) {
        args.push_back("-i");
        args.push_back(inp);
    }

    int n = static_cast<int>(inputs.size());

    if (n > 1) {
        std::ostringstream filter;
        for (int i = 0; i < n; i++)
            filter << "[" << i << ":v][" << i << ":a]";
        filter << "concat=n=" << n << ":v=1:a=1[v][a]";

        args.push_back("-filter_complex");
        args.push_back(filter.str());
        args.push_back("-map");
        args.push_back("[v]");
        args.push_back("-map");
        args.push_back("[a]");
    }

    args.push_back("-c:v");
    args.push_back(ffmpeg_encoder_name(spec.vcodec, spec.encoder));
    args.push_back("-b:v");
    args.push_back(std::to_string(spec.bitrate) + "k");
    args.push_back("-progress");
    args.push_back("pipe:1");
    args.push_back("-nostats");
    args.push_back("-y");
    args.push_back(spec.output);

    return args;
}

// Probe total duration of a single file by running "ffmpeg -i <file>".
static double probe_duration(const std::string &ffmpeg_path,
                              const std::string &file)
{
    std::string cmd = "\"" + ffmpeg_path + "\" -i \"" + file + "\" 2>&1";
    FILE *fp = POPEN(cmd.c_str(), "r");
    if (!fp) return -1.0;

    char buf[512];
    double dur = -1.0;
    while (fgets(buf, sizeof(buf), fp)) {
        double d = parse_duration_seconds(buf);
        if (d >= 0.0) { dur = d; break; }
    }
    PCLOSE(fp);
    return dur;
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

int run_ffmpeg(const FfmpegSpec &spec, const std::string &progress_path)
{
    ProgressWriter pw(progress_path);
    pw.write(0);

    // Probe total duration (sum across all inputs)
    std::vector<std::string> probe_files;
    if (!spec.intro.empty()) probe_files.push_back(spec.intro);
    probe_files.push_back(spec.input);
    if (!spec.outro.empty()) probe_files.push_back(spec.outro);

    double total_duration = 0.0;
    for (const auto &f : probe_files) {
        double d = probe_duration(spec.ffmpeg, f);
        if (d > 0.0) total_duration += d;
    }

    // Build shell command from args list
    auto args = build_ffmpeg_args(spec);
    std::ostringstream cmd;
    for (const auto &a : args)
        cmd << "\"" << a << "\" ";
    cmd << "2>&1";

    FILE *fp = POPEN(cmd.str().c_str(), "r");
    if (!fp) {
        pw.write_error("Failed to launch ffmpeg");
        return 2;
    }

    char buf[512];
    std::string last_nonempty_line;
    while (fgets(buf, sizeof(buf), fp)) {
        double current = parse_out_time_ms(buf);
        if (current >= 0.0 && total_duration > 0.0) {
            int pct = static_cast<int>((current / total_duration) * 100.0);
            if (pct > 100) pct = 100;
            pw.write(pct);
        }
        if (buf[0] != '\n' && strlen(buf) > 1)
            last_nonempty_line = buf;
    }

    int ret = PCLOSE(fp);
    if (ret != 0) {
        pw.write_error(last_nonempty_line.empty() ? "ffmpeg failed" : last_nonempty_line);
        return 2;
    }

    pw.write(100);
    return 0;
}
