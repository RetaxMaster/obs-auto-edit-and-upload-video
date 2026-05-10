#include <cstdio>
#include <cstring>
#include "args.h"
#include "concat.h"
#include "progress.h"

#ifdef _WIN32
#include <sys/stat.h>
static bool dir_exists(const std::string &output_path)
{
    size_t sep = output_path.find_last_of("/\\");
    if (sep == std::string::npos) return true;
    std::string dir = output_path.substr(0, sep);
    struct _stat st;
    return _stat(dir.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR);
}
#else
#include <sys/stat.h>
static bool dir_exists(const std::string &output_path)
{
    size_t sep = output_path.find_last_of("/\\");
    if (sep == std::string::npos) return true;
    std::string dir = output_path.substr(0, sep);
    struct stat st;
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

int main(int argc, char *argv[])
{
    WorkerArgs args;
    try {
        args = parse_args(argc, argv);
    } catch (const std::invalid_argument &e) {
        fprintf(stderr, "rizzytos-worker: %s\n", e.what());
        return 1;
    }

    if (!dir_exists(args.output)) {
        ProgressWriter pw(args.progress);
        pw.write_error("Output directory does not exist");
        return 3;
    }

    FfmpegSpec spec;
    spec.ffmpeg      = args.ffmpeg;
    spec.intro       = args.intro;
    spec.input       = args.input;
    spec.outro       = args.outro;
    spec.output      = args.output;
    spec.vcodec      = args.vcodec;
    spec.encoder     = args.encoder;
    spec.bitrate     = args.bitrate;
    spec.out_width   = args.out_width;
    spec.out_height  = args.out_height;
    spec.out_format  = args.out_format;

    return run_ffmpeg(spec, args.progress);
}
