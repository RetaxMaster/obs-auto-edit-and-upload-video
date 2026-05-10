# OBS Plugin Improvements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add configurable output resolution/format, fix the record button re-enable bug, and add YouTube upload integration with OAuth 2.0 PKCE.

**Architecture:** Worker gains `--width`, `--height`, `--format` CLI args and always scales to the user-specified resolution with an explicit container format flag. Plugin-side gains a `processing_finished` signal that closes the button state machine, and two new Qt classes (`YouTubeAuth`, `YouTubeUploader`) that handle OAuth and resumable upload respectively. `plugin-main.cpp` orchestrates everything; the dock gains new UI sections.

**Tech Stack:** C++17, Qt6 (Widgets, Network, Core), OBS frontend API, qtkeychain (FetchContent from GitHub), FFmpeg via popen, YouTube Data API v3.

---

## File Map

| File | Action | What changes |
|------|--------|--------------|
| `worker/args.h` | Modify | Add `out_width`, `out_height`, `out_format` fields |
| `worker/args.cpp` | Modify | Parse `--width`, `--height`, `--format` as required |
| `worker/concat.h` | Modify | Add `out_width`, `out_height`, `out_format` to `FfmpegSpec` |
| `worker/concat.cpp` | Modify | Use spec dimensions for scaling, add `-f` flag |
| `worker/main.cpp` | Modify | Copy new args fields into `FfmpegSpec` |
| `tests/worker/test_args.cpp` | Modify | Update all existing tests + add new test |
| `tests/worker/test_concat.cpp` | Modify | Update all existing tests + add new test |
| `src/plugin-settings.h` | Modify | Add `output_resolution`, `output_format`; add `YouTubeSettings` struct |
| `src/plugin-settings.cpp` | Modify | Load/save new fields and `YouTubeSettings` |
| `src/plugin-launcher.cpp` | Modify | Extension from `output_format`, pass `--width/height/format` |
| `src/plugin-ui.h` | Modify | Add combos, `processing_finished` signal, YouTube UI members and methods |
| `src/plugin-ui.cpp` | Modify | Add combos, emit `processing_finished`, add YouTube UI section |
| `src/plugin-main.cpp` | Modify | Connect `processing_finished`, instantiate `YouTubeAuth`, wire upload |
| `src/youtube-auth.h` | Create | `YouTubeAuth` class declaration |
| `src/youtube-auth.cpp` | Create | OAuth 2.0 PKCE flow + token storage |
| `src/youtube-uploader.h` | Create | `YouTubeUploader` class declaration |
| `src/youtube-uploader.cpp` | Create | Resumable upload logic |
| `data/locale/en-US.ini` | Modify | Add strings for all new UI controls |
| `CMakeLists.txt` | Modify | Add Qt6::Network, qtkeychain, credential defines, new source files |

---

## Task 1: Worker — extend args for resolution and format

**Files:**
- Modify: `worker/args.h`
- Modify: `worker/args.cpp`
- Modify: `tests/worker/test_args.cpp`

- [ ] **Step 1: Add new fields to `WorkerArgs` in `worker/args.h`**

Replace the struct so it reads:

```cpp
#pragma once
#include <string>
#include <stdexcept>

struct WorkerArgs {
    std::string input;
    std::string intro;      // optional; empty = skip
    std::string outro;      // optional; empty = skip
    std::string output;
    std::string ffmpeg;
    std::string vcodec;     // "h264" | "hevc" | "av1"
    std::string encoder;    // "nvenc" | "qsv" | "amf" | "videotoolbox" | "software"
    int         bitrate;    // kbps
    std::string progress;   // path to progress file
    int         out_width;  // e.g. 1920
    int         out_height; // e.g. 1080
    std::string out_format; // "mkv" | "mp4"
};

// Parses CLI arguments into WorkerArgs.
// Throws std::invalid_argument if a required field is missing.
WorkerArgs parse_args(int argc, char *argv[]);
```

- [ ] **Step 2: Parse new required args in `worker/args.cpp`**

Replace the function body so it reads:

```cpp
#include "args.h"
#include <unordered_map>

WorkerArgs parse_args(int argc, char *argv[])
{
    std::unordered_map<std::string, std::string> kv;
    for (int i = 1; i + 1 < argc; i += 2)
        kv[argv[i]] = argv[i + 1];

    auto require = [&](const char *key) -> const std::string & {
        auto it = kv.find(key);
        if (it == kv.end())
            throw std::invalid_argument(std::string("Missing required argument: ") + key);
        return it->second;
    };

    WorkerArgs args;
    args.input      = require("--input");
    args.output     = require("--output");
    args.ffmpeg     = require("--ffmpeg");
    args.vcodec     = require("--vcodec");
    args.encoder    = require("--encoder");
    args.bitrate    = std::stoi(require("--bitrate"));
    args.progress   = require("--progress");
    args.out_width  = std::stoi(require("--width"));
    args.out_height = std::stoi(require("--height"));
    args.out_format = require("--format");

    auto it_intro = kv.find("--intro");
    if (it_intro != kv.end()) args.intro = it_intro->second;

    auto it_outro = kv.find("--outro");
    if (it_outro != kv.end()) args.outro = it_outro->second;

    return args;
}
```

- [ ] **Step 3: Update `tests/worker/test_args.cpp` — add new required args to every existing argv array and add one new test**

Replace the entire file:

```cpp
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
    int argc = 21;
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
```

- [ ] **Step 4: Build and run args tests**

```bash
cd /home/retaxmaster/projects/obs-plugintemplate
cmake -B build_worker -DWORKER_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_worker --target test_worker_args
./build_worker/test_worker_args
```

Expected output:
```
test_full_args: PASS
test_optional_intro_outro: PASS
test_missing_required_throws: PASS
test_resolution_and_format_required: PASS
All arg tests passed.
```

- [ ] **Step 5: Commit**

```bash
git add worker/args.h worker/args.cpp tests/worker/test_args.cpp
git commit -m "feat(worker): add --width, --height, --format required args"
```

---

## Task 2: Worker — FfmpegSpec, scaling, and container format

**Files:**
- Modify: `worker/concat.h`
- Modify: `worker/concat.cpp`
- Modify: `worker/main.cpp`
- Modify: `tests/worker/test_concat.cpp`

- [ ] **Step 1: Add new fields to `FfmpegSpec` in `worker/concat.h`**

```cpp
#pragma once
#include <string>
#include <vector>

struct FfmpegSpec {
    std::string ffmpeg;
    std::string intro;
    std::string input;
    std::string outro;
    std::string output;
    std::string vcodec;
    std::string encoder;
    int         bitrate;
    int         out_width  = 0;  // 0 = do not force scale
    int         out_height = 0;
    std::string out_format;      // "mkv" | "mp4" | "" = infer from extension
};

std::vector<std::string> build_ffmpeg_args(const FfmpegSpec &spec);

int run_ffmpeg(const FfmpegSpec &spec, const std::string &progress_path);
```

- [ ] **Step 2: Update `build_ffmpeg_args()` in `worker/concat.cpp` to apply scale and `-f` flag**

Find the `build_ffmpeg_args` function (lines 28–70 of the current file) and replace it entirely:

```cpp
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
        // Single input with scaling
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
```

- [ ] **Step 3: Update `run_ffmpeg()` in `worker/concat.cpp` to use spec resolution and always scale**

Find the section that detects `target_w/target_h` and the `need_scale` variable (lines 164–187 in the original) and replace just that block:

```cpp
    // Use the user-specified output resolution; fall back to recording's resolution
    // if not specified (for backward-compat with tests that don't set out_width).
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
    // When a target resolution is set, always scale so that mixed-format inputs
    // (e.g. MP4 intro + MKV recording) are all brought to the same dimensions.
    bool need_scale = (target_w > 0);
```

Then at the end of `run_ffmpeg()`, just before `args.push_back(spec.output)`, add the `-f` flag. Find the block:

```cpp
    args.push_back("-progress");
    args.push_back("pipe:1");
    args.push_back("-nostats");
    args.push_back("-y");
    args.push_back(spec.output);
```

