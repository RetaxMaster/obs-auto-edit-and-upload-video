# RizzyTos Auto Edit — Design Spec

**Date:** 2026-05-08
**Plugin name:** RizzyTos Auto Edit
**Target platforms:** Windows, macOS

---

## Overview

RizzyTos Auto Edit is an OBS Studio plugin that adds a "Grabar y editar automáticamente" button to OBS's native controls dock. When the user stops recording via this button, the plugin automatically launches an external worker process that concatenates a configured intro and outro video to the recording using FFmpeg, re-encoding with the same settings OBS used for the recording.

---

## Artifacts

Three artifacts are installed together:

```
obs-plugins/64bit/
  rizzytos-auto-edit.dll                    (Windows plugin)
  rizzytos-worker.exe                       (Windows worker)
  data/rizzytos-auto-edit/
    ffmpeg.exe                              (bundled FFmpeg, Windows)

# macOS layout:
obs-plugins/rizzytos-auto-edit.plugin/
  Contents/
    MacOS/rizzytos-auto-edit               (plugin dylib)
    Resources/
      ffmpeg                               (bundled FFmpeg, universal binary)
      rizzytos-worker                      (worker binary)
```

The plugin resolves all paths at runtime using `obs_get_module_data_path()` and `obs_get_module_binary_path()`. No absolute paths are hardcoded.

---

## CMake Build Structure

```cmake
# Plugin shared library (existing target)
add_library(rizzytos-auto-edit MODULE)
target_compile_options(... ENABLE_FRONTEND_API ON ENABLE_QT ON)

# Worker standalone executable (new target)
add_executable(rizzytos-worker worker/main.cpp)

# FFmpeg is downloaded at configure/package time and placed in data/
```

The worker is a separate CMake target with no OBS or Qt dependencies — only the C++ standard library.

---

## UI: Button Injection

The plugin injects a QPushButton into OBS's native controls dock at `obs_module_load` time, after the Qt event loop is running (via `obs_frontend_add_event_callback` on `OBS_FRONTEND_EVENT_FINISHED_LOADING`).

**Injection procedure:**
1. Get `QMainWindow*` from `obs_frontend_get_main_window()`
2. Find `QPushButton` with `objectName == "recordButton"` via `findChild`
3. Get the parent widget's `QVBoxLayout`
4. Insert our button immediately after the record button (`layout->insertWidget(index + 1, ourButton)`)

**Button states:**

| State | Text | Enabled |
|-------|------|---------|
| Idle | "Grabar y editar auto" | Yes |
| Recording (our button) | "Detener grabación" | Yes |
| Processing (worker running) | "Grabar y editar auto" | No (grayed out) |

The native OBS record button remains fully functional and unmodified.

---

## UI: Settings Dock Panel

A separate dock widget registered via `obs_frontend_add_dock_by_id()`, accessible from OBS's Docks menu. Layout:

```
┌─ RizzyTos Auto Edit ──────────────────────────────┐
│                                                    │
│  Ruta de salida:  [/Videos/editados      ] [...]  │
│  Nombre:          [rizzytos_%Y-%m-%d_%H-%M-%S    ] │
│                                                    │
│  Intro:           [/Videos/intro.mp4     ] [...]  │
│  Outro:           [/Videos/outro.mp4     ] [...]  │
│                                                    │
│  ─────────────────────────────────────────────── │
│  Estado: ● Listo                                   │
│  [████████████░░░░░░░░] 60%  Procesando...        │
└────────────────────────────────────────────────────┘
```

- `[...]` buttons open the OS native file dialog (`QFileDialog`)
- The progress bar and status line are hidden when no worker is active
- Output name supports strftime-style variables: `%Y`, `%m`, `%d`, `%H`, `%M`, `%S`
- Configuration is persisted to the plugin data directory as JSON using `obs_data_t` / `obs_data_save_json`

---

## Recording Flow

```
User clicks "Grabar y editar auto"
  → plugin sets our_recording_active = true
  → calls obs_frontend_recording_start()
  → button text changes to "Detener grabación"

User clicks "Detener grabación"
  → calls obs_frontend_recording_stop()

OBS fires OBS_FRONTEND_EVENT_RECORDING_STOPPED
  → plugin checks our_recording_active
  → if false: ignore (user used the native button)
  → if true: set our_recording_active = false, proceed

Plugin reads from OBS:
  - Recorded file path: obs_frontend_get_last_recording()
  - Video info: obs_get_video_info() → resolution, FPS
  - Record output: obs_frontend_get_recording_output()
  - Encoder: obs_output_get_video_encoder(output)
  - Encoder ID: obs_encoder_get_id(encoder) → identifies hw/sw
  - Encoder settings: obs_encoder_get_settings(encoder) → bitrate
  - Container: derived from file extension of recorded path

Plugin expands output filename template with current timestamp.

Plugin launches rizzytos-worker as detached process (see Worker section).

Plugin starts QTimer (500ms interval) to poll progress file.
Panel shows progress bar and "Procesando...".
```

---

## Worker: `rizzytos-worker`

A standalone C++ executable. No OBS or Qt dependency. Receives all inputs as CLI arguments:

