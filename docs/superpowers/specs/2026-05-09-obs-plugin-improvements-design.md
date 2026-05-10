# OBS Plugin Improvements — Design Spec
**Date:** 2026-05-09
**Project:** rizzytos-auto-edit

---

## Overview

Three improvement areas:

1. **Configurable output resolution and format** — user selects target resolution (720p–4K) and container (mkv/mp4) from the dock panel. Worker adapts FFmpeg scaling and container accordingly.
2. **Record button re-enable fix** — button stays disabled after processing; add signal path from dock to plugin-main to re-enable it when the worker finishes.
3. **YouTube upload integration** — after editing is done, optionally upload the final video to YouTube using OAuth 2.0 PKCE + YouTube Data API v3.

---

## Feature 1 — Output Resolution and Format Selectors

### PluginSettings changes

Add two new fields to the `PluginSettings` struct:

```cpp
std::string output_resolution = "1080p"; // "720p" | "1080p" | "2k" | "4k"
std::string output_format = "mkv";       // "mkv" | "mp4"
```

Both are persisted in `settings.json` alongside existing fields.

Resolution-to-dimension mapping:

| Option | Width × Height |
|--------|---------------|
| 720p   | 1280 × 720    |
| 1080p  | 1920 × 1080   |
| 2K     | 2560 × 1440   |
| 4K     | 3840 × 2160   |

### UI changes (AutoEditDock)

Add two `QComboBox` widgets in the settings section of the dock, below existing path fields:

- **Resolución de salida**: items "720p (1280×720)", "1080p (1920×1080)", "2K (2560×1440)", "4K (3840×2160)". Default: 1080p.
- **Formato de salida**: items "MKV (.mkv)", "MP4 (.mp4)". Default: MKV.

Both trigger `settings_changed` on change.

New locale keys:
```ini
RizzyTos.Settings.OutputResolution=Resolución de salida
RizzyTos.Settings.OutputFormat=Formato de salida
```

### Launcher changes (plugin-launcher.cpp)

- The output file extension is derived from `output_format` ("mkv" or "mp4"), **not** from the recording's extension. This ensures the output container is always what the user selected, regardless of what OBS produced.
- Pass two new CLI arguments to the worker:
  - `--width <N>` (e.g. 1920)
  - `--height <N>` (e.g. 1080)
  - `--format <ext>` (e.g. "mp4")

Resolution is resolved in the launcher by mapping the `output_resolution` string to width/height before building the args list.

### Worker changes

**args.cpp/h** — add three new fields to `WorkerArgs`:
```cpp
int out_width;   // required
int out_height;  // required
std::string out_format; // "mkv" | "mp4"
```

**concat.cpp** — change scaling target:

- Previously: scale all inputs to match the recording's detected resolution.
- Now: scale all inputs to `out_width × out_height` from the CLI args.
- The `scale=WxH,setsar=1` filter is always applied to all inputs (intro, recording, outro) to the target resolution, regardless of each input's original format or resolution. This handles the case where intro is MP4 at 1080p, recording is MKV at 1440p, and the target output is 720p — FFmpeg demuxes each independently and scales each to the target.
- Explicitly pass `-f matroska` (for mkv) or `-f mp4` (for mp4) to the FFmpeg command to guarantee the output container, not relying solely on file extension inference when inputs have mixed formats.

---

## Feature 2 — Record Button Re-enable Fix

### Root cause

`set_button_processing()` in `plugin-main.cpp` disables the button. When `AutoEditDock::on_poll_timer()` detects completion (progress == 100), error, or stale timeout, it calls `stop_progress()` — but does not signal back to `plugin-main.cpp`. The button stays disabled until OBS restarts.

### Fix

Add a signal to `AutoEditDock`:
```cpp
signals:
    void processing_finished();
```

Emit `processing_finished()` in `on_poll_timer()` on all terminal conditions:
- Progress reaches 100
- `error:` prefix detected in progress file
- Stale timeout (>30s without update)

In `plugin-main.cpp`, connect the signal:
```cpp
connect(g_dock, &AutoEditDock::processing_finished,
        []() { set_button_idle(); });
```

