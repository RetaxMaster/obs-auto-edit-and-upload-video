#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "concat.h"

static void test_three_inputs_nvenc_h264()
{
    FfmpegSpec spec;
    spec.ffmpeg  = "/usr/bin/ffmpeg";
    spec.intro   = "/tmp/intro.mp4";
    spec.input   = "/tmp/rec.mkv";
    spec.outro   = "/tmp/outro.mp4";
    spec.output  = "/tmp/out.mp4";
    spec.vcodec  = "h264";
    spec.encoder = "nvenc";
    spec.bitrate = 8000;
    // out_width/height/format not set: legacy behaviour, no forced scale/format

    auto args = build_ffmpeg_args(spec);

    int input_count = 0;
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == "-i") input_count++;
    assert(input_count == 3);

    bool found_encoder = false;
    for (const auto &a : args)
        if (a == "h264_nvenc") { found_encoder = true; break; }
    assert(found_encoder);

    bool found_concat = false;
    for (const auto &a : args)
        if (a.find("concat=n=3") != std::string::npos) { found_concat = true; break; }
    assert(found_concat);

    printf("test_three_inputs_nvenc_h264: PASS\n");
}

static void test_input_only_software_hevc()
{
    FfmpegSpec spec;
    spec.ffmpeg  = "/usr/bin/ffmpeg";
    spec.input   = "/tmp/rec.mkv";
    spec.output  = "/tmp/out.mp4";
    spec.vcodec  = "hevc";
    spec.encoder = "software";
    spec.bitrate = 4000;

    auto args = build_ffmpeg_args(spec);

    int input_count = 0;
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == "-i") input_count++;
    assert(input_count == 1);

    bool found_encoder = false;
    for (const auto &a : args)
        if (a == "libx265") { found_encoder = true; break; }
    assert(found_encoder);

    bool found_concat = false;
    for (const auto &a : args)
        if (a.find("concat") != std::string::npos) { found_concat = true; break; }
    assert(!found_concat);

    printf("test_input_only_software_hevc: PASS\n");
}

static void test_intro_only()
{
    FfmpegSpec spec;
    spec.ffmpeg  = "/usr/bin/ffmpeg";
    spec.intro   = "/tmp/intro.mp4";
    spec.input   = "/tmp/rec.mkv";
    spec.output  = "/tmp/out.mkv";
    spec.vcodec  = "h264";
    spec.encoder = "software";
    spec.bitrate = 6000;

    auto args = build_ffmpeg_args(spec);

    int input_count = 0;
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == "-i") input_count++;
    assert(input_count == 2);

    bool found_concat = false;
    for (const auto &a : args)
        if (a.find("concat=n=2") != std::string::npos) { found_concat = true; break; }
    assert(found_concat);

    printf("test_intro_only: PASS\n");
}

static void test_resolution_scaling_applied()
{
    FfmpegSpec spec;
    spec.ffmpeg     = "/usr/bin/ffmpeg";
    spec.intro      = "/tmp/intro.mp4";
    spec.input      = "/tmp/rec.mkv";
    spec.outro      = "/tmp/outro.mp4";
    spec.output     = "/tmp/out.mp4";
    spec.vcodec     = "h264";
    spec.encoder    = "software";
    spec.bitrate    = 8000;
    spec.out_width  = 1280;
    spec.out_height = 720;
    spec.out_format = "mp4";

    auto args = build_ffmpeg_args(spec);

    bool found_scale = false;
    for (const auto &a : args)
        if (a.find("scale=1280:720") != std::string::npos) { found_scale = true; break; }
    assert(found_scale);

    bool found_f = false;
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == "-f" && args[i+1] == "mp4") { found_f = true; break; }
    assert(found_f);

    printf("test_resolution_scaling_applied: PASS\n");
}

static void test_mkv_format_flag()
{
    FfmpegSpec spec;
    spec.ffmpeg     = "/usr/bin/ffmpeg";
    spec.input      = "/tmp/rec.mkv";
    spec.output     = "/tmp/out.mkv";
    spec.vcodec     = "h264";
    spec.encoder    = "software";
    spec.bitrate    = 8000;
    spec.out_width  = 1920;
    spec.out_height = 1080;
    spec.out_format = "mkv";

    auto args = build_ffmpeg_args(spec);

    bool found_f = false;
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == "-f" && args[i+1] == "matroska") { found_f = true; break; }
    assert(found_f);

    printf("test_mkv_format_flag: PASS\n");
}

int main()
{
    test_three_inputs_nvenc_h264();
    test_input_only_software_hevc();
    test_intro_only();
    test_resolution_scaling_applied();
    test_mkv_format_flag();
    printf("All concat tests passed.\n");
    return 0;
}
