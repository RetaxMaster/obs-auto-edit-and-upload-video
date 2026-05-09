# RizzyTos Auto Edit — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an OBS Studio plugin (Windows + macOS) that adds a "Grabar y editar auto" button which, when the recording stops, automatically concatenates configured intro/outro videos using a bundled FFmpeg worker process.

**Architecture:** The plugin (C++ shared library with Qt6 + obs-frontend-api) injects a button into OBS's controls dock and owns a settings dock panel. When triggered, it reads OBS's encoding configuration and spawns a standalone `rizzytos-worker` executable (no OBS/Qt dependency) that calls bundled FFmpeg to concatenate and re-encode the video. Progress is communicated via a temp file polled by a QTimer.

**Tech Stack:** C++17, Qt6 Widgets, obs-frontend-api, obs-libobs, CMake 3.28+, FFmpeg CLI (bundled), CTest for worker unit tests.

---

## File Map

| File | Role |
|------|------|
| `buildspec.json` | Plugin metadata (name, version, author) |
| `CMakeLists.txt` | Enable Qt + frontend API; add worker target and tests |
| `CMakePresets.json` | Enable `ENABLE_FRONTEND_API` and `ENABLE_QT` in template preset |
| `data/locale/en-US.ini` | All UI strings |
| `src/plugin-main.cpp` | `obs_module_load/unload`, event callback, global wiring |
| `src/plugin-support.h` | Unchanged (logging helpers) |
| `src/plugin-support.c.in` | Unchanged (generated) |
| `src/plugin-settings.h/.cpp` | `PluginSettings` struct; load/save via `obs_data_t` |
| `src/plugin-recorder.h/.cpp` | `OBSEncoderInfo`; `our_recording_active` flag; reads OBS encoder settings |
| `src/plugin-launcher.h/.cpp` | Spawns worker via `QProcess::startDetached`; owns progress file path |
| `src/plugin-ui.h/.cpp` | `AutoEditDock` widget (settings + progress); `inject_record_button()` |
| `worker/args.h/.cpp` | `WorkerArgs` struct; `parse_args()` |
| `worker/progress.h/.cpp` | `ProgressWriter`; `parse_duration_seconds()` |
| `worker/concat.h/.cpp` | `build_ffmpeg_args()`; `run_ffmpeg()` with progress pipe |
| `worker/main.cpp` | Entry point; wires args → progress → concat |
| `tests/worker/test_args.cpp` | Unit tests for `parse_args()` |
| `tests/worker/test_progress.cpp` | Unit tests for `parse_duration_seconds()` |
| `tests/worker/test_concat.cpp` | Unit tests for `build_ffmpeg_args()` |

---

## Task 1: Rename branch to main and update project metadata

**Files:**
- Modify: `buildspec.json`
- Modify: `CMakeLists.txt` (top comment only; name comes from buildspec)

- [ ] **Step 1: Rename branch**

```bash
git branch -m master main
```

Expected: no output (branch renamed locally).

- [ ] **Step 2: Update buildspec.json with plugin identity**

Replace the entire contents of `buildspec.json`:

```json
{
    "dependencies": {
        "obs-studio": {
            "version": "31.1.1",
            "baseUrl": "https://github.com/obsproject/obs-studio/archive/refs/tags",
            "label": "OBS sources",
            "hashes": {
                "macos": "39751f067bacc13d44b116c5138491b5f1391f91516d3d590d874edd21292291",
                "windows-x64": "2c8427c10b55ac6d68008df2e9a3e82f4647aaad18f105e30d4713c2de678ccf"
            }
        },
        "prebuilt": {
            "version": "2025-07-11",
            "baseUrl": "https://github.com/obsproject/obs-deps/releases/download",
            "label": "Pre-Built obs-deps",
            "hashes": {
                "macos": "495687e63383d1a287684b6e2e9bfe246bb8f156fe265926afb1a325af1edd2a",
                "windows-x64": "c8c642c1070dc31ce9a0f1e4cef5bb992f4bff4882255788b5da12129e85caa7"
            }
        },
        "qt6": {
            "version": "2025-07-11",
            "baseUrl": "https://github.com/obsproject/obs-deps/releases/download",
            "label": "Pre-Built Qt6",
            "hashes": {
                "macos": "d3f5f04b6ea486e032530bdf0187cbda9a54e0a49621a4c8ba984c5023998867",
                "windows-x64": "0e76bf0555dd5382838850b748d3dcfab44a1e1058441309ab54e1a65b156d0a"
            },
            "debugSymbols": {
                "windows-x64": "11b7be92cf66a273299b8f3515c07a5cfb61614b59a4e67f7fc5ecba5e2bdf21"
            }
        }
    },
    "platformConfig": {
        "macos": {
            "bundleId": "com.retaxmaster.rizzytos-auto-edit"
        }
    },
    "name": "rizzytos-auto-edit",
    "displayName": "RizzyTos Auto Edit",
    "version": "1.0.0",
    "author": "RetaxMaster",
    "website": "https://retaxmaster.com",
    "email": "carlos@retaxmaster.com"
}
```

- [ ] **Step 3: Commit**

```bash
git add buildspec.json
git commit -m "chore: rename branch to main and set plugin identity"
```

---

## Task 2: Update CMakeLists.txt and CMakePresets.json

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`

The template disables Qt and frontend API by default. We need both. We also add the worker executable target and test targets.

- [ ] **Step 1: Enable Qt and frontend API in CMakePresets.json**

In `CMakePresets.json`, find the `"template"` configure preset and change both flags to `true`:

```json
{
  "name": "template",
  "hidden": true,
  "cacheVariables": {
    "ENABLE_FRONTEND_API": true,
    "ENABLE_QT": true
  }
}
```

- [ ] **Step 2: Replace CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(BUILD_TESTING "Build unit tests for the worker" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# ── Plugin shared library ────────────────────────────────────────────────────

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/plugin-settings.cpp
    src/plugin-recorder.cpp
    src/plugin-launcher.cpp
    src/plugin-ui.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# ── Worker standalone executable ─────────────────────────────────────────────

add_executable(rizzytos-worker
  worker/main.cpp
  worker/args.cpp
  worker/concat.cpp
  worker/progress.cpp
)

target_include_directories(rizzytos-worker PRIVATE worker)
set_property(TARGET rizzytos-worker PROPERTY CXX_STANDARD 17)

install(TARGETS rizzytos-worker
  RUNTIME DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit"
  BUNDLE  DESTINATION "."
)

# ── Worker unit tests ─────────────────────────────────────────────────────────

if(BUILD_TESTING)
  enable_testing()

  add_executable(test_worker_args
    tests/worker/test_args.cpp
    worker/args.cpp
  )
  target_include_directories(test_worker_args PRIVATE worker)
  set_property(TARGET test_worker_args PROPERTY CXX_STANDARD 17)
  add_test(NAME worker_args COMMAND test_worker_args)

  add_executable(test_worker_progress
    tests/worker/test_progress.cpp
    worker/progress.cpp
  )
  target_include_directories(test_worker_progress PRIVATE worker)
  set_property(TARGET test_worker_progress PROPERTY CXX_STANDARD 17)
  add_test(NAME worker_progress COMMAND test_worker_progress)

  add_executable(test_worker_concat
    tests/worker/test_concat.cpp
    worker/concat.cpp
    worker/progress.cpp
  )
  target_include_directories(test_worker_concat PRIVATE worker)
  set_property(TARGET test_worker_concat PROPERTY CXX_STANDARD 17)
  add_test(NAME worker_concat COMMAND test_worker_concat)
endif()
```