No other changes needed. The button state machine (`idle → recording → processing → idle`) is now fully closed.

---

## Feature 3 — YouTube Upload Integration

### New source files

| File | Responsibility |
|------|---------------|
| `src/youtube-auth.h/cpp` | OAuth 2.0 PKCE flow, token storage, access token refresh |
| `src/youtube-uploader.h/cpp` | Resumable upload via YouTube Data API v3, progress reporting |

### New dependency: qtkeychain

Provides cross-platform secure token storage:
- Windows: Windows Credential Manager (DPAPI)
- macOS: Keychain
- Linux: libsecret / kwallet, plaintext fallback

CMakeLists.txt addition:
```cmake
find_package(Qt6Keychain REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6Keychain)
```

### Credentials (CMakeLists.txt)

OAuth client credentials are injected at compile time. They are **not** in source control:

```cmake
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    RIZZYTOS_CLIENT_ID="${RIZZYTOS_CLIENT_ID}"
    RIZZYTOS_CLIENT_SECRET="${RIZZYTOS_CLIENT_SECRET}"
)
```

The `client_secret` for a Desktop App OAuth client is not a real secret (it is extractable from any compiled binary). Security comes from PKCE, random `state`, loopback redirect, explicit user consent, and minimum scope. It is included solely because Google's token endpoint requires it for the exchange step.

**Local build:**
```bash
cmake -B build \
  -DRIZZYTOS_CLIENT_ID="xxxx.apps.googleusercontent.com" \
  -DRIZZYTOS_CLIENT_SECRET="xxxx"
```

**GitHub Actions:** add `RIZZYTOS_CLIENT_ID` and `RIZZYTOS_CLIENT_SECRET` as repository secrets, then pass them as `-D` flags in the CI cmake step. No credentials in `.yml` files.

### YouTubeAuth

Manages the full OAuth 2.0 PKCE authorization code flow for a Desktop App client.

**Authorization flow:**

1. Generate `code_verifier`: 32 random bytes, base64url-encoded (no padding).
2. Generate `code_challenge`: SHA-256 of `code_verifier`, base64url-encoded.
3. Generate `state`: 16 random bytes, hex-encoded (anti-CSRF).
4. Start `QTcpServer` on `127.0.0.1` binding to an OS-assigned available port (no fixed port, avoids conflicts).
5. Open system browser with authorization URL:
   ```
   https://accounts.google.com/o/oauth2/v2/auth
     ?client_id=CLIENT_ID
     &redirect_uri=http://127.0.0.1:{port}
     &response_type=code
     &scope=https://www.googleapis.com/auth/youtube.upload
     &code_challenge={challenge}
     &code_challenge_method=S256
     &access_type=offline
     &prompt=consent
     &state={state}
   ```
   (`prompt=consent` is required to receive a `refresh_token` on every auth, not just the first.)
6. Accept one TCP connection, parse `code` and `state` from the GET request.
7. Validate that received `state` matches the generated one. If mismatch: close server, emit `auth_failed("CSRF state mismatch")`.
8. Send minimal HTTP 200 response with HTML: "Autorización exitosa, puedes cerrar esta ventana."
9. Close the `QTcpServer`.
10. POST to `https://oauth2.googleapis.com/token` with:
    - `code`, `client_id`, `client_secret`, `redirect_uri`, `code_verifier`
    - `grant_type=authorization_code`
11. Parse response: extract `access_token`, `refresh_token`, `expires_in`.
12. Store `refresh_token` via qtkeychain (service: `"rizzytos-auto-edit"`, key: `"youtube_refresh_token"`).
13. Store `access_token` and expiry in memory only (never persisted).
14. Emit `authenticated()`.

**Token refresh (`ensure_valid_token`):**

Called before every upload. If the `access_token` is expired (or within 60s of expiry):
- POST to `https://oauth2.googleapis.com/token` with `grant_type=refresh_token`, `refresh_token`, `client_id`, `client_secret`.
- On success: update in-memory `access_token` and expiry.
- On failure (HTTP 400/401 with `invalid_grant`): delete token from keychain, emit `auth_revoked()`.

**Signals:**
```cpp
void authenticated();
void auth_failed(QString error);
void auth_revoked();
```

