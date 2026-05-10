#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>
#include "progress.h"

static void test_parse_duration_simple()
{
    double d = parse_duration_seconds("  Duration: 00:01:23.45, start: ...");
    assert(std::fabs(d - 83.45) < 0.01);
    printf("test_parse_duration_simple: PASS\n");
}

static void test_parse_duration_hours()
{
    double d = parse_duration_seconds("Duration: 01:00:00.00,");
    assert(std::fabs(d - 3600.0) < 0.01);
    printf("test_parse_duration_hours: PASS\n");
}

static void test_parse_duration_not_found()
{
    double d = parse_duration_seconds("Some other ffmpeg output line");
    assert(d < 0.0);
    printf("test_parse_duration_not_found: PASS\n");
}

static void test_progress_writer_writes_value()
{
    const char *path = "/tmp/rizzytos_test_progress.txt";
    {
        ProgressWriter pw(path);
        pw.write(42);
    }
    std::ifstream f(path);
    std::string content;
    std::getline(f, content);
    assert(content == "42");
    printf("test_progress_writer_writes_value: PASS\n");
}

static void test_progress_writer_writes_error()
{
    const char *path = "/tmp/rizzytos_test_progress_err.txt";
    {
        ProgressWriter pw(path);
        pw.write_error("ffmpeg crashed");
    }
    std::ifstream f(path);
    std::string content;
    std::getline(f, content);
    assert(content == "error: ffmpeg crashed");
    printf("test_progress_writer_writes_error: PASS\n");
}

int main()
{
    test_parse_duration_simple();
    test_parse_duration_hours();
    test_parse_duration_not_found();
    test_progress_writer_writes_value();
    test_progress_writer_writes_error();
    printf("All progress tests passed.\n");
    return 0;
}