- [ ] **Step 3: Create tests directory**

```bash
mkdir -p tests/worker
```

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt CMakePresets.json tests/
git commit -m "build: enable Qt + frontend API, add worker target and test scaffolding"
```

---

## Task 3: Locale strings

**Files:**
- Modify: `data/locale/en-US.ini`

- [ ] **Step 1: Write all UI strings**

```ini
RizzyTos.Button.Record=Grabar y editar auto
RizzyTos.Button.Stop=Detener grabación
RizzyTos.Dock.Title=RizzyTos Auto Edit
RizzyTos.Settings.OutputDir=Ruta de salida
RizzyTos.Settings.OutputName=Nombre
RizzyTos.Settings.Intro=Intro
RizzyTos.Settings.Outro=Outro
RizzyTos.Settings.Browse=...
RizzyTos.Status.Idle=Listo
RizzyTos.Status.Processing=Procesando...
RizzyTos.Status.Done=¡Listo! Video guardado en:
RizzyTos.Status.Error=Error:
RizzyTos.Status.Stale=El worker dejó de responder.
RizzyTos.Error.NoRecording=No se encontró el archivo grabado.
RizzyTos.Error.NoOutputDir=El directorio de salida no existe.
```

- [ ] **Step 2: Commit**

```bash
git add data/locale/en-US.ini
git commit -m "feat: add locale strings for RizzyTos Auto Edit"
```

---

## Task 4: Worker — `WorkerArgs` (TDD)

**Files:**
- Create: `worker/args.h`
- Create: `worker/args.cpp`
- Create: `tests/worker/test_args.cpp`

- [ ] **Step 1: Write failing test**

`tests/worker/test_args.cpp`:

```cpp
#include <cassert>
#include <cstdio>
#include <cstring>
#include "args.h"

static void test_full_args() {
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
        "--progress", "/tmp/prog.txt"
    };
    int argc = 19;
    WorkerArgs args = parse_args(argc, const_cast<char **>(argv));
    assert(args.input    == "/tmp/rec.mkv");
    assert(args.intro    == "/tmp/intro.mp4");
    assert(args.outro    == "/tmp/outro.mp4");
    assert(args.output   == "/tmp/out.mp4");
    assert(args.ffmpeg   == "/usr/bin/ffmpeg");
    assert(args.vcodec   == "h264");
    assert(args.encoder  == "software");
    assert(args.bitrate  == 8000);
    assert(args.progress == "/tmp/prog.txt");
    printf("test_full_args: PASS\n");
}

static void test_optional_intro_outro() {
    const char *argv[] = {
        "worker",
        "--input",    "/tmp/rec.mkv",
        "--output",   "/tmp/out.mp4",
        "--ffmpeg",   "/usr/bin/ffmpeg",
        "--vcodec",   "h264",
        "--encoder",  "nvenc",
        "--bitrate",  "6000",
        "--progress", "/tmp/p.txt"
    };
    int argc = 15;
    WorkerArgs args = parse_args(argc, const_cast<char **>(argv));
    assert(args.intro.empty());
    assert(args.outro.empty());
    assert(args.bitrate == 6000);
    printf("test_optional_intro_outro: PASS\n");
}