And replace it with:

```cpp
    args.push_back("-progress");
    args.push_back("pipe:1");
    args.push_back("-nostats");
    args.push_back("-y");

    if (!spec.out_format.empty()) {
        args.push_back("-f");
        args.push_back(spec.out_format == "mkv" ? "matroska" : spec.out_format);
    }

    args.push_back(spec.output);
```

- [ ] **Step 4: Update `worker/main.cpp` to copy new fields from `WorkerArgs` to `FfmpegSpec`**

Find the block that builds `spec` (after `FfmpegSpec spec;`) and add three new lines:

```cpp
    FfmpegSpec spec;
    spec.ffmpeg     = args.ffmpeg;
    spec.intro      = args.intro;
    spec.input      = args.input;
    spec.outro      = args.outro;
    spec.output     = args.output;
    spec.vcodec     = args.vcodec;
    spec.encoder    = args.encoder;
    spec.bitrate    = args.bitrate;
    spec.out_width  = args.out_width;
    spec.out_height = args.out_height;
    spec.out_format = args.out_format;
```

- [ ] **Step 5: Update `tests/worker/test_concat.cpp` — update existing tests and add new ones**

Replace the entire file:

```cpp
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

    // scale filter must be present for all 3 inputs
    bool found_scale = false;
    for (const auto &a : args)
        if (a.find("scale=1280:720") != std::string::npos) { found_scale = true; break; }
    assert(found_scale);

    // -f mp4 must be present
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

    // mkv must map to "matroska" format flag
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
```

- [ ] **Step 6: Build and run all worker tests**

```bash
cmake --build build_worker --target test_worker_args test_worker_concat test_worker_progress
./build_worker/test_worker_args
./build_worker/test_worker_concat
./build_worker/test_worker_progress
```

Expected: all tests print PASS lines and exit 0.

- [ ] **Step 7: Commit**

```bash
git add worker/concat.h worker/concat.cpp worker/main.cpp tests/worker/test_concat.cpp
git commit -m "feat(worker): scale all inputs to user-specified resolution, add -f container flag"
```

---

## Task 3: Plugin — extend PluginSettings and add YouTubeSettings

**Files:**
- Modify: `src/plugin-settings.h`
- Modify: `src/plugin-settings.cpp`

- [ ] **Step 1: Update `src/plugin-settings.h`**

Replace the file:

```cpp
#pragma once
#include <string>

struct PluginSettings {
    std::string output_dir;
    std::string output_name_template = "rizzytos_%Y-%m-%d_%H-%M-%S";
    std::string intro_path;
    std::string outro_path;
    std::string output_resolution = "1080p"; // "720p" | "1080p" | "2k" | "4k"
    std::string output_format     = "mkv";   // "mkv" | "mp4"
};

struct YouTubeSettings {
    bool        upload_enabled = false;
    std::string privacy        = "private"; // "private" | "public"
    std::string title;
    std::string description =
        "Mira mis streams en https://www.twitch.tv/nansulli \xF0\x9F\x92\x96";
};

PluginSettings  settings_load(const char *config_path);
void            settings_save(const PluginSettings &s, const char *config_path);

YouTubeSettings youtube_settings_load(const char *config_path);
void            youtube_settings_save(const YouTubeSettings &ys, const char *config_path);
```

Note: `\xF0\x9F\x92\x96` is the UTF-8 encoding of 💖. It avoids source-file encoding issues.

- [ ] **Step 2: Update `src/plugin-settings.cpp`**

Replace the file:

```cpp
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
        ys.upload_enabled = obs_data_get_bool(yt, "upload_enabled");
        const char *privacy = obs_data_get_string(yt, "privacy");
        const char *title   = obs_data_get_string(yt, "title");
        const char *desc    = obs_data_get_string(yt, "description");
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
```

Note: both `settings_save` and `youtube_settings_save` load the existing JSON first, then overwrite only their keys. This prevents each save from wiping the other's data.

- [ ] **Step 3: Commit**

```bash
git add src/plugin-settings.h src/plugin-settings.cpp
git commit -m "feat(settings): add output_resolution, output_format, YouTubeSettings"
```

---

## Task 4: Launcher — derive extension from output_format, pass new args

**Files:**
- Modify: `src/plugin-launcher.cpp`

- [ ] **Step 1: Update `PluginLauncher::launch()` in `src/plugin-launcher.cpp`**

Two changes inside `launch()`:

**Change A** — The output file extension must come from `settings.output_format`, not from `encoder_info.ext`. Find the line:

```cpp
                 + "." + encoder_info.ext;
```

Replace it with:

```cpp
                 + "." + settings.output_format;
```

**Change B** — After the line that appends `--progress`, add the resolution and format args. Find:

```cpp
    if (!settings.intro_path.empty())
        args << "--intro" << QString::fromStdString(settings.intro_path);
```

And insert before it:

```cpp
    // Resolve resolution string to pixel dimensions
    int out_w = 1920, out_h = 1080;
    if      (settings.output_resolution == "720p")  { out_w = 1280; out_h = 720;  }
    else if (settings.output_resolution == "1080p") { out_w = 1920; out_h = 1080; }
    else if (settings.output_resolution == "2k")    { out_w = 2560; out_h = 1440; }
    else if (settings.output_resolution == "4k")    { out_w = 3840; out_h = 2160; }

    args << "--width"  << QString::number(out_w)
         << "--height" << QString::number(out_h)
         << "--format" << QString::fromStdString(settings.output_format);

```

- [ ] **Step 2: Commit**

```bash
git add src/plugin-launcher.cpp
git commit -m "feat(launcher): use output_format for file extension; pass --width/height/format to worker"
```

---

## Task 5: Dock UI — resolution/format combos + processing_finished signal

**Files:**
- Modify: `src/plugin-ui.h`
- Modify: `src/plugin-ui.cpp`

- [ ] **Step 1: Update `src/plugin-ui.h`**

Replace the file:

```cpp
#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QFrame>
#include "plugin-settings.h"

class AutoEditDock : public QWidget {
    Q_OBJECT
public:
    explicit AutoEditDock(QWidget *parent = nullptr);

    PluginSettings get_settings() const;
    void set_settings(const PluginSettings &s);

    void start_progress(const std::string &progress_file_path,
                        const std::string &output_path);
    void stop_progress();

    void set_action_button_state(bool recording, bool enabled);

signals:
    void settings_changed(const PluginSettings &s);
    void record_requested();
    void processing_finished();

private slots:
    void browse_output_dir();
    void browse_intro();
    void browse_outro();
    void on_poll_timer();

private:
    QPushButton  *action_btn_       = nullptr;
    QLineEdit    *output_dir_edit_  = nullptr;
    QLineEdit    *output_name_edit_ = nullptr;
    QLineEdit    *intro_edit_       = nullptr;
    QLineEdit    *outro_edit_       = nullptr;
    QComboBox    *resolution_combo_ = nullptr;
    QComboBox    *format_combo_     = nullptr;
    QProgressBar *progress_bar_     = nullptr;
    QLabel       *status_label_     = nullptr;
    QTimer       *poll_timer_       = nullptr;

    std::string  progress_file_;
    std::string  output_path_;
    QDateTime    last_progress_change_;
    int          last_progress_value_ = -1;
};

QPushButton *inject_record_button(QWidget *main_window);
```

- [ ] **Step 2: Add the two combo boxes to the constructor in `src/plugin-ui.cpp`**

In the constructor, find the block that adds the output name template row (after `make_browse_row` calls). That block ends with `layout->addLayout(row);` for the name row. Right after that (before the first separator), add:

```cpp
    // Resolution selector
    {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(obs_module_text("RizzyTos.Settings.OutputResolution"), this);
        lbl->setFixedWidth(100);
        resolution_combo_ = new QComboBox(this);
        resolution_combo_->addItem("720p (1280\xC3\x97""720)",  QString("720p"));
        resolution_combo_->addItem("1080p (1920\xC3\x97""1080)", QString("1080p"));
        resolution_combo_->addItem("2K (2560\xC3\x97""1440)",   QString("2k"));
        resolution_combo_->addItem("4K (3840\xC3\x97""2160)",   QString("4k"));
        resolution_combo_->setCurrentIndex(1); // default 1080p
        row->addWidget(lbl);
        row->addWidget(resolution_combo_);
        layout->addLayout(row);
        connect(resolution_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { emit settings_changed(get_settings()); });
    }

    // Format selector
    {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(obs_module_text("RizzyTos.Settings.OutputFormat"), this);
        lbl->setFixedWidth(100);
        format_combo_ = new QComboBox(this);
        format_combo_->addItem("MKV (.mkv)", QString("mkv"));
        format_combo_->addItem("MP4 (.mp4)", QString("mp4"));
        format_combo_->setCurrentIndex(0); // default MKV
        row->addWidget(lbl);
        row->addWidget(format_combo_);
        layout->addLayout(row);
        connect(format_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { emit settings_changed(get_settings()); });
    }
```

Note: `\xC3\x97` is the UTF-8 encoding of the × character.

- [ ] **Step 3: Update `get_settings()` in `src/plugin-ui.cpp` to include resolution and format**

Replace the function:

```cpp
PluginSettings AutoEditDock::get_settings() const
{
    PluginSettings s;
    s.output_dir           = output_dir_edit_->text().toStdString();
    s.output_name_template = output_name_edit_->text().toStdString();
    s.intro_path           = intro_edit_->text().toStdString();
    s.outro_path           = outro_edit_->text().toStdString();
    s.output_resolution    = resolution_combo_->currentData().toString().toStdString();
    s.output_format        = format_combo_->currentData().toString().toStdString();
    return s;
}
```

- [ ] **Step 4: Update `set_settings()` in `src/plugin-ui.cpp` to populate the combos**

At the end of the `set_settings()` function (before the closing brace), add:

```cpp
    // Sync resolution combo
    for (int i = 0; i < resolution_combo_->count(); ++i) {
        if (resolution_combo_->itemData(i).toString().toStdString() == s.output_resolution) {
            resolution_combo_->setCurrentIndex(i);
            break;
        }
    }

    // Sync format combo
    for (int i = 0; i < format_combo_->count(); ++i) {
        if (format_combo_->itemData(i).toString().toStdString() == s.output_format) {
            format_combo_->setCurrentIndex(i);
            break;
        }
    }
```

- [ ] **Step 5: Emit `processing_finished` on all terminal conditions in `on_poll_timer()`**

In `on_poll_timer()`, after `stop_progress()` is called in the error branch, add the emit:

```cpp
    if (content.rfind("error: ", 0) == 0) {
        stop_progress();
        std::string msg = std::string(obs_module_text("RizzyTos.Status.Error"))
                        + " " + content.substr(7);
        status_label_->setText(QString::fromStdString(msg));
        // ... (existing log reading code) ...
        emit processing_finished();  // ← ADD THIS
        return;
    }
```

After the stale check:

```cpp
    if (last_progress_change_.secsTo(QDateTime::currentDateTime()) > 30) {
        stop_progress();
        status_label_->setText(obs_module_text("RizzyTos.Status.Stale"));
        emit processing_finished();  // ← ADD THIS
        return;
    }
```

After the 100% check:

```cpp
    if (pct >= 100) {
        stop_progress();
        QString done = QString(obs_module_text("RizzyTos.Status.Done"))
                     + " " + QString::fromStdString(output_path_);
        status_label_->setText(done);
        emit processing_finished();  // ← ADD THIS
    }
```

- [ ] **Step 6: Commit**

```bash
git add src/plugin-ui.h src/plugin-ui.cpp
git commit -m "feat(ui): add resolution/format combos; add processing_finished signal"
```

---

## Task 6: plugin-main — connect processing_finished to set_button_idle

**Files:**
- Modify: `src/plugin-main.cpp`

- [ ] **Step 1: Add the connection inside `OBS_FRONTEND_EVENT_FINISHED_LOADING`**

In the `OBS_FRONTEND_EVENT_FINISHED_LOADING` case, after the existing `connect` calls for `settings_changed` and `record_requested`, add:

```cpp
        QObject::connect(g_dock, &AutoEditDock::processing_finished,
                         []() { set_button_idle(); });
```

- [ ] **Step 2: Commit**

```bash
git add src/plugin-main.cpp
git commit -m "fix(plugin): re-enable record button when worker finishes or errors"
```

---

## Task 7: Locale strings

**Files:**
- Modify: `data/locale/en-US.ini`

- [ ] **Step 1: Add new strings**

Append to the file:

```ini
RizzyTos.Settings.OutputResolution=Resolución de salida
RizzyTos.Settings.OutputFormat=Formato de salida
RizzyTos.YouTube.SectionTitle=YouTube
RizzyTos.YouTube.Connect=Conectar con YouTube
RizzyTos.YouTube.UploadCheck=Subir video a YouTube
RizzyTos.YouTube.Private=Privado
RizzyTos.YouTube.Public=Público
RizzyTos.YouTube.Title=Título
RizzyTos.YouTube.Description=Descripción
RizzyTos.YouTube.Status.Preparing=Preparando subida...
RizzyTos.YouTube.Status.Uploading=Subiendo...
RizzyTos.YouTube.Status.Processing=Procesando respuesta...
RizzyTos.YouTube.Status.Done=Subida completada
RizzyTos.YouTube.Status.Error=Error en subida:
RizzyTos.YouTube.Error.QuotaExceeded=Cuota de YouTube agotada. Intenta más tarde.
RizzyTos.YouTube.Error.NotVerified=YouTube no permite subidas públicas desde apps no verificadas. Selecciona 'Privado' o verifica la app en Google Cloud Console.
RizzyTos.YouTube.Error.NoFile=No se puede subir: el archivo de video no existe.
```

- [ ] **Step 2: Commit**

```bash
git add data/locale/en-US.ini
git commit -m "feat(locale): add strings for resolution/format selectors and YouTube section"
```

---

## Task 8: CMake — add Qt6::Network, qtkeychain, credential defines, new sources

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add `include(FetchContent)` and qtkeychain near the top of CMakeLists.txt**

After the `option()` declarations (around line 11), insert:

```cmake
include(FetchContent)

# qtkeychain: cross-platform secure credential storage
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  qtkeychain
  GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
  GIT_TAG        0.14.3
)
FetchContent_MakeAvailable(qtkeychain)
```

- [ ] **Step 2: Add Qt6::Network and qt6keychain to plugin target, inside the `if(ENABLE_QT)` block**

Change:

```cmake
    find_package(Qt6 COMPONENTS Widgets Core)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
```

To:

```cmake
    find_package(Qt6 COMPONENTS Widgets Core Network)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network qt6keychain)
```

- [ ] **Step 3: Add compile-time credential defines inside `if(NOT WORKER_ONLY)`**

After the `if(ENABLE_QT)` block closes, add:

```cmake
  # OAuth credentials — never hardcoded; must be passed via -D at configure time
  if(NOT DEFINED RIZZYTOS_CLIENT_ID OR "${RIZZYTOS_CLIENT_ID}" STREQUAL "")
    message(WARNING "RIZZYTOS_CLIENT_ID is not set. YouTube integration will not work.")
  endif()
  if(NOT DEFINED RIZZYTOS_CLIENT_SECRET OR "${RIZZYTOS_CLIENT_SECRET}" STREQUAL "")
    message(WARNING "RIZZYTOS_CLIENT_SECRET is not set. YouTube integration will not work.")
  endif()

  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    RIZZYTOS_CLIENT_ID="${RIZZYTOS_CLIENT_ID}"
    RIZZYTOS_CLIENT_SECRET="${RIZZYTOS_CLIENT_SECRET}"
  )
```

- [ ] **Step 4: Add new source files to `target_sources`**

In the `target_sources(${CMAKE_PROJECT_NAME} ...)` block, add:

```cmake
      src/youtube-auth.cpp
      src/youtube-uploader.cpp
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add Qt6::Network, qtkeychain, credential defines, YouTube source files"
```