**Startup check (`load_stored_token`):**

On plugin load, attempt to read `youtube_refresh_token` from keychain. If present, set internal state to authenticated without running the full flow. The token validity is verified lazily on first upload attempt.

### YouTubeUploader

Handles YouTube Data API v3 resumable upload. Does not hold a reference to `YouTubeAuth` — it receives the access token as a string parameter and emits a signal when the token needs refreshing. `plugin-main.cpp` mediates the refresh and passes the new token back. This keeps auth and upload fully decoupled.

**Upload states:**
```cpp
enum class UploadState {
    Preparing,
    Uploading,
    ProcessingResponse,
    Completed,
    Failed
};
```

**Resumable upload flow:**

1. **Initiate session** — POST to:
   ```
   https://www.googleapis.com/upload/youtube/v3/videos
     ?uploadType=resumable
     &part=snippet,status
   ```
   Headers: `Authorization: Bearer {access_token}`, `X-Upload-Content-Type: video/*`, `X-Upload-Content-Length: {file_size}`.
   Body: metadata JSON (see below).
   Response: `Location` header = upload session URI.

2. **Upload chunks** — PUT to the session URI with `Content-Range: bytes {start}-{end}/{total}`. Chunk size: **8 MB** (multiple of 256 KB as required by Google). Each chunk triggers `progress_updated(percent)` where `percent = bytes_sent / total * 100`.

3. **On network error or 5xx** — query resume offset: PUT to session URI with `Content-Range: bytes */{total}`, no body. Response gives the last confirmed byte. Resume from that offset. Maximum 3 retry attempts before emitting `failed`.

4. **On 401** — emit `token_expired()` signal and pause upload. `plugin-main.cpp` calls `g_youtube_auth->ensure_valid_token()`, receives the new token, and calls `uploader->set_access_token(new_token)` to resume. If refresh fails (auth_revoked), upload is aborted via `failed()`.

5. **On 200/201 final response** — parse `id` field from response JSON, emit `completed("https://www.youtube.com/watch?v=" + id)`.

**Metadata sent:**
```json
{
  "snippet": {
    "title": "<panel title field>",
    "description": "<panel description field>"
  },
  "status": {
    "privacyStatus": "private" | "public",
    "selfDeclaredMadeForKids": false,
    "containsSyntheticMedia": false
  },
  "notifySubscribers": true
}
```

Tags and categoryId are not sent.

**Signals:**
```cpp
void state_changed(UploadState state);
void progress_updated(int percent);     // 0–100 upload byte progress
void completed(QString video_url);
void failed(QString error);
void token_expired();                   // plugin-main refreshes and calls set_access_token()
```

The uploader runs asynchronously using `QNetworkAccessManager`. It does not block the UI thread.

### AutoEditDock — YouTube Section

A new section at the bottom of the dock, below the existing settings. It renders conditionally based on auth state.

**Unauthenticated state:**
- Label: "YouTube"
- Button: "Conectar con YouTube" — triggers `YouTubeAuth::start_auth_flow()`
- No other controls visible.

**Authenticated state:**
```
[ ] Subir video a YouTube
      ◉ Privado  ○ Público
      Título:      [_______________________]
      Descripción: [_______________________]
                   [_______________________]
```
- Checkbox "Subir video a YouTube" — enables/disables the sub-controls and the upload trigger.
- Radio buttons "Privado" / "Público" — map to `"private"` / `"public"`.
- Title `QLineEdit` — independent of the output filename.
- Description `QTextEdit` — pre-filled default: `Mira mis streams en https://www.twitch.tv/nansulli 💖`.

**During and after upload (shown regardless of checkbox state once upload starts):**
```
Estado: Subiendo...
[████████████░░░░░░░░] 62%
URL: https://www.youtube.com/watch?v=xxxxx   ← clickable QLabel
```
- Upload progress bar is separate from the worker progress bar.
- Status label cycles through: "Preparando subida", "Subiendo", "Procesando respuesta", "Subida completada", "Error en subida: {msg}".
- URL label is hidden until upload completes. Uses `setOpenExternalLinks(true)`.

**YouTube settings persistence:**