static void test_missing_required_throws() {
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

int main() {
    test_full_args();
    test_optional_intro_outro();
    test_missing_required_throws();
    printf("All arg tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Verify it does not compile yet**

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DENABLE_FRONTEND_API=OFF -DENABLE_QT=OFF 2>&1 | grep -i "error\|Cannot"
```

Expected: CMake configure error because `args.h` doesn't exist yet.

- [ ] **Step 3: Create `worker/args.h`**

```cpp
#pragma once
#include <string>
#include <stdexcept>

struct WorkerArgs {
    std::string input;
    std::string intro;    // optional
    std::string outro;    // optional
    std::string output;
    std::string ffmpeg;
    std::string vcodec;   // "h264" | "hevc" | "av1"
    std::string encoder;  // "nvenc" | "qsv" | "amf" | "videotoolbox" | "software"
    int         bitrate;  // kbps
    std::string progress; // path to progress file
};

// Parses CLI arguments into WorkerArgs.
// Throws std::invalid_argument if a required field is missing.
WorkerArgs parse_args(int argc, char *argv[]);
```

- [ ] **Step 4: Create `worker/args.cpp`**

```cpp
#include "args.h"
#include <unordered_map>
#include <stdexcept>

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
    args.input    = require("--input");
    args.output   = require("--output");
    args.ffmpeg   = require("--ffmpeg");
    args.vcodec   = require("--vcodec");
    args.encoder  = require("--encoder");
    args.bitrate  = std::stoi(require("--bitrate"));
    args.progress = require("--progress");

    auto it_intro = kv.find("--intro");
    if (it_intro != kv.end()) args.intro = it_intro->second;

    auto it_outro = kv.find("--outro");
    if (it_outro != kv.end()) args.outro = it_outro->second;

    return args;
}
```

- [ ] **Step 5: Build and run the test**

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DENABLE_FRONTEND_API=OFF -DENABLE_QT=OFF
cmake --build build --target test_worker_args
./build/test_worker_args
```

Expected output:
```
test_full_args: PASS
test_optional_intro_outro: PASS
test_missing_required_throws: PASS
All arg tests passed.
```

- [ ] **Step 6: Commit**

```bash
git add worker/args.h worker/args.cpp tests/worker/test_args.cpp
git commit -m "feat(worker): add WorkerArgs struct and parse_args()"
```

---

## Task 5: Worker — `ProgressWriter` and `parse_duration_seconds` (TDD)

**Files:**
- Create: `worker/progress.h`
- Create: `worker/progress.cpp`
- Create: `tests/worker/test_progress.cpp`

- [ ] **Step 1: Write failing tests**

`tests/worker/test_progress.cpp`:

```cpp
#include <cassert>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>
#include "progress.h"

static void test_parse_duration_simple() {
    double d = parse_duration_seconds("  Duration: 00:01:23.45, start: ...");
    assert(std::fabs(d - 83.45) < 0.01);
    printf("test_parse_duration_simple: PASS\n");
}

static void test_parse_duration_hours() {
    double d = parse_duration_seconds("Duration: 01:00:00.00,");
    assert(std::fabs(d - 3600.0) < 0.01);
    printf("test_parse_duration_hours: PASS\n");
}

static void test_parse_duration_not_found() {
    double d = parse_duration_seconds("Some other ffmpeg output line");
    assert(d < 0.0);
    printf("test_parse_duration_not_found: PASS\n");
}

static void test_progress_writer_writes_value() {
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

static void test_progress_writer_writes_error() {
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

int main() {
    test_parse_duration_simple();
    test_parse_duration_hours();
    test_parse_duration_not_found();
    test_progress_writer_writes_value();
    test_progress_writer_writes_error();
    printf("All progress tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Create `worker/progress.h`**

```cpp
#pragma once
#include <string>

// Parses a Duration line from ffmpeg stderr output.
// Returns total seconds, or -1.0 if not found.
// Input example: "  Duration: 00:01:23.45, start: 0.000000, bitrate: ..."
double parse_duration_seconds(const std::string &line);

// Writes progress updates to a file read by the plugin.
class ProgressWriter {
public:
    explicit ProgressWriter(const std::string &path);

    // Writes an integer 0-100.
    void write(int percent);

    // Writes "error: <message>".
    void write_error(const std::string &message);

private:
    std::string path_;
};
```

- [ ] **Step 3: Create `worker/progress.cpp`**

```cpp
#include "progress.h"
#include <fstream>
#include <regex>
#include <cmath>

double parse_duration_seconds(const std::string &line)
{
    std::smatch m;
    static const std::regex re(R"(Duration:\s+(\d+):(\d+):(\d+)\.(\d+))");
    if (!std::regex_search(line, m, re))
        return -1.0;

    double h  = std::stod(m[1].str());
    double mn = std::stod(m[2].str());
    double s  = std::stod(m[3].str());
    double frac = std::stod(m[4].str()) / std::pow(10.0, m[4].str().size());
    return h * 3600.0 + mn * 60.0 + s + frac;
}

ProgressWriter::ProgressWriter(const std::string &path) : path_(path) {}

void ProgressWriter::write(int percent)
{
    std::ofstream f(path_, std::ios::trunc);
    f << percent;
}

void ProgressWriter::write_error(const std::string &message)
{
    std::ofstream f(path_, std::ios::trunc);
    f << "error: " << message;
}
```

- [ ] **Step 4: Build and run tests**

```bash
cmake --build build --target test_worker_progress
./build/test_worker_progress
```

Expected:
```
test_parse_duration_simple: PASS
test_parse_duration_hours: PASS
test_parse_duration_not_found: PASS
test_progress_writer_writes_value: PASS
test_progress_writer_writes_error: PASS
All progress tests passed.
```

- [ ] **Step 5: Commit**

```bash
git add worker/progress.h worker/progress.cpp tests/worker/test_progress.cpp
git commit -m "feat(worker): add ProgressWriter and parse_duration_seconds()"
```

---

## Task 6: Worker — FFmpeg command builder (TDD)

**Files:**
- Create: `worker/concat.h`
- Create: `worker/concat.cpp`
- Create: `tests/worker/test_concat.cpp`

- [ ] **Step 1: Write failing tests**

`tests/worker/test_concat.cpp`:

```cpp
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "concat.h"

static void test_three_inputs_nvenc_h264() {
    FfmpegSpec spec;
    spec.ffmpeg  = "/usr/bin/ffmpeg";
    spec.intro   = "/tmp/intro.mp4";
    spec.input   = "/tmp/rec.mkv";
    spec.outro   = "/tmp/outro.mp4";
    spec.output  = "/tmp/out.mp4";
    spec.vcodec  = "h264";
    spec.encoder = "nvenc";
    spec.bitrate = 8000;

    auto args = build_ffmpeg_args(spec);
    // Should have -i intro, -i input, -i outro
    int input_count = 0;
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == "-i") input_count++;
    }
    assert(input_count == 3);
    // Should contain h264_nvenc
    bool found_encoder = false;
    for (const auto &a : args)
        if (a == "h264_nvenc") { found_encoder = true; break; }
    assert(found_encoder);
    // Should contain concat filter
    bool found_concat = false;
    for (const auto &a : args)
        if (a.find("concat=n=3") != std::string::npos) { found_concat = true; break; }
    assert(found_concat);
    printf("test_three_inputs_nvenc_h264: PASS\n");
}

static void test_input_only_software_hevc() {
    FfmpegSpec spec;
    spec.ffmpeg  = "/usr/bin/ffmpeg";
    spec.intro   = "";
    spec.input   = "/tmp/rec.mkv";
    spec.outro   = "";
    spec.output  = "/tmp/out.mp4";
    spec.vcodec  = "hevc";
    spec.encoder = "software";
    spec.bitrate = 4000;

    auto args = build_ffmpeg_args(spec);
    int input_count = 0;
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == "-i") input_count++;
    }
    assert(input_count == 1);
    bool found_encoder = false;
    for (const auto &a : args)
        if (a == "libx265") { found_encoder = true; break; }
    assert(found_encoder);
    // No concat filter needed for single input
    bool found_concat = false;
    for (const auto &a : args)
        if (a.find("concat") != std::string::npos) { found_concat = true; break; }
    assert(!found_concat);
    printf("test_input_only_software_hevc: PASS\n");
}

static void test_intro_only() {
    FfmpegSpec spec;
    spec.ffmpeg  = "/usr/bin/ffmpeg";
    spec.intro   = "/tmp/intro.mp4";
    spec.input   = "/tmp/rec.mkv";
    spec.outro   = "";
    spec.output  = "/tmp/out.mkv";
    spec.vcodec  = "h264";
    spec.encoder = "software";
    spec.bitrate = 6000;

    auto args = build_ffmpeg_args(spec);
    int input_count = 0;
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == "-i") input_count++;
    }
    assert(input_count == 2);
    bool found_concat = false;
    for (const auto &a : args)
        if (a.find("concat=n=2") != std::string::npos) { found_concat = true; break; }
    assert(found_concat);
    printf("test_intro_only: PASS\n");
}