---

## Task 9: YouTubeAuth — OAuth 2.0 PKCE flow

**Files:**
- Create: `src/youtube-auth.h`
- Create: `src/youtube-auth.cpp`

- [ ] **Step 1: Create `src/youtube-auth.h`**

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QNetworkAccessManager>

class QTcpServer;

class YouTubeAuth : public QObject {
    Q_OBJECT
public:
    explicit YouTubeAuth(QObject *parent = nullptr);

    // Call on plugin load. Reads keychain; if token found, sets authenticated
    // state silently (validity checked lazily on first ensure_valid_token call).
    void load_stored_token();

    bool    is_authenticated() const { return authenticated_; }
    QString access_token()     const { return access_token_;  }

    // Opens browser, starts local server, completes PKCE flow.
    void start_auth_flow();

    // If access_token is still valid, emits authenticated() via queued invoke.
    // If expired, does a refresh network request. If refresh fails, emits auth_revoked().
    void ensure_valid_token();

signals:
    void authenticated();
    void auth_failed(QString error);
    void auth_revoked();

private:
    void exchange_code_for_tokens(const QString &code);
    void do_refresh();
    void store_refresh_token(const QString &token);
    void clear_stored_token();
    void handle_token_response(const QByteArray &body, bool is_refresh);

    QNetworkAccessManager *nam_;
    QTcpServer            *tcp_server_ = nullptr;

    QString   code_verifier_;
    QString   state_;
    QString   redirect_uri_;

    QString   refresh_token_;
    QString   access_token_;
    QDateTime token_expiry_;
    bool      authenticated_ = false;
};
```

- [ ] **Step 2: Create `src/youtube-auth.cpp`**

```cpp
#include "youtube-auth.h"
#include <plugin-support.h>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QDesktopServices>
#include <QUrl>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <keychain.h>

static constexpr const char *CLIENT_ID     = RIZZYTOS_CLIENT_ID;
static constexpr const char *CLIENT_SECRET = RIZZYTOS_CLIENT_SECRET;
static constexpr const char *KEYCHAIN_SERVICE = "rizzytos-auto-edit";
static constexpr const char *KEYCHAIN_KEY     = "youtube_refresh_token";

YouTubeAuth::YouTubeAuth(QObject *parent)
    : QObject(parent)
    , nam_(new QNetworkAccessManager(this))
{}

void YouTubeAuth::load_stored_token()
{
    auto *job = new QKeychain::ReadPasswordJob(KEYCHAIN_SERVICE, this);
    job->setAutoDelete(true);
    job->setKey(KEYCHAIN_KEY);
    connect(job, &QKeychain::ReadPasswordJob::finished, this, [=]() {
        if (job->error() == QKeychain::NoError && !job->textData().isEmpty()) {
            refresh_token_ = job->textData();
            authenticated_ = true;
            obs_log(LOG_INFO, "YouTube: stored token loaded from keychain");
        }
    });
    job->start();
}

void YouTubeAuth::start_auth_flow()
{
    // Generate PKCE code_verifier (32 random bytes → base64url, no padding)
    QByteArray rand_bytes(32, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32 *>(rand_bytes.data()), 8);
    code_verifier_ = rand_bytes.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    // code_challenge = SHA-256(code_verifier) → base64url
    QByteArray challenge_bytes = QCryptographicHash::hash(
        code_verifier_.toUtf8(), QCryptographicHash::Sha256);
    QString code_challenge = challenge_bytes.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    // Anti-CSRF state
    QByteArray state_bytes(16, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32 *>(state_bytes.data()), 4);
    state_ = state_bytes.toHex();

    // Start local loopback server on an OS-assigned port
    tcp_server_ = new QTcpServer(this);
    if (!tcp_server_->listen(QHostAddress::LocalHost, 0)) {
        emit auth_failed("Could not start local auth server");
        tcp_server_->deleteLater();
        tcp_server_ = nullptr;
        return;
    }

    quint16 port = tcp_server_->serverPort();
    redirect_uri_ = QString("http://127.0.0.1:%1").arg(port);

    // Handle one connection
    connect(tcp_server_, &QTcpServer::newConnection, this, [=]() {
        QTcpSocket *socket = tcp_server_->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [=]() {
            QString request = QString::fromUtf8(socket->readAll());

            // Parse GET /?code=...&state=... HTTP/1.1
            QRegularExpression re("GET /\\?([^ ]+) HTTP");
            auto match = re.match(request);

            socket->write(
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Connection: close\r\n\r\n"
                "<html><body><h2>Autorizaci\xC3\xB3n exitosa."
                " Puedes cerrar esta ventana.</h2></body></html>");
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
            tcp_server_->close();
            tcp_server_->deleteLater();
            tcp_server_ = nullptr;

            if (!match.hasMatch()) {
                emit auth_failed("No authorization code in redirect");
                return;
            }

            QUrlQuery query(match.captured(1));
            QString code  = query.queryItemValue("code");
            QString state = query.queryItemValue("state");

            if (state != state_) {
                emit auth_failed("CSRF state mismatch");
                return;
            }
            if (code.isEmpty()) {
                QString error = query.queryItemValue("error");
                emit auth_failed(error.isEmpty() ? "No code received" : error);
                return;
            }

            exchange_code_for_tokens(code);
        });
    });

    // Build authorization URL
    QUrl auth_url("https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery params;
    params.addQueryItem("client_id",             CLIENT_ID);
    params.addQueryItem("redirect_uri",          redirect_uri_);
    params.addQueryItem("response_type",         "code");
    params.addQueryItem("scope",
        "https://www.googleapis.com/auth/youtube.upload");
    params.addQueryItem("code_challenge",        code_challenge);
    params.addQueryItem("code_challenge_method", "S256");
    params.addQueryItem("access_type",           "offline");
    params.addQueryItem("prompt",                "consent");
    params.addQueryItem("state",                 state_);
    auth_url.setQuery(params);

    QDesktopServices::openUrl(auth_url);
    obs_log(LOG_INFO, "YouTube: opened browser for OAuth authorization");
}

void YouTubeAuth::exchange_code_for_tokens(const QString &code)
{
    QNetworkRequest req(QUrl("https://oauth2.googleapis.com/token"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem("code",          code);
    body.addQueryItem("client_id",     CLIENT_ID);
    body.addQueryItem("client_secret", CLIENT_SECRET);
    body.addQueryItem("redirect_uri",  redirect_uri_);
    body.addQueryItem("code_verifier", code_verifier_);
    body.addQueryItem("grant_type",    "authorization_code");

    auto *reply = nam_->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        QByteArray data = reply->readAll();
        reply->deleteLater();
        handle_token_response(data, false);
    });
}

void YouTubeAuth::do_refresh()
{
    QNetworkRequest req(QUrl("https://oauth2.googleapis.com/token"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem("grant_type",    "refresh_token");
    body.addQueryItem("refresh_token", refresh_token_);
    body.addQueryItem("client_id",     CLIENT_ID);
    body.addQueryItem("client_secret", CLIENT_SECRET);

    auto *reply = nam_->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        QByteArray data = reply->readAll();
        reply->deleteLater();
        handle_token_response(data, true);
    });
}

void YouTubeAuth::handle_token_response(const QByteArray &body, bool is_refresh)
{
    QJsonDocument doc  = QJsonDocument::fromJson(body);
    QJsonObject   obj  = doc.object();

    if (obj.contains("error")) {
        QString err = obj["error"].toString();
        obs_log(LOG_WARNING, "YouTube token error: %s", err.toUtf8().constData());
        if (is_refresh && (err == "invalid_grant" || err == "invalid_token")) {
            clear_stored_token();
            authenticated_ = false;
            access_token_.clear();
            emit auth_revoked();
        } else {
            emit auth_failed(obj.value("error_description").toString(err));
        }
        return;
    }

    access_token_ = obj["access_token"].toString();
    int expires_in = obj["expires_in"].toInt(3600);
    // Subtract 60s so we refresh before expiry
    token_expiry_ = QDateTime::currentDateTime().addSecs(expires_in - 60);

    if (!is_refresh && obj.contains("refresh_token")) {
        refresh_token_ = obj["refresh_token"].toString();
        store_refresh_token(refresh_token_);
    }

    authenticated_ = true;
    obs_log(LOG_INFO, "YouTube: token obtained successfully");
    emit authenticated();
}

