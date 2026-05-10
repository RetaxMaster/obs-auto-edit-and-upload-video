#include <cassert>
#include <cstdio>
#include <stdexcept>
#include "args.h"

static void test_full_args()
{
    const char *argv[] = {
        "worker",
        "--input",    "/tmp/rec.mkv",
        "--intro",    "/tmp/intro.mp4",
        "--outro",    "/tmp/outro.mp4",
        "--output",   "/tmp/out.mp4",
        "--ffmpeg",   "/usr/bin/ffmpeg",
        "--vcodec",   "h264",
        "--encoder",  "software",
        "--bitrate",  "8000",
        "--progress", "/tmp/prog.txt",
        "--width",    "1920",
        "--height",   "1080",
        "--format",   "mp4"
    };
    int argc = 25;
    WorkerArgs args = parse_args(argc, const_cast<char **>(argv));
    assert(args.input      == "/tmp/rec.mkv");
    assert(args.intro      == "/tmp/intro.mp4");
    assert(args.outro      == "/tmp/outro.mp4");
    assert(args.output     == "/tmp/out.mp4");
    assert(args.ffmpeg     == "/usr/bin/ffmpeg");
    assert(args.vcodec     == "h264");
    assert(args.encoder    == "software");
    assert(args.bitrate    == 8000);
    assert(args.progress   == "/tmp/prog.txt");
    assert(args.out_width  == 1920);
    assert(args.out_height == 1080);
    assert(args.out_format == "mp4");
    printf("test_full_args: PASS\n");
}

static void test_optional_intro_outro()
{
    const char *argv[] = {
        "worker",
        "--input",    "/tmp/rec.mkv",
        "--output",   "/tmp/out.mp4",
        "--ffmpeg",   "/usr/bin/ffmpeg",
        "--vcodec",   "h264",
        "--encoder",  "nvenc",
        "--bitrate",  "6000",
        "--progress", "/tmp/p.txt",
        "--width",    "1280",
        "--height",   "720",
        "--format",   "mkv"
    };
    int argc = 21;
    WorkerArgs args = parse_args(argc, const_cast<char **>(argv));
    assert(args.intro.empty());
    assert(args.outro.empty());
    assert(args.bitrate    == 6000);
    assert(args.out_width  == 1280);
    assert(args.out_height == 720);
    assert(args.out_format == "mkv");
    printf("test_optional_intro_outro: PASS\n");
}

static void test_missing_required_throws()
{
    const char *argv[] = {"worker", "--input", "/tmp/rec.mkv"};
    int argc = 3;
    bool threw = false;
    try {
        parse_args(argc, const_cast<char **>(argv));
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    assert(threw);
    printf("test_missing_required_throws: PASS\n");
}

static void test_resolution_and_format_required()
{
    // Missing --width should throw
    const char *argv[] = {
        "worker",
        "--input",    "/tmp/rec.mkv",
        "--output",   "/tmp/out.mp4",
        "--ffmpeg",   "/usr/bin/ffmpeg",
        "--vcodec",   "h264",
        "--encoder",  "software",
        "--bitrate",  "8000",
        "--progress", "/tmp/p.txt",
        "--height",   "1080",
        "--format",   "mp4"
    };
    int argc = 19;
    bool threw = false;
    try {
        parse_args(argc, const_cast<char **>(argv));
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    assert(threw);
    printf("test_resolution_and_format_required: PASS\n");
}

int main()
{
    test_full_args();
    test_optional_intro_outro();
    test_missing_required_throws();
    test_resolution_and_format_required();
    printf("All arg tests passed.\n");
    return 0;
}