int main() {
    test_three_inputs_nvenc_h264();
    test_input_only_software_hevc();
    test_intro_only();
    printf("All concat tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Create `worker/concat.h`**

```cpp
#pragma once
#include <string>
#include <vector>

struct FfmpegSpec {
    std::string ffmpeg;   // path to ffmpeg binary
    std::string intro;    // optional; empty = skip
    std::string input;    // required; main recording
    std::string outro;    // optional; empty = skip
    std::string output;   // required; output file path
    std::string vcodec;   // "h264" | "hevc" | "av1"
    std::string encoder;  // "nvenc" | "qsv" | "amf" | "videotoolbox" | "software"
    int         bitrate;  // kbps
};

// Returns the FFmpeg argument list (without the program name).
// The first element is the ffmpeg binary path.
std::vector<std::string> build_ffmpeg_args(const FfmpegSpec &spec);

// Runs FFmpeg with the given spec. Reads stderr for progress and
// writes 0-100 to the progress file. Returns ffmpeg exit code.
int run_ffmpeg(const FfmpegSpec &spec, const std::string &progress_path);
```

- [ ] **Step 3: Create `worker/concat.cpp`**

```cpp
#include "concat.h"
#include "progress.h"
#include <sstream>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#define POPEN  _popen
#define PCLOSE _pclose
#else
#define POPEN  popen
#define PCLOSE pclose
#endif

static std::string ffmpeg_encoder_name(const std::string &vcodec, const std::string &encoder)
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

    // Add inputs in order: intro (optional), main input, outro (optional)
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
        // Build filter_complex for concatenation
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

// Probe total duration by running "ffmpeg -i <file>" and parsing stderr.
static double probe_duration(const std::string &ffmpeg, const std::string &file)
{
    std::string cmd = "\"" + ffmpeg + "\" -i \"" + file + "\" 2>&1";
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

// Parse "out_time_ms=N" line from ffmpeg -progress output (microseconds).
static double parse_out_time_ms(const char *line)
{
    if (strncmp(line, "out_time_ms=", 12) != 0) return -1.0;
    double us = std::stod(line + 12);
    return us / 1e6; // convert to seconds
}

int run_ffmpeg(const FfmpegSpec &spec, const std::string &progress_path)
{
    ProgressWriter pw(progress_path);
    pw.write(0);

    // Probe total duration (sum of all inputs)
    std::vector<std::string> probe_files;
    if (!spec.intro.empty()) probe_files.push_back(spec.intro);
    probe_files.push_back(spec.input);
    if (!spec.outro.empty()) probe_files.push_back(spec.outro);

    double total_duration = 0.0;
    for (const auto &f : probe_files) {
        double d = probe_duration(spec.ffmpeg, f);
        if (d > 0.0) total_duration += d;
    }

    // Build command string for popen
    auto args = build_ffmpeg_args(spec);
    std::ostringstream cmd;
    for (const auto &a : args) {
        cmd << "\"" << a << "\" ";
    }
    cmd << "2>&1";

    FILE *fp = POPEN(cmd.str().c_str(), "r");
    if (!fp) {
        pw.write_error("Failed to launch ffmpeg");
        return 2;
    }

    char buf[512];
    std::string last_error_line;
    while (fgets(buf, sizeof(buf), fp)) {
        // Lines from -progress pipe:1 look like "key=value"
        double current = parse_out_time_ms(buf);
        if (current >= 0.0 && total_duration > 0.0) {
            int pct = static_cast<int>((current / total_duration) * 100.0);
            if (pct > 100) pct = 100;
            pw.write(pct);
        }
        // Track last non-empty line for error reporting
        if (buf[0] != '\n' && strlen(buf) > 1)
            last_error_line = buf;
    }

    int ret = PCLOSE(fp);
    if (ret != 0) {
        pw.write_error(last_error_line.empty() ? "ffmpeg failed" : last_error_line);
        return 2;
    }

    pw.write(100);
    return 0;
}
```

- [ ] **Step 4: Build and run tests**

```bash
cmake --build build --target test_worker_concat
./build/test_worker_concat
```

Expected:
```
test_three_inputs_nvenc_h264: PASS
test_input_only_software_hevc: PASS
test_intro_only: PASS
All concat tests passed.
```

- [ ] **Step 5: Commit**

```bash
git add worker/concat.h worker/concat.cpp tests/worker/test_concat.cpp
git commit -m "feat(worker): add FFmpeg command builder and run_ffmpeg()"
```

---

## Task 7: Worker — `main.cpp` entry point

**Files:**
- Create: `worker/main.cpp`

- [ ] **Step 1: Create `worker/main.cpp`**

```cpp
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include "args.h"
#include "concat.h"
#include "progress.h"

static bool dir_exists(const std::string &path)
{
    // Extract directory part from output path
    size_t sep = path.find_last_of("/\\");
    if (sep == std::string::npos) return true; // current dir
    std::string dir = path.substr(0, sep);
#ifdef _WIN32
    struct _stat st;
    return _stat(dir.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR);
#else
    struct stat st;
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

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
    spec.ffmpeg  = args.ffmpeg;
    spec.intro   = args.intro;
    spec.input   = args.input;
    spec.outro   = args.outro;
    spec.output  = args.output;
    spec.vcodec  = args.vcodec;
    spec.encoder = args.encoder;
    spec.bitrate = args.bitrate;

    return run_ffmpeg(spec, args.progress);
}
```

- [ ] **Step 2: Build the worker binary**

```bash
cmake --build build --target rizzytos-worker
./build/rizzytos-worker 2>&1 | head -3
```

Expected: prints "rizzytos-worker: Missing required argument: --input" (exit code 1).

- [ ] **Step 3: Commit**

```bash
git add worker/main.cpp
git commit -m "feat(worker): add main.cpp entry point"
```

---

## Task 8: Plugin settings module

**Files:**
- Create: `src/plugin-settings.h`
- Create: `src/plugin-settings.cpp`

These files have no OBS-header dependencies beyond `obs-data.h` (pulled via `obs-module.h`).

- [ ] **Step 1: Create `src/plugin-settings.h`**

```cpp
#pragma once
#include <string>

struct PluginSettings {
    std::string output_dir;
    std::string output_name_template = "rizzytos_%Y-%m-%d_%H-%M-%S";
    std::string intro_path;
    std::string outro_path;
};

// Load from JSON file. Returns defaults if file does not exist.
PluginSettings settings_load(const char *config_path);

// Save to JSON file (overwrites).
void settings_save(const PluginSettings &s, const char *config_path);
```

- [ ] **Step 2: Create `src/plugin-settings.cpp`**

```cpp
#include "plugin-settings.h"
#include <obs-data.h>
#include <cstring>

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
```

- [ ] **Step 3: Commit**

```bash
git add src/plugin-settings.h src/plugin-settings.cpp
git commit -m "feat(plugin): add settings load/save module"
```

---

## Task 9: Plugin recorder module

**Files:**
- Create: `src/plugin-recorder.h`
- Create: `src/plugin-recorder.cpp`

This module reads OBS encoder settings and manages the `our_recording_active` flag.

- [ ] **Step 1: Create `src/plugin-recorder.h`**

```cpp
#pragma once
#include <string>

struct OBSEncoderInfo {
    std::string vcodec;   // "h264" | "hevc" | "av1"
    std::string encoder;  // "nvenc" | "qsv" | "amf" | "videotoolbox" | "software"
    int         bitrate_kbps = 8000;
    std::string ext;      // file extension without dot, e.g. "mp4" or "mkv"
};

// Call obs_frontend_recording_start(); set our_recording_active = true.
// Does nothing if OBS is already recording.
void recorder_start_our_recording(bool &our_recording_active);

// Call obs_frontend_recording_stop().
void recorder_stop_our_recording();

// Get the path of the file that was just recorded.
// Must be called after OBS_FRONTEND_EVENT_RECORDING_STOPPED fires.
// Returns empty string on failure.
std::string recorder_get_last_recording_path();

// Read the current recording output encoder settings from OBS.
OBSEncoderInfo recorder_read_encoder_info(const std::string &recording_path);

// Expand strftime-style template (e.g. "video_%Y-%m-%d") with current local time.
std::string recorder_expand_template(const std::string &tmpl);
```

- [ ] **Step 2: Create `src/plugin-recorder.cpp`**

```cpp
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

static std::string map_obs_encoder(const char *id, const std::string &vcodec)
{
    if (!id) return "software";
    std::string sid = id;
    if (sid == "jim_nvenc"  || sid == "ffmpeg_nvenc" ||
        sid == "jim_hevc_nvenc" || sid == "ffmpeg_hevc_nvenc") return "nvenc";
    if (sid.rfind("obs_qsv", 0) == 0)  return "qsv";
    if (sid.rfind("amd_amf", 0) == 0)  return "amf";
    if (sid.rfind("com.apple.videotoolbox", 0) == 0) return "videotoolbox";
    return "software";
}

static std::string map_obs_vcodec(const char *id)
{
    if (!id) return "h264";
    std::string sid = id;
    if (sid.find("hevc") != std::string::npos || sid.find("h265") != std::string::npos)
        return "hevc";
    if (sid.find("av1") != std::string::npos)
        return "av1";
    return "h264";
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
    info.encoder = map_obs_encoder(enc_id, info.vcodec);

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
```

- [ ] **Step 3: Commit**

```bash
git add src/plugin-recorder.h src/plugin-recorder.cpp
git commit -m "feat(plugin): add recorder module (OBS encoder reading, state management)"
```

---

## Task 10: Plugin launcher module

**Files:**
- Create: `src/plugin-launcher.h`
- Create: `src/plugin-launcher.cpp`

This module spawns `rizzytos-worker` as a detached process and tracks the progress file path.

- [ ] **Step 1: Create `src/plugin-launcher.h`**

```cpp
#pragma once
#include <string>
#include "plugin-settings.h"
#include "plugin-recorder.h"

class PluginLauncher {
public:
    // Returns the progress file path (available after a successful launch()).
    const std::string &progress_path() const { return progress_path_; }

    // Spawns rizzytos-worker as a detached process.
    // Returns true on success. On failure, writes to progress_path_ with "error: ...".
    bool launch(const std::string    &input_path,
                const PluginSettings &settings,
                const OBSEncoderInfo &encoder_info);

private:
    std::string progress_path_;

    std::string find_worker_binary() const;
    std::string find_ffmpeg_binary() const;
};
```

- [ ] **Step 2: Create `src/plugin-launcher.cpp`**

```cpp
#include "plugin-launcher.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QProcess>
#include <QDir>
#include <QUuid>
#include <QString>
#include <QStringList>

std::string PluginLauncher::find_worker_binary() const
{
    // Worker lives next to the plugin binary
    const char *bin_path = obs_get_module_binary_path(obs_current_module());
    if (!bin_path) return "";

    QFileInfo plugin_file(QString::fromUtf8(bin_path));
    QDir bin_dir = plugin_file.absoluteDir();

#ifdef _WIN32
    QString worker = bin_dir.filePath("rizzytos-worker.exe");
#else
    QString worker = bin_dir.filePath("rizzytos-worker");
#endif
    return worker.toStdString();
}

std::string PluginLauncher::find_ffmpeg_binary() const
{
    const char *data_path = obs_get_module_data_path(obs_current_module());
    if (!data_path) return "ffmpeg";

    QDir data_dir(QString::fromUtf8(data_path));
#ifdef _WIN32
    return data_dir.filePath("ffmpeg.exe").toStdString();
#else
    return data_dir.filePath("ffmpeg").toStdString();
#endif
}

bool PluginLauncher::launch(const std::string    &input_path,
                             const PluginSettings &settings,
                             const OBSEncoderInfo &encoder_info)
{
    // Generate unique progress file path
    QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
    progress_path_ = QDir::tempPath().toStdString()
                   + "/rizzytos_" + uuid.toStdString() + ".txt";

    // Build output file path: dir + "/" + expanded_name + "." + ext
    std::string expanded_name = recorder_expand_template(settings.output_name_template);
    std::string output_path = settings.output_dir
                            + "/" + expanded_name
                            + "." + encoder_info.ext;

    std::string worker = find_worker_binary();
    std::string ffmpeg = find_ffmpeg_binary();

    QStringList args;
    args << "--input"    << QString::fromStdString(input_path)
         << "--output"   << QString::fromStdString(output_path)
         << "--ffmpeg"   << QString::fromStdString(ffmpeg)
         << "--vcodec"   << QString::fromStdString(encoder_info.vcodec)
         << "--encoder"  << QString::fromStdString(encoder_info.encoder)
         << "--bitrate"  << QString::number(encoder_info.bitrate_kbps)
         << "--progress" << QString::fromStdString(progress_path_);

    if (!settings.intro_path.empty())
        args << "--intro" << QString::fromStdString(settings.intro_path);
    if (!settings.outro_path.empty())
        args << "--outro" << QString::fromStdString(settings.outro_path);

    qint64 pid = 0;
    bool ok = QProcess::startDetached(QString::fromStdString(worker), args,
                                      QString(), &pid);
    return ok;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/plugin-launcher.h src/plugin-launcher.cpp
git commit -m "feat(plugin): add launcher module (spawns worker as detached process)"
```

---

## Task 11: Plugin UI — settings dock widget

**Files:**
- Create: `src/plugin-ui.h`
- Create: `src/plugin-ui.cpp`

- [ ] **Step 1: Create `src/plugin-ui.h`**

```cpp
#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include "plugin-settings.h"

class AutoEditDock : public QWidget {
    Q_OBJECT
public:
    explicit AutoEditDock(QWidget *parent = nullptr);

    PluginSettings get_settings() const;
    void set_settings(const PluginSettings &s);

    // Begin polling progress_file_path every 500 ms.
    void start_progress(const std::string &progress_file_path,
                        const std::string &output_path);
    void stop_progress();

signals:
    void settings_changed(const PluginSettings &s);

private slots:
    void browse_output_dir();
    void browse_intro();
    void browse_outro();
    void on_poll_timer();

private:
    QLineEdit    *output_dir_edit_  = nullptr;
    QLineEdit    *output_name_edit_ = nullptr;
    QLineEdit    *intro_edit_       = nullptr;
    QLineEdit    *outro_edit_       = nullptr;
    QProgressBar *progress_bar_     = nullptr;
    QLabel       *status_label_     = nullptr;
    QTimer       *poll_timer_       = nullptr;

    std::string   progress_file_;
    std::string   output_path_;
    QDateTime     last_progress_change_;
    int           last_progress_value_ = -1;
};

// Inject a QPushButton into OBS's controls dock, directly after the native
// record button. Returns nullptr and logs a warning if the widget is not found.
QPushButton *inject_record_button(QWidget *main_window);
```

- [ ] **Step 2: Create `src/plugin-ui.cpp`**

```cpp
#include "plugin-ui.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMainWindow>
#include <QDockWidget>
#include <QSizePolicy>
#include <fstream>
#include <sstream>

// ── AutoEditDock ─────────────────────────────────────────────────────────────

AutoEditDock::AutoEditDock(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto make_row = [&](const char *label_text, QLineEdit *&edit,
                        const char *browse_slot) {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(obs_module_text(label_text), this);
        lbl->setFixedWidth(90);
        edit = new QLineEdit(this);
        auto *btn = new QPushButton(obs_module_text("RizzyTos.Settings.Browse"), this);
        btn->setFixedWidth(30);
        row->addWidget(lbl);
        row->addWidget(edit);
        row->addWidget(btn);
        layout->addLayout(row);
        connect(btn, &QPushButton::clicked, this, browse_slot);
    };

    // Output directory
    make_row("RizzyTos.Settings.OutputDir", output_dir_edit_,
             SLOT(browse_output_dir()));

    // Output name template
    {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(obs_module_text("RizzyTos.Settings.OutputName"), this);
        lbl->setFixedWidth(90);
        output_name_edit_ = new QLineEdit(this);
        row->addWidget(lbl);
        row->addWidget(output_name_edit_);
        layout->addLayout(row);
    }

    // Intro
    make_row("RizzyTos.Settings.Intro", intro_edit_, SLOT(browse_intro()));
    // Outro
    make_row("RizzyTos.Settings.Outro", outro_edit_, SLOT(browse_outro()));

    // Separator
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    layout->addWidget(sep);

    // Status label
    status_label_ = new QLabel(obs_module_text("RizzyTos.Status.Idle"), this);
    layout->addWidget(status_label_);

    // Progress bar (hidden by default)
    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 100);
    progress_bar_->hide();
    layout->addWidget(progress_bar_);

    layout->addStretch();

    // Connect field changes → emit settings_changed
    auto emit_changed = [this]() { emit settings_changed(get_settings()); };
    connect(output_dir_edit_,  &QLineEdit::editingFinished, this, emit_changed);
    connect(output_name_edit_, &QLineEdit::editingFinished, this, emit_changed);
    connect(intro_edit_,       &QLineEdit::editingFinished, this, emit_changed);
    connect(outro_edit_,       &QLineEdit::editingFinished, this, emit_changed);

    // Poll timer
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(500);
    connect(poll_timer_, &QTimer::timeout, this, &AutoEditDock::on_poll_timer);
}

PluginSettings AutoEditDock::get_settings() const
{
    PluginSettings s;
    s.output_dir           = output_dir_edit_->text().toStdString();
    s.output_name_template = output_name_edit_->text().toStdString();
    s.intro_path           = intro_edit_->text().toStdString();
    s.outro_path           = outro_edit_->text().toStdString();
    return s;
}

void AutoEditDock::set_settings(const PluginSettings &s)
{
    output_dir_edit_->setText(QString::fromStdString(s.output_dir));
    output_name_edit_->setText(QString::fromStdString(s.output_name_template));
    intro_edit_->setText(QString::fromStdString(s.intro_path));
    outro_edit_->setText(QString::fromStdString(s.outro_path));
}

void AutoEditDock::start_progress(const std::string &progress_file_path,
                                   const std::string &output_path)
{
    progress_file_       = progress_file_path;
    output_path_         = output_path;
    last_progress_value_ = -1;
    last_progress_change_= QDateTime::currentDateTime();

    progress_bar_->setValue(0);
    progress_bar_->show();
    status_label_->setText(obs_module_text("RizzyTos.Status.Processing"));
    poll_timer_->start();
}

void AutoEditDock::stop_progress()
{
    poll_timer_->stop();
    progress_bar_->hide();
}

void AutoEditDock::on_poll_timer()
{
    std::ifstream f(progress_file_);
    if (!f.is_open()) return;

    std::string content;
    std::getline(f, content);
    f.close();

    if (content.empty()) return;

    if (content.rfind("error: ", 0) == 0) {
        stop_progress();
        std::string msg = std::string(obs_module_text("RizzyTos.Status.Error"))
                        + " " + content.substr(7);
        status_label_->setText(QString::fromStdString(msg));
        return;
    }

    int pct = std::stoi(content);
    if (pct != last_progress_value_) {
        last_progress_value_ = pct;
        last_progress_change_ = QDateTime::currentDateTime();
        progress_bar_->setValue(pct);
    }

    // Stale detection: no change for 30 seconds
    if (last_progress_change_.secsTo(QDateTime::currentDateTime()) > 30) {
        stop_progress();
        status_label_->setText(obs_module_text("RizzyTos.Status.Stale"));
        return;
    }

    if (pct >= 100) {
        stop_progress();
        QString done = QString(obs_module_text("RizzyTos.Status.Done"))
                     + " " + QString::fromStdString(output_path_);
        status_label_->setText(done);
    }
}

void AutoEditDock::browse_output_dir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, obs_module_text("RizzyTos.Settings.OutputDir"),
        output_dir_edit_->text());
    if (!dir.isEmpty()) {
        output_dir_edit_->setText(dir);
        emit settings_changed(get_settings());
    }
}

void AutoEditDock::browse_intro()
{
    QString f = QFileDialog::getOpenFileName(
        this, obs_module_text("RizzyTos.Settings.Intro"),
        intro_edit_->text(),
        "Video files (*.mp4 *.mkv *.mov *.avi);;All files (*)");
    if (!f.isEmpty()) {
        intro_edit_->setText(f);
        emit settings_changed(get_settings());
    }
}

void AutoEditDock::browse_outro()
{
    QString f = QFileDialog::getOpenFileName(
        this, obs_module_text("RizzyTos.Settings.Outro"),
        outro_edit_->text(),
        "Video files (*.mp4 *.mkv *.mov *.avi);;All files (*)");
    if (!f.isEmpty()) {
        outro_edit_->setText(f);
        emit settings_changed(get_settings());
    }
}

// ── Button injection ──────────────────────────────────────────────────────────

QPushButton *inject_record_button(QWidget *main_window)
{
    if (!main_window) return nullptr;

    QPushButton *record_btn = main_window->findChild<QPushButton *>("recordButton");
    if (!record_btn) {
        obs_log(LOG_WARNING, "Could not find recordButton in OBS main window");
        return nullptr;
    }

    QWidget *parent = record_btn->parentWidget();
    if (!parent) return nullptr;

    QLayout *layout = parent->layout();
    if (!layout) return nullptr;

    // Find index of record_btn in the layout
    int idx = -1;
    for (int i = 0; i < layout->count(); i++) {
        if (layout->itemAt(i)->widget() == record_btn) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return nullptr;

    auto *btn = new QPushButton(obs_module_text("RizzyTos.Button.Record"), parent);
    btn->setObjectName("rizzytos_record_button");
    btn->setSizePolicy(record_btn->sizePolicy());
    btn->setMinimumHeight(record_btn->minimumHeight());

    auto *vbox = qobject_cast<QVBoxLayout *>(layout);
    if (vbox) {
        vbox->insertWidget(idx + 1, btn);
    } else {
        layout->addWidget(btn); // fallback
    }

    return btn;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/plugin-ui.h src/plugin-ui.cpp
git commit -m "feat(plugin): add AutoEditDock widget and inject_record_button()"
```

---

## Task 12: Plugin main — wire everything together

**Files:**
- Modify: `src/plugin-main.c` → rename to `src/plugin-main.cpp` and replace content

- [ ] **Step 1: Rename plugin-main.c to plugin-main.cpp**

```bash
git mv src/plugin-main.c src/plugin-main.cpp
```

- [ ] **Step 2: Replace content of `src/plugin-main.cpp`**

```cpp
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include "plugin-settings.h"
#include "plugin-recorder.h"
#include "plugin-launcher.h"
#include "plugin-ui.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// ── Module-level state ────────────────────────────────────────────────────────

static PluginSettings  g_settings;
static OBSEncoderInfo  g_encoder_info;
static PluginLauncher  g_launcher;
static bool            g_our_recording_active = false;
static char            g_config_path[1024]    = {};

static QPointer<AutoEditDock> g_dock;
static QPointer<QPushButton>  g_button;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void set_button_state_idle()
{
    if (!g_button) return;
    g_button->setText(obs_module_text("RizzyTos.Button.Record"));
    g_button->setEnabled(true);
}

static void set_button_state_recording()
{
    if (!g_button) return;
    g_button->setText(obs_module_text("RizzyTos.Button.Stop"));
    g_button->setEnabled(true);
}

static void set_button_state_processing()
{
    if (!g_button) return;
    g_button->setEnabled(false);
}

static void on_button_clicked()
{
    if (g_our_recording_active) {
        recorder_stop_our_recording();
    } else {
        recorder_start_our_recording(g_our_recording_active);
        if (g_our_recording_active)
            set_button_state_recording();
    }
}

static void trigger_auto_edit()
{
    std::string rec_path = recorder_get_last_recording_path();
    if (rec_path.empty()) {
        obs_log(LOG_WARNING, "Recording path is empty after stop event");
        set_button_state_idle();
        return;
    }

    g_encoder_info = recorder_read_encoder_info(rec_path);

    bool ok = g_launcher.launch(rec_path, g_settings, g_encoder_info);
    if (!ok) {
        obs_log(LOG_WARNING, "Failed to launch rizzytos-worker");
        set_button_state_idle();
        return;
    }

    set_button_state_processing();

    // Build expected output path for the "Done" message
    std::string expanded = recorder_expand_template(g_settings.output_name_template);
    std::string out_path = g_settings.output_dir + "/" + expanded + "." + g_encoder_info.ext;

    if (g_dock)
        g_dock->start_progress(g_launcher.progress_path(), out_path);
}

static void on_dock_settings_changed(const PluginSettings &s)
{
    g_settings = s;
    settings_save(g_settings, g_config_path);
}

// ── OBS event callback ────────────────────────────────────────────────────────

static void obs_event_cb(enum obs_frontend_event event, void * /*private_data*/)
{
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
        // Inject button into controls dock
        QMainWindow *main_window =
            static_cast<QMainWindow *>(obs_frontend_get_main_window());
        g_button = inject_record_button(main_window);
        if (g_button)
            QObject::connect(g_button, &QPushButton::clicked, on_button_clicked);

        // Register settings dock
        auto *dock_widget = new AutoEditDock();
        g_dock = dock_widget;
        g_dock->set_settings(g_settings);
        QObject::connect(g_dock, &AutoEditDock::settings_changed,
                         on_dock_settings_changed);

        obs_frontend_add_dock_by_id("rizzytos_auto_edit_dock",
                                    obs_module_text("RizzyTos.Dock.Title"),
                                    dock_widget);
        break;
    }
    case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
        if (g_our_recording_active) {
            g_our_recording_active = false;
            trigger_auto_edit();
        }
        break;
    default:
        break;
    }
}

// ── Module lifecycle ──────────────────────────────────────────────────────────

bool obs_module_load(void)
{
    obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

    // Resolve config path
    char *conf_dir = obs_module_get_config_path(obs_current_module(), "");
    if (conf_dir) {
        snprintf(g_config_path, sizeof(g_config_path), "%s/settings.json", conf_dir);
        bfree(conf_dir);
    }

    g_settings = settings_load(g_config_path);

    obs_frontend_add_event_callback(obs_event_cb, nullptr);
    return true;
}

void obs_module_unload(void)
{
    obs_log(LOG_INFO, "plugin unloaded");
    obs_frontend_remove_event_callback(obs_event_cb, nullptr);
}
```

- [ ] **Step 3: Build the full plugin**

On Windows (in a Developer PowerShell with OBS deps available):
```powershell
cmake --preset windows-ci-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

On macOS:
```bash
cmake --preset macos
cmake --build --preset macos --config RelWithDebInfo
```

Fix any compile errors. Common ones:
- Qt `SLOT()` macro needs `Q_OBJECT` — already present in `AutoEditDock`
- `obs_frontend_add_dock_by_id` — available since OBS 28; prototype in `obs-frontend-api.h`
- Missing `#include <QObject>` anywhere `connect()` is used

- [ ] **Step 4: Commit**

```bash
git add src/plugin-main.cpp
git rm src/plugin-main.c 2>/dev/null || true
git commit -m "feat(plugin): wire all modules in plugin-main.cpp"
```

---

## Task 13: FFmpeg bundling

**Files:**
- Modify: `.github/scripts/Build-Windows.ps1` (add FFmpeg download step)
- Modify: `.github/scripts/build-macos` (add FFmpeg download step)
- Modify: `CMakeLists.txt` (install FFmpeg into data/)

FFmpeg is downloaded from the official static builds and placed in `data/` so the cmake `target_install_resources` picks it up automatically.

- [ ] **Step 1: Add FFmpeg download to Windows build script**

At the end of the `Build` function in `.github/scripts/Build-Windows.ps1`, before the closing `}`, add:

```powershell
# Download bundled FFmpeg
$FfmpegUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
$FfmpegZip = "${env:TEMP}\ffmpeg.zip"
$FfmpegDir = "${env:TEMP}\ffmpeg_extract"

Log-Group "Downloading FFmpeg..."
Invoke-WebRequest -Uri $FfmpegUrl -OutFile $FfmpegZip -UseBasicParsing
Expand-Archive -Path $FfmpegZip -DestinationPath $FfmpegDir -Force
$FfmpegExe = Get-ChildItem -Path $FfmpegDir -Recurse -Filter "ffmpeg.exe" | Select-Object -First 1
Copy-Item $FfmpegExe.FullName "${ProjectRoot}/data/ffmpeg.exe" -Force
Log-Group
```

- [ ] **Step 2: Add FFmpeg download to macOS build script**

In `.github/scripts/build-macos`, inside the `build()` function, after the cmake install step, add:

```zsh
log_group "Downloading bundled FFmpeg..."
local ffmpeg_url="https://evermeet.cx/ffmpeg/getrelease/zip"
local ffmpeg_zip="/tmp/ffmpeg_macos.zip"
local ffmpeg_dir="/tmp/ffmpeg_macos_extract"

curl -L -o "${ffmpeg_zip}" "${ffmpeg_url}"
mkdir -p "${ffmpeg_dir}"
unzip -o "${ffmpeg_zip}" -d "${ffmpeg_dir}"
cp "${ffmpeg_dir}/ffmpeg" "${project_root}/data/ffmpeg"
chmod +x "${project_root}/data/ffmpeg"
log_group
```

- [ ] **Step 3: Add FFmpeg to .gitignore exclusions**

The `data/ffmpeg` and `data/ffmpeg.exe` binaries are not committed (they're downloaded at build time). Add them to `.gitignore`:

```
# (already ignored by the /* rule — no change needed)
```

They are already excluded because `.gitignore` only whitelists specific files. The `!/data` rule allows the `data/` directory itself but files inside it that aren't committed are not tracked. The ffmpeg binary should NOT be committed to the repo — it's too large and platform-specific.

However, `data/locale/en-US.ini` IS committed. Verify this is tracked:

```bash
git ls-files data/
```

Expected: `data/locale/en-US.ini` listed. If `data/` itself is not whitelisted in `.gitignore`, add `!/data` (same pattern used for `!/docs` in Task 1).

- [ ] **Step 4: Verify FFmpeg path resolution at runtime**

In `plugin-launcher.cpp`, `find_ffmpeg_binary()` uses `obs_get_module_data_path()`. In OBS, this resolves to the `data/<plugin-name>/` directory where resources are installed. Confirm the install path in `CMakeLists.txt` matches:

```cmake
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION "${target}/data" ...)
```

This is handled automatically by `target_install_resources()` in the cmake helpers. FFmpeg placed in `data/` will land at `<plugin>/data/ffmpeg(.exe)` after install.

- [ ] **Step 5: Commit**

```bash
git add .github/scripts/Build-Windows.ps1 .github/scripts/build-macos
git commit -m "build: download and bundle FFmpeg during CI build"
```

---

## Task 14: Run all worker tests via CTest

- [ ] **Step 1: Run full test suite**

```bash
cmake --build build --target test_worker_args test_worker_progress test_worker_concat
ctest --test-dir build -V
```

Expected output:
```
Test project /path/to/build
    Start 1: worker_args
1/3 Test #1: worker_args .......................   Passed    0.01 sec
    Start 2: worker_progress
2/3 Test #2: worker_progress ...................   Passed    0.01 sec
    Start 3: worker_concat
3/3 Test #3: worker_concat .....................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3
```

- [ ] **Step 2: Manual end-to-end checklist (requires OBS)**

Load the plugin in OBS (copy built artifacts to OBS plugins dir). Verify:

- [ ] "Grabar y editar auto" button appears immediately below the native record button
- [ ] Clicking it starts recording (OBS recording indicator lights up)
- [ ] Button text changes to "Detener grabación" while recording
- [ ] Clicking "Detener grabación" stops the recording
- [ ] After stopping: button grays out, dock panel shows "Procesando..." + progress bar
- [ ] Native OBS record button still works independently (no trigger of worker)
- [ ] If native button starts/stops recording, our button stays in "Grabar y editar auto" state
- [ ] Settings dock accessible from OBS Docks menu
- [ ] File pickers work for intro, outro, output directory
- [ ] Settings persist across OBS restarts
- [ ] After worker finishes: panel shows "¡Listo! Video guardado en: ..."
- [ ] Output file exists at configured path with intro + recording + outro

- [ ] **Step 3: Commit final state**

```bash
git add -A
git commit -m "chore: finalize RizzyTos Auto Edit v1.0.0"
```

---

## Self-Review

**Spec coverage check:**

| Spec requirement | Task |
|-----------------|------|
| Button below native record button | Task 11 (`inject_record_button`) |
| Button triggers OBS recording (same settings) | Task 12 (`recorder_start_our_recording`) |
| Only our button triggers worker on stop | Task 12 (`g_our_recording_active` flag) |
| External worker process, bundled | Tasks 4–7 (worker binary), Task 13 (bundling) |
| Worker concatenates intro + video + outro | Task 6 (`build_ffmpeg_args`) |
| Worker uses OBS encoding settings | Tasks 9, 10 (encoder info reading) |
| Settings panel: output path, name, intro, outro | Task 11 (`AutoEditDock`) |
| Progress indicator in panel | Task 11 (`start_progress`, `on_poll_timer`) |
| FFmpeg bundled | Task 13 |
| Branch rename master → main | Task 1 |
| Error handling (missing file, worker crash, stale, output dir missing) | Tasks 6, 11, 12 |

All spec requirements are covered.