void YouTubeAuth::ensure_valid_token()
{
    if (authenticated_ && !access_token_.isEmpty() &&
        QDateTime::currentDateTime() < token_expiry_) {
        // Still valid — emit asynchronously to avoid re-entrant signal issues
        QTimer::singleShot(0, this, [this]() { emit authenticated(); });
        return;
    }

    if (refresh_token_.isEmpty()) {
        authenticated_ = false;
        emit auth_revoked();
        return;
    }

    do_refresh();
}

void YouTubeAuth::store_refresh_token(const QString &token)
{
    auto *job = new QKeychain::WritePasswordJob(KEYCHAIN_SERVICE, this);
    job->setAutoDelete(true);
    job->setKey(KEYCHAIN_KEY);
    job->setTextData(token);
    connect(job, &QKeychain::WritePasswordJob::finished, this, [=]() {
        if (job->error() != QKeychain::NoError)
            obs_log(LOG_WARNING, "YouTube: failed to store token in keychain: %s",
                    job->errorString().toUtf8().constData());
    });
    job->start();
}

void YouTubeAuth::clear_stored_token()
{
    refresh_token_.clear();
    auto *job = new QKeychain::DeletePasswordJob(KEYCHAIN_SERVICE, this);
    job->setAutoDelete(true);
    job->setKey(KEYCHAIN_KEY);
    job->start();
    obs_log(LOG_INFO, "YouTube: cleared stored token from keychain");
}
```

- [ ] **Step 3: Commit**

```bash
git add src/youtube-auth.h src/youtube-auth.cpp
git commit -m "feat(youtube): add YouTubeAuth with OAuth 2.0 PKCE flow and keychain storage"
```

---

## Task 10: YouTubeUploader — resumable upload

**Files:**
- Create: `src/youtube-uploader.h`
- Create: `src/youtube-uploader.cpp`

- [ ] **Step 1: Create `src/youtube-uploader.h`**

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QNetworkAccessManager>

enum class UploadState {
    Preparing,
    Uploading,
    ProcessingResponse,
    Completed,
    Failed
};

struct UploadMetadata {
    QString title;
    QString description;
    QString privacy_status; // "private" | "public"
};

class YouTubeUploader : public QObject {
    Q_OBJECT
public:
    explicit YouTubeUploader(QObject *parent = nullptr);

    void set_access_token(const QString &token);
    void start(const QString &file_path, const UploadMetadata &metadata);

signals:
    void state_changed(UploadState state);
    void progress_updated(int percent);
    void completed(QString video_url);
    void failed(QString error);
    void token_expired();   // plugin-main refreshes token, then calls set_access_token()

private slots:
    void on_initiate_finished();
    void on_chunk_finished();

private:
    void initiate_session();
    void send_next_chunk();
    void query_resume_offset();
    QString build_metadata_json() const;

    QNetworkAccessManager *nam_;
    QString         access_token_;
    UploadMetadata  metadata_;
    QString         file_path_;
    QUrl            upload_url_;
    qint64          file_size_  = 0;
    qint64          bytes_sent_ = 0;
    int             retry_count_ = 0;

    static constexpr qint64 CHUNK_SIZE = 8LL * 1024 * 1024; // 8 MB
};
```

- [ ] **Step 2: Create `src/youtube-uploader.cpp`**

```cpp
#include "youtube-uploader.h"
#include <plugin-support.h>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

YouTubeUploader::YouTubeUploader(QObject *parent)
    : QObject(parent)
    , nam_(new QNetworkAccessManager(this))
{}

void YouTubeUploader::set_access_token(const QString &token)
{
    access_token_ = token;
    // Resume after a token refresh — re-send the current chunk
    if (!upload_url_.isEmpty())
        send_next_chunk();
}

void YouTubeUploader::start(const QString &file_path, const UploadMetadata &metadata)
{
    file_path_   = file_path;
    metadata_    = metadata;
    bytes_sent_  = 0;
    retry_count_ = 0;
    upload_url_  = QUrl();

    QFile file(file_path_);
    if (!file.exists() || file.size() == 0) {
        emit failed(obs_module_text("RizzyTos.YouTube.Error.NoFile"));
        return;
    }
    file_size_ = file.size();

    initiate_session();
}

void YouTubeUploader::initiate_session()
{
    emit state_changed(UploadState::Preparing);

    QUrl url("https://www.googleapis.com/upload/youtube/v3/videos");
    QUrlQuery q;
    q.addQueryItem("uploadType",        "resumable");
    q.addQueryItem("part",              "snippet,status");
    q.addQueryItem("notifySubscribers", "true");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=UTF-8");
    req.setRawHeader("Authorization",
                     QString("Bearer %1").arg(access_token_).toUtf8());
    req.setRawHeader("X-Upload-Content-Type",   "video/*");
    req.setRawHeader("X-Upload-Content-Length",
                     QString::number(file_size_).toUtf8());

    auto *reply = nam_->post(req, build_metadata_json().toUtf8());
    connect(reply, &QNetworkReply::finished, this, &YouTubeUploader::on_initiate_finished);
}

void YouTubeUploader::on_initiate_finished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    int status  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 401) {
        reply->deleteLater();
        emit token_expired();
        return;
    }

    if (status != 200) {
        QByteArray body = reply->readAll();
        reply->deleteLater();
        emit failed(QString("Failed to initiate upload (HTTP %1): %2")
                    .arg(status).arg(QString::fromUtf8(body).left(200)));
        return;
    }

    upload_url_ = QUrl(reply->rawHeader("Location"));
    reply->deleteLater();

    if (!upload_url_.isValid() || upload_url_.isEmpty()) {
        emit failed("No upload URL in initiate response");
        return;
    }

    emit state_changed(UploadState::Uploading);
    send_next_chunk();
}

void YouTubeUploader::send_next_chunk()
{
    QFile file(file_path_);
    if (!file.open(QIODevice::ReadOnly)) {
        emit failed("Cannot open file: " + file_path_);
        return;
    }
    file.seek(bytes_sent_);
    QByteArray chunk = file.read(CHUNK_SIZE);
    file.close();

    if (chunk.isEmpty()) {
        emit failed("Unexpected end of file at offset " + QString::number(bytes_sent_));
        return;
    }

    qint64 start = bytes_sent_;
    qint64 end   = bytes_sent_ + chunk.size() - 1;

    QNetworkRequest req(upload_url_);
    req.setHeader(QNetworkRequest::ContentTypeHeader,   "video/*");
    req.setHeader(QNetworkRequest::ContentLengthHeader, chunk.size());
    req.setRawHeader("Authorization",
                     QString("Bearer %1").arg(access_token_).toUtf8());
    req.setRawHeader("Content-Range",
                     QString("bytes %1-%2/%3").arg(start).arg(end).arg(file_size_).toUtf8());

    auto *reply = nam_->put(req, chunk);

    connect(reply, &QNetworkReply::uploadProgress, this,
            [=](qint64 sent, qint64 /*total*/) {
        int pct = static_cast<int>(((bytes_sent_ + sent) * 100LL) / file_size_);
        emit progress_updated(qBound(0, pct, 99)); // 100 only on final 200/201
    });

    connect(reply, &QNetworkReply::finished, this, &YouTubeUploader::on_chunk_finished);
}

void YouTubeUploader::on_chunk_finished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    int status  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 401) {
        reply->deleteLater();
        emit token_expired();
        return;
    }

    if (status == 403) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QString reason = doc.object()["error"].toObject()["errors"]
                             .toArray().first().toObject()["reason"].toString();
        reply->deleteLater();
        if (reason == "forbidden" || reason == "quotaExceeded") {
            emit failed(obs_module_text("RizzyTos.YouTube.Error.QuotaExceeded"));
        } else {
            emit failed(obs_module_text("RizzyTos.YouTube.Error.NotVerified"));
        }
        return;
    }

    if (status == 500 || status == 502 || status == 503 || status == 504) {
        reply->deleteLater();
        if (++retry_count_ >= 3) {
            emit failed(QString("Server error (HTTP %1) after 3 retries").arg(status));
            return;
        }
        query_resume_offset();
        return;
    }

    if (status == 308) {
        // Chunk accepted; server tells us the last byte it received
        QString range_hdr = QString::fromUtf8(reply->rawHeader("Range"));
        reply->deleteLater();
        if (!range_hdr.isEmpty()) {
            // Format: bytes=0-N
            int dash = range_hdr.lastIndexOf('-');
            if (dash >= 0)
                bytes_sent_ = range_hdr.mid(dash + 1).toLongLong() + 1;
        }
        retry_count_ = 0;
        emit state_changed(UploadState::Uploading);
        send_next_chunk();
        return;
    }

    if (status == 200 || status == 201) {
        emit state_changed(UploadState::ProcessingResponse);
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        QString video_id = doc.object()["id"].toString();
        if (video_id.isEmpty()) {
            emit failed("Upload complete but no video ID in response");
            return;
        }
        emit progress_updated(100);
        emit state_changed(UploadState::Completed);
        emit completed("https://www.youtube.com/watch?v=" + video_id);
        return;
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();
    emit failed(QString("Unexpected HTTP %1: %2")
                .arg(status).arg(QString::fromUtf8(body).left(200)));
}

void YouTubeUploader::query_resume_offset()
{
    QNetworkRequest req(upload_url_);
    req.setRawHeader("Authorization",
                     QString("Bearer %1").arg(access_token_).toUtf8());
    req.setRawHeader("Content-Range",
                     QString("bytes */%1").arg(file_size_).toUtf8());
    req.setHeader(QNetworkRequest::ContentLengthHeader, 0);

    auto *reply = nam_->put(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (status == 308) {
            QString range_hdr = QString::fromUtf8(reply->rawHeader("Range"));
            reply->deleteLater();
            if (range_hdr.isEmpty()) {
                bytes_sent_ = 0; // no bytes confirmed yet
            } else {
                int dash = range_hdr.lastIndexOf('-');
                if (dash >= 0)
                    bytes_sent_ = range_hdr.mid(dash + 1).toLongLong() + 1;
            }
            send_next_chunk();
        } else if (status == 200 || status == 201) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            reply->deleteLater();
            QString video_id = doc.object()["id"].toString();
            emit progress_updated(100);
            emit state_changed(UploadState::Completed);
            emit completed("https://www.youtube.com/watch?v=" + video_id);
        } else {
            reply->deleteLater();
            emit failed(QString("Resume query failed with HTTP %1").arg(status));
        }
    });
}

QString YouTubeUploader::build_metadata_json() const
{
    QJsonObject snippet;
    snippet["title"]       = metadata_.title;
    snippet["description"] = metadata_.description;

    QJsonObject status;
    status["privacyStatus"]          = metadata_.privacy_status;
    status["selfDeclaredMadeForKids"] = false;
    status["containsSyntheticMedia"]  = false;

    QJsonObject root;
    root["snippet"] = snippet;
    root["status"]  = status;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/youtube-uploader.h src/youtube-uploader.cpp
git commit -m "feat(youtube): add YouTubeUploader with resumable upload, progress signals, and retry"
```