```bash
rizzytos-worker \
  --input    /path/to/recording.mkv \
  --intro    /path/to/intro.mp4 \        # optional, omit if not set
  --outro    /path/to/outro.mp4 \        # optional, omit if not set
  --output   /path/to/output/video.mp4 \
  --ffmpeg   /path/to/bundled/ffmpeg \
  --vcodec   h264 \                      # h264 | hevc | av1
  --encoder  nvenc \                     # nvenc | qsv | amf | videotoolbox | software
  --bitrate  8000 \                      # kbps
  --progress /tmp/rizzytos_12345.txt
```

**FFmpeg encoder mapping:**

| OBS encoder ID | FFmpeg encoder arg |
|----------------|--------------------|
| `jim_nvenc` / `ffmpeg_nvenc` | `h264_nvenc` / `hevc_nvenc` |
| `obs_qsv11` | `h264_qsv` / `hevc_qsv` |
| `amd_amf_h264` / `amd_amf_hevc` | `h264_amf` / `hevc_amf` |
| `com.apple.videotoolbox.*` | `h264_videotoolbox` / `hevc_videotoolbox` |
| `obs_x264` / `obs_x265` | `libx264` / `libx265` |

**FFmpeg command (3 inputs):**

```bash
ffmpeg \
  -i intro.mp4 -i recording.mkv -i outro.mp4 \
  -filter_complex "[0:v][0:a][1:v][1:a][2:v][2:a]concat=n=3:v=1:a=1[v][a]" \
  -map "[v]" -map "[a]" \
  -c:v <encoder> -b:v <bitrate>k \
  -y output.mp4
```

If only intro or only outro is configured, `n` adjusts accordingly. If neither is configured, FFmpeg re-encodes the input with the target settings only (no concat filter).

**Progress parsing:**

FFmpeg writes `time=HH:MM:SS.xx` to stderr during encoding. The worker:
1. Probes total duration of all input files by running `ffmpeg -i <file>` and parsing the `Duration:` line from stderr
2. Parses each `time=` line from FFmpeg stderr
3. Computes `percent = (current_time / total_duration) * 100`
4. Writes the integer (0–100) to the progress file

**Exit codes:**
- `0` — success
- `1` — argument error
- `2` — FFmpeg process failed (non-zero exit from FFmpeg)
- `3` — output directory does not exist or is not writable

On failure, the worker writes `error: <description>` to the progress file before exiting.

---

## Progress Communication

**Progress file path:** `<system_temp_dir>/rizzytos_<worker_pid>.txt`

The plugin constructs the expected path by reading the worker's PID after spawning it. Contents are either:
- An integer string `"0"` to `"100"`
- A string starting with `"error: "` followed by a description

The plugin polls via `QTimer` (500ms). On reading `100` or an `error:` prefix, the timer stops. The panel updates accordingly:
- `0–99` → progress bar + "Procesando..."
- `100` → "¡Listo! Video guardado en `<output_path>`"
- `error:` → "Error: `<description>`" in red

**Stale worker detection:** if the progress file has not changed for 30 seconds, the plugin marks the state as errored with "El worker dejó de responder."

---

## Configuration Persistence

Settings are saved to `<obs_module_config_path>/rizzytos-auto-edit.json`:

```json
{
  "output_dir": "/Videos/editados",
  "output_name_template": "rizzytos_%Y-%m-%d_%H-%M-%S",
  "intro_path": "/Videos/intro.mp4",
  "outro_path": "/Videos/outro.mp4"
}
```

Loaded on `obs_module_load`, saved whenever the user changes a field.

---

## Error Handling

| Situation | Behavior |
|-----------|----------|
| Intro/outro not configured | Corresponding `--intro` / `--outro` args are omitted; worker skips them in concat |
| Both intro and outro absent | Worker re-encodes input only (no concat) |
| Recorded file path not found | Panel shows "Error: no se encontró el archivo grabado" |
| FFmpeg exits non-zero | Worker writes `error: <stderr tail>` to progress file; panel shows it |
| Worker process dies unexpectedly | Plugin detects stale progress file after 30s; marks as error |
| OBS closes while processing | Worker continues independently; on next OBS launch the panel shows idle state (no recovery of in-flight jobs) |
| Output directory missing | Worker exits with code 3; plugin shows error |
| Record button injected widget not found | Plugin logs a warning and skips injection; settings dock still registers normally |

---

## Branch Rename

The repository's default branch is renamed from `master` to `main` as part of initial setup. All CI workflow files referencing `master` are updated accordingly.

---

## Source File Layout

```
src/
  plugin-main.cpp          # obs_module_load/unload, event callbacks
  plugin-ui.cpp/h          # button injection, dock widget, QTimer polling
  plugin-settings.cpp/h    # config load/save, obs_data_t wrapper
  plugin-recorder.cpp/h    # recording state machine, OBS settings reader
  plugin-launcher.cpp/h    # spawns rizzytos-worker, manages progress file path
worker/
  main.cpp                 # rizzytos-worker entry point
  concat.cpp/h             # FFmpeg command builder and process runner
  progress.cpp/h           # progress file writer, FFmpeg stderr parser
data/
  locale/en-US.ini
  ffmpeg(.exe)             # bundled at package time
docs/
  superpowers/specs/
    2026-05-08-rizzytos-auto-edit-design.md
```