Add a `YouTubeSettings` struct:
```cpp
struct YouTubeSettings {
    bool upload_enabled = false;
    std::string privacy = "private";
    std::string title;
    std::string description = "Mira mis streams en https://www.twitch.tv/nansulli 💖";
};
```

Persisted in `settings.json` under a `"youtube"` key, alongside existing fields.

### Integration in plugin-main.cpp

When `processing_finished()` is emitted by the dock:

1. Call `set_button_idle()` (the button fix from Feature 2).
2. Get `output_path` from `g_launcher`.
3. If `output_path` is empty or file does not exist on disk: skip upload, optionally log a warning.
4. If `g_dock->youtube_upload_enabled()` is false: done.
5. Call `g_youtube_auth->ensure_valid_token()`.
   - On `auth_revoked()`: dock switches back to unauthenticated state, stop.
6. Build `UploadMetadata` from dock's current YouTube settings.
7. Create `YouTubeUploader`, pass access token and metadata.
8. Connect uploader signals to dock slots for progress/state/URL display.
9. Connect `uploader->token_expired()` to a lambda in plugin-main that calls `g_youtube_auth->ensure_valid_token()` and then `uploader->set_access_token(new_token)`.
10. Call `uploader->start(output_path)`.

`YouTubeAuth` is instantiated once at plugin load (as a global alongside `g_settings`, `g_launcher`, etc.) and reused across recordings.

### Error handling matrix

| Condition | Behavior |
|-----------|----------|
| `refresh_token` not in keychain | Show "Conectar con YouTube" button |
| `refresh_token` invalid/revoked | Clear keychain entry, show "Conectar con YouTube" |
| HTTP 401 during upload | Refresh token → retry chunk once → if fails, auth_revoked |
| HTTP 403 (quota exceeded) | Show: "Cuota de YouTube agotada. Intenta más tarde." |
| HTTP 403 (app not verified, public upload restricted) | Show: "YouTube no permite subidas públicas desde apps no verificadas. Selecciona 'Privado' o verifica la app en Google Cloud Console." |
| Network error during chunk | Attempt resume (up to 3 retries) → if all fail, emit failed |
| Output file missing or empty path | Log warning, skip upload entirely |
| CSRF state mismatch in callback | Abort auth flow, show error in dock |

---

## CMake / Build Notes for Developer

After adding qtkeychain and the credential defines, the developer must:

1. Install `qtkeychain` on their system (or let CMake's `FetchContent` download it — to be decided during implementation).
2. Before running cmake locally, set:
   ```bash
   export RIZZYTOS_CLIENT_ID="xxxx.apps.googleusercontent.com"
   export RIZZYTOS_CLIENT_SECRET="xxxx"
   cmake -B build -DRIZZYTOS_CLIENT_ID="$RIZZYTOS_CLIENT_ID" -DRIZZYTOS_CLIENT_SECRET="$RIZZYTOS_CLIENT_SECRET"
   ```
3. In GitHub Actions, add both values as repository secrets and pass them to the cmake step.
4. Add to `.gitignore` (if a local secrets file is used for convenience): `cmake-secrets.sh` or equivalent.

---

## Files Changed Summary

| File | Change type |
|------|------------|
| `src/plugin-settings.h/cpp` | Add `output_resolution`, `output_format`, `YouTubeSettings` |
| `src/plugin-ui.h/cpp` | Add resolution/format combos, `processing_finished` signal, YouTube section |
| `src/plugin-launcher.h/cpp` | Derive output extension from format, pass `--width/height/format` |
| `src/plugin-main.cpp` | Connect `processing_finished`, instantiate `YouTubeAuth`, wire uploader |
| `src/youtube-auth.h/cpp` | New — OAuth 2.0 PKCE flow and token management |
| `src/youtube-uploader.h/cpp` | New — resumable upload and progress |
| `worker/args.h/cpp` | Add `out_width`, `out_height`, `out_format` |
| `worker/concat.h/cpp` | Use CLI-specified resolution and explicit container format |
| `data/locale/en-US.ini` | Add strings for new UI controls |
| `CMakeLists.txt` | Add qtkeychain, credential defines |