---

## Task 11: Dock — YouTube UI section

**Files:**
- Modify: `src/plugin-ui.h`
- Modify: `src/plugin-ui.cpp`

- [ ] **Step 1: Update `src/plugin-ui.h` — add YouTube includes, members, and methods**

Replace the file completely (adds YouTube section on top of Task 5's version):

```cpp
#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QFrame>
#include "plugin-settings.h"
#include "youtube-uploader.h"

class AutoEditDock : public QWidget {
    Q_OBJECT
public:
    explicit AutoEditDock(QWidget *parent = nullptr);

    PluginSettings get_settings() const;
    void set_settings(const PluginSettings &s);

    YouTubeSettings get_youtube_settings() const;
    void set_youtube_settings(const YouTubeSettings &ys);
    bool youtube_upload_enabled() const;

    // Called from plugin-main when YouTubeAuth state changes
    void set_youtube_authenticated(bool authenticated);

    // Called from plugin-main to display upload progress
    void on_yt_state_changed(UploadState state);
    void on_yt_progress_updated(int percent);
    void on_yt_upload_completed(const QString &url);
    void on_yt_upload_failed(const QString &error);

    void start_progress(const std::string &progress_file_path,
                        const std::string &output_path);
    void stop_progress();

    void set_action_button_state(bool recording, bool enabled);

signals:
    void settings_changed(const PluginSettings &s);
    void record_requested();
    void processing_finished();
    void youtube_connect_requested();
    void youtube_settings_changed(const YouTubeSettings &ys);

private slots:
    void browse_output_dir();
    void browse_intro();
    void browse_outro();
    void on_poll_timer();

private:
    // Existing settings widgets
    QPushButton  *action_btn_       = nullptr;
    QLineEdit    *output_dir_edit_  = nullptr;
    QLineEdit    *output_name_edit_ = nullptr;
    QLineEdit    *intro_edit_       = nullptr;
    QLineEdit    *outro_edit_       = nullptr;
    QComboBox    *resolution_combo_ = nullptr;
    QComboBox    *format_combo_     = nullptr;
    QProgressBar *progress_bar_     = nullptr;
    QLabel       *status_label_     = nullptr;
    QTimer       *poll_timer_       = nullptr;

    std::string  progress_file_;
    std::string  output_path_;
    QDateTime    last_progress_change_;
    int          last_progress_value_ = -1;

    // YouTube section widgets
    QWidget      *yt_unauth_widget_   = nullptr; // shown when not authenticated
    QWidget      *yt_auth_widget_     = nullptr; // shown when authenticated
    QCheckBox    *yt_upload_check_    = nullptr;
    QWidget      *yt_settings_widget_ = nullptr; // shown when checkbox checked
    QRadioButton *yt_private_radio_   = nullptr;
    QRadioButton *yt_public_radio_    = nullptr;
    QLineEdit    *yt_title_edit_      = nullptr;
    QTextEdit    *yt_desc_edit_       = nullptr;
    QLabel       *yt_status_label_    = nullptr;
    QProgressBar *yt_progress_bar_    = nullptr;
    QLabel       *yt_url_label_       = nullptr;
};

QPushButton *inject_record_button(QWidget *main_window);
```

- [ ] **Step 2: Add YouTube section construction to `AutoEditDock` constructor in `src/plugin-ui.cpp`**

Add these includes at the top of `plugin-ui.cpp` (alongside existing includes):

```cpp
#include <QCheckBox>
#include <QRadioButton>
#include <QTextEdit>
#include <QGroupBox>
#include <QButtonGroup>
```

At the very end of the constructor, just before `layout->addStretch()` (move the stretch call to after the YouTube section):

Remove the existing `layout->addStretch();` line and replace with:

```cpp
    // ── YouTube section ───────────────────────────────────────────────────────

    {
        auto *sep2 = new QFrame(this);
        sep2->setFrameShape(QFrame::HLine);
        sep2->setFrameShadow(QFrame::Sunken);
        layout->addWidget(sep2);
    }

    auto *yt_title_lbl = new QLabel(obs_module_text("RizzyTos.YouTube.SectionTitle"), this);
    QFont yt_font = yt_title_lbl->font();
    yt_font.setBold(true);
    yt_title_lbl->setFont(yt_font);
    layout->addWidget(yt_title_lbl);

    // --- Unauthenticated widget ---
    yt_unauth_widget_ = new QWidget(this);
    {
        auto *vbox = new QVBoxLayout(yt_unauth_widget_);
        vbox->setContentsMargins(0, 0, 0, 0);
        auto *connect_btn = new QPushButton(
            obs_module_text("RizzyTos.YouTube.Connect"), yt_unauth_widget_);
        vbox->addWidget(connect_btn);
        connect(connect_btn, &QPushButton::clicked,
                this, &AutoEditDock::youtube_connect_requested);
    }
    layout->addWidget(yt_unauth_widget_);

    // --- Authenticated widget ---
    yt_auth_widget_ = new QWidget(this);
    {
        auto *vbox = new QVBoxLayout(yt_auth_widget_);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(4);

        yt_upload_check_ = new QCheckBox(
            obs_module_text("RizzyTos.YouTube.UploadCheck"), yt_auth_widget_);
        vbox->addWidget(yt_upload_check_);

        // Settings sub-widget shown only when checkbox is checked
        yt_settings_widget_ = new QWidget(yt_auth_widget_);
        {
            auto *svbox = new QVBoxLayout(yt_settings_widget_);
            svbox->setContentsMargins(16, 0, 0, 0);
            svbox->setSpacing(4);

            // Privacy radio buttons
            auto *privacy_row = new QHBoxLayout;
            yt_private_radio_ = new QRadioButton(
                obs_module_text("RizzyTos.YouTube.Private"), yt_settings_widget_);
            yt_public_radio_  = new QRadioButton(
                obs_module_text("RizzyTos.YouTube.Public"),  yt_settings_widget_);
            yt_private_radio_->setChecked(true);
            privacy_row->addWidget(yt_private_radio_);
            privacy_row->addWidget(yt_public_radio_);
            privacy_row->addStretch();
            svbox->addLayout(privacy_row);

            // Title
            {
                auto *row = new QHBoxLayout;
                auto *lbl = new QLabel(
                    obs_module_text("RizzyTos.YouTube.Title"), yt_settings_widget_);
                lbl->setFixedWidth(80);
                yt_title_edit_ = new QLineEdit(yt_settings_widget_);
                row->addWidget(lbl);
                row->addWidget(yt_title_edit_);
                svbox->addLayout(row);
            }

            // Description
            {
                auto *lbl = new QLabel(
                    obs_module_text("RizzyTos.YouTube.Description"), yt_settings_widget_);
                svbox->addWidget(lbl);
                yt_desc_edit_ = new QTextEdit(yt_settings_widget_);
                yt_desc_edit_->setFixedHeight(60);
                yt_desc_edit_->setPlainText(
                    QString::fromUtf8("Mira mis streams en "
                        "https://www.twitch.tv/nansulli \xF0\x9F\x92\x96"));
                svbox->addWidget(yt_desc_edit_);
            }
        }
        yt_settings_widget_->hide();
        vbox->addWidget(yt_settings_widget_);

        // Show/hide settings sub-widget on checkbox toggle
        connect(yt_upload_check_, &QCheckBox::toggled, this, [=](bool checked) {
            yt_settings_widget_->setVisible(checked);
            emit youtube_settings_changed(get_youtube_settings());
        });

        auto emit_yt_changed = [this]() {
            emit youtube_settings_changed(get_youtube_settings());
        };
        connect(yt_title_edit_, &QLineEdit::editingFinished, this, emit_yt_changed);
        connect(yt_desc_edit_,  &QTextEdit::textChanged,    this, emit_yt_changed);
        connect(yt_private_radio_, &QRadioButton::toggled, this, emit_yt_changed);
    }
    yt_auth_widget_->hide(); // hidden until auth confirmed
    layout->addWidget(yt_auth_widget_);

    // --- Upload progress area (hidden until upload starts) ---
    yt_status_label_ = new QLabel(this);
    yt_status_label_->hide();
    layout->addWidget(yt_status_label_);

    yt_progress_bar_ = new QProgressBar(this);
    yt_progress_bar_->setRange(0, 100);
    yt_progress_bar_->hide();
    layout->addWidget(yt_progress_bar_);

    yt_url_label_ = new QLabel(this);
    yt_url_label_->setTextFormat(Qt::RichText);
    yt_url_label_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    yt_url_label_->setOpenExternalLinks(true);
    yt_url_label_->hide();
    layout->addWidget(yt_url_label_);

    layout->addStretch();
```

- [ ] **Step 3: Implement the YouTube getter/setter methods in `src/plugin-ui.cpp`**

Add these methods after the existing `set_settings()` function:

```cpp
YouTubeSettings AutoEditDock::get_youtube_settings() const
{
    YouTubeSettings ys;
    if (yt_upload_check_)
        ys.upload_enabled = yt_upload_check_->isChecked();
    if (yt_private_radio_ && yt_public_radio_)
        ys.privacy = yt_public_radio_->isChecked() ? "public" : "private";
    if (yt_title_edit_)
        ys.title = yt_title_edit_->text().toStdString();
    if (yt_desc_edit_)
        ys.description = yt_desc_edit_->toPlainText().toStdString();
    return ys;
}

void AutoEditDock::set_youtube_settings(const YouTubeSettings &ys)
{
    if (!yt_upload_check_) return;
    yt_upload_check_->setChecked(ys.upload_enabled);
    yt_settings_widget_->setVisible(ys.upload_enabled);
    if (ys.privacy == "public")
        yt_public_radio_->setChecked(true);
    else
        yt_private_radio_->setChecked(true);
    yt_title_edit_->setText(QString::fromStdString(ys.title));
    if (!ys.description.empty())
        yt_desc_edit_->setPlainText(QString::fromStdString(ys.description));
}

bool AutoEditDock::youtube_upload_enabled() const
{
    return yt_upload_check_ && yt_upload_check_->isChecked();
}

void AutoEditDock::set_youtube_authenticated(bool authenticated)
{
    if (authenticated) {
        yt_unauth_widget_->hide();
        yt_auth_widget_->show();
    } else {
        yt_auth_widget_->hide();
        yt_unauth_widget_->show();
    }
}

void AutoEditDock::on_yt_state_changed(UploadState state)
{
    yt_status_label_->show();
    yt_progress_bar_->show();
    switch (state) {
    case UploadState::Preparing:
        yt_status_label_->setText(obs_module_text("RizzyTos.YouTube.Status.Preparing"));
        break;
    case UploadState::Uploading:
        yt_status_label_->setText(obs_module_text("RizzyTos.YouTube.Status.Uploading"));
        break;
    case UploadState::ProcessingResponse:
        yt_status_label_->setText(obs_module_text("RizzyTos.YouTube.Status.Processing"));
        break;
    case UploadState::Completed:
        yt_status_label_->setText(obs_module_text("RizzyTos.YouTube.Status.Done"));
        yt_progress_bar_->hide();
        break;
    case UploadState::Failed:
        yt_progress_bar_->hide();
        break;
    }
}

void AutoEditDock::on_yt_progress_updated(int percent)
{
    yt_progress_bar_->setValue(percent);
}

void AutoEditDock::on_yt_upload_completed(const QString &url)
{
    yt_url_label_->setText(QString("<a href=\"%1\">%1</a>").arg(url));
    yt_url_label_->show();
}

void AutoEditDock::on_yt_upload_failed(const QString &error)
{
    QString msg = QString(obs_module_text("RizzyTos.YouTube.Status.Error"))
                + " " + error;
    yt_status_label_->setText(msg);
    yt_status_label_->show();
    yt_progress_bar_->hide();
}
```

- [ ] **Step 4: Commit**

```bash
git add src/plugin-ui.h src/plugin-ui.cpp
git commit -m "feat(ui): add YouTube section to dock with auth state, upload progress, and URL display"
```

---

## Task 12: plugin-main — YouTube wiring

**Files:**
- Modify: `src/plugin-main.cpp`

- [ ] **Step 1: Add includes, globals, and helpers at the top of `src/plugin-main.cpp`**

Add includes (alongside existing ones):

```cpp
#include "youtube-auth.h"
#include "youtube-uploader.h"
#include "plugin-settings.h" // already included
```

Add new globals after the existing `static QPointer<QPushButton> g_button;` line:

```cpp
static YouTubeAuth    *g_youtube_auth   = nullptr;
static YouTubeSettings g_yt_settings;
static QString         g_pending_upload_path; // set when processing_finished fires with upload enabled
```

- [ ] **Step 2: Add `on_youtube_authenticated()` helper function**

Add this static function before `obs_event_cb`:

```cpp
static void start_youtube_upload()
{
    if (g_pending_upload_path.isEmpty()) return;
    if (!QFileInfo::exists(g_pending_upload_path)) {
        obs_log(LOG_WARNING, "YouTube: output file not found: %s",
                g_pending_upload_path.toUtf8().constData());
        g_pending_upload_path.clear();
        return;
    }

    UploadMetadata meta;
    meta.title          = QString::fromStdString(g_yt_settings.title);
    meta.description    = QString::fromStdString(g_yt_settings.description);
    meta.privacy_status = QString::fromStdString(g_yt_settings.privacy);

    // Uploader lifetime tied to the dock (parent)
    auto *uploader = new YouTubeUploader(g_dock);
    uploader->set_access_token(g_youtube_auth->access_token());

    if (g_dock) {
        QObject::connect(uploader, &YouTubeUploader::state_changed,
                         g_dock,   &AutoEditDock::on_yt_state_changed);
        QObject::connect(uploader, &YouTubeUploader::progress_updated,
                         g_dock,   &AutoEditDock::on_yt_progress_updated);
        QObject::connect(uploader, &YouTubeUploader::completed,
                         g_dock,   &AutoEditDock::on_yt_upload_completed);
        QObject::connect(uploader, &YouTubeUploader::failed,
                         g_dock,   &AutoEditDock::on_yt_upload_failed);
    }

    // Clean up uploader when done so its connections don't accumulate
    QObject::connect(uploader, &YouTubeUploader::completed, uploader, &QObject::deleteLater);
    QObject::connect(uploader, &YouTubeUploader::failed,    uploader, &QObject::deleteLater);

    // Token-expired: refresh and resume
    QObject::connect(uploader, &YouTubeUploader::token_expired,
                     g_youtube_auth, &YouTubeAuth::ensure_valid_token);
    QObject::connect(g_youtube_auth, &YouTubeAuth::authenticated, uploader,
                     [uploader]() {
                         if (g_youtube_auth)
                             uploader->set_access_token(g_youtube_auth->access_token());
                     });

    QString path = g_pending_upload_path;
    g_pending_upload_path.clear();
    uploader->start(path, meta);
}

static void on_processing_finished()
{
    set_button_idle();

    if (!g_youtube_auth || !g_dock) return;
    if (!g_dock->youtube_upload_enabled()) return;

    QString output_path = QString::fromStdString(g_launcher.output_path());
    if (output_path.isEmpty()) return;

    g_pending_upload_path = output_path;
    g_yt_settings = g_dock->get_youtube_settings();

    g_youtube_auth->ensure_valid_token();
}
```

- [ ] **Step 3: Update the `OBS_FRONTEND_EVENT_FINISHED_LOADING` case to wire everything up**

Replace the case content with:

```cpp
    case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
        QMainWindow *mw =
            static_cast<QMainWindow *>(obs_frontend_get_main_window());
        g_button = inject_record_button(mw);
        if (g_button)
            QObject::connect(g_button, &QPushButton::clicked, on_button_clicked);

        auto *dock_widget = new AutoEditDock();
        g_dock = dock_widget;
        QObject::connect(g_dock, &AutoEditDock::settings_changed,
                         on_dock_settings_changed);
        QObject::connect(g_dock, &AutoEditDock::record_requested,
                         on_button_clicked);
        QObject::connect(g_dock, &AutoEditDock::processing_finished,
                         on_processing_finished);
        QObject::connect(g_dock, &AutoEditDock::youtube_connect_requested,
                         []() { if (g_youtube_auth) g_youtube_auth->start_auth_flow(); });
        QObject::connect(g_dock, &AutoEditDock::youtube_settings_changed,
                         [](const YouTubeSettings &ys) {
                             g_yt_settings = ys;
                             youtube_settings_save(ys, g_config_path);
                         });
        g_dock->set_settings(g_settings);
        g_dock->set_youtube_settings(g_yt_settings);

        // YouTube auth
        g_youtube_auth = new YouTubeAuth(g_dock); // parented to dock; cleaned up with it
        QObject::connect(g_youtube_auth, &YouTubeAuth::authenticated, []() {
            if (g_dock) g_dock->set_youtube_authenticated(true);
            start_youtube_upload(); // no-op if no pending upload
        });
        QObject::connect(g_youtube_auth, &YouTubeAuth::auth_revoked, []() {
            if (g_dock) g_dock->set_youtube_authenticated(false);
            g_pending_upload_path.clear();
        });
        QObject::connect(g_youtube_auth, &YouTubeAuth::auth_failed,
                         [](const QString &err) {
                             obs_log(LOG_WARNING, "YouTube auth failed: %s",
                                     err.toUtf8().constData());
                             if (g_dock) g_dock->set_youtube_authenticated(false);
                         });

        g_youtube_auth->load_stored_token();

        obs_frontend_add_dock_by_id("rizzytos_auto_edit_dock",
                                    obs_module_text("RizzyTos.Dock.Title"),
                                    dock_widget);
        break;
    }
```

- [ ] **Step 4: Update `obs_module_load()` to load YouTube settings**

In `obs_module_load()`, after `g_settings = settings_load(g_config_path);` add:

```cpp
    g_yt_settings = youtube_settings_load(g_config_path);
```

- [ ] **Step 5: Add `#include <QFileInfo>` to plugin-main.cpp** (needed by `start_youtube_upload`)

Add near the other Qt includes:

```cpp
#include <QFileInfo>
```

- [ ] **Step 6: Commit**

```bash
git add src/plugin-main.cpp
git commit -m "feat(plugin-main): wire YouTubeAuth, YouTubeUploader, and YouTube settings persistence"
```

---

## Build and integration test

- [ ] **Step 1: Configure with credentials**

```bash
cmake -B build \
  -DRIZZYTOS_CLIENT_ID="YOUR_CLIENT_ID.apps.googleusercontent.com" \
  -DRIZZYTOS_CLIENT_SECRET="YOUR_CLIENT_SECRET"
```

Replace the values with real credentials from Google Cloud Console (OAuth 2.0 Desktop App client).

- [ ] **Step 2: Build**

```bash
cmake --build build --config RelWithDebInfo
```

Fix any compiler errors before continuing.

- [ ] **Step 3: Integration test checklist**

Install the plugin into OBS and verify:

- [ ] Resolution combo shows 720p / 1080p / 2K / 4K; selected value is saved and restored after OBS restart.
- [ ] Format combo shows MKV / MP4; selected value is saved and restored.
- [ ] Record a short clip. After processing, output file has the correct extension and is at the correct resolution (open in a media player or run `ffprobe output.mkv`).
- [ ] After processing completes, the Record button re-enables automatically (no OBS restart needed).
- [ ] YouTube section shows "Conectar con YouTube" when not authenticated.
- [ ] Clicking "Conectar con YouTube" opens a browser tab to Google's OAuth consent screen.
- [ ] After approving, the dock switches to show the upload controls.
- [ ] Checking "Subir video a YouTube" reveals privacy/title/description fields.
- [ ] Recording and processing a clip with the checkbox enabled triggers the upload progress bar in the dock.
- [ ] After upload, a clickable YouTube URL appears in the dock.
- [ ] Restarting OBS preserves the authenticated state (no new login required).

---

## CMake notes for GitHub Actions

In your workflow `.yml`, pass credentials from repository secrets:

```yaml
- name: Configure CMake
  run: |
    cmake -B build \
      -DRIZZYTOS_CLIENT_ID="${{ secrets.RIZZYTOS_CLIENT_ID }}" \
      -DRIZZYTOS_CLIENT_SECRET="${{ secrets.RIZZYTOS_CLIENT_SECRET }}"
```

Add `RIZZYTOS_CLIENT_ID` and `RIZZYTOS_CLIENT_SECRET` as encrypted repository secrets in **Settings → Secrets and variables → Actions**.

Also add to `.gitignore` any local file you use to hold the credentials during development:

```
cmake-secrets.sh
local-configure.sh
```
