/*
RizzyTos Auto Edit
Copyright (C) 2026 RetaxMaster <carlos@retaxmaster.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/platform.h>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include "plugin-settings.h"
#include "plugin-recorder.h"
#include "plugin-launcher.h"
#include "plugin-ui.h"
#include "youtube-auth.h"
#include "youtube-uploader.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// ── Module-level state ────────────────────────────────────────────────────────

static PluginSettings  g_settings;
static YouTubeSettings g_yt_settings;
static PluginLauncher  g_launcher;
static bool            g_our_recording_active = false;
static char            g_config_path[2048]    = {};

static QPointer<AutoEditDock>    g_dock;
static QPointer<QPushButton>     g_button;
static QPointer<YouTubeAuth>     g_yt_auth;
static std::string               g_pending_upload_path;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void set_button_idle()
{
    if (g_button) {
        g_button->setText(obs_module_text("RizzyTos.Button.Record"));
        g_button->setEnabled(true);
    }
    if (g_dock) g_dock->set_action_button_state(false, true);
}

static void set_button_recording()
{
    if (g_button) {
        g_button->setText(obs_module_text("RizzyTos.Button.Stop"));
        g_button->setEnabled(true);
    }
    if (g_dock) g_dock->set_action_button_state(true, true);
}

static void set_button_processing()
{
    if (g_button) g_button->setEnabled(false);
    if (g_dock) g_dock->set_action_button_state(false, false);
}

static void start_youtube_upload(const std::string &video_path)
{
    if (!g_dock || !g_yt_auth) return;

    auto *uploader = new YouTubeUploader(g_dock);
    uploader->set_access_token(g_yt_auth->access_token());

    QObject::connect(uploader, &YouTubeUploader::progress,
                     g_dock, &AutoEditDock::set_youtube_upload_progress);
    QObject::connect(uploader, &YouTubeUploader::status_changed,
                     g_dock, &AutoEditDock::set_youtube_upload_status);
    QObject::connect(uploader, &YouTubeUploader::completed,
                     [](const QString &url) {
                         obs_log(LOG_INFO, "YouTube upload completed: %s",
                                 url.toUtf8().constData());
                     });
    QObject::connect(uploader, &YouTubeUploader::completed,
                     uploader, &QObject::deleteLater);
    QObject::connect(uploader, &YouTubeUploader::failed,
                     [](const QString &reason) {
                         obs_log(LOG_WARNING, "YouTube upload failed: %s",
                                 reason.toUtf8().constData());
                     });
    QObject::connect(uploader, &YouTubeUploader::failed,
                     uploader, &QObject::deleteLater);

    // Token expired mid-upload: refresh and retry
    QObject::connect(uploader, &YouTubeUploader::token_expired,
                     [uploader]() {
                         if (g_yt_auth) {
                             QObject::connect(g_yt_auth, &YouTubeAuth::token_refreshed,
                                              uploader, [uploader](const QString &token) {
                                                  uploader->set_access_token(token);
                                                  uploader->retry();
                                              }, Qt::SingleShotConnection);
                             g_yt_auth->refresh_access_token();
                         }
                     });

    uploader->start(QString::fromStdString(video_path), g_yt_settings);
}

static void on_processing_finished()
{
    set_button_idle();
    if (g_yt_settings.upload_enabled && !g_pending_upload_path.empty()) {
        if (!g_yt_auth || !g_yt_auth->is_authenticated()) {
            obs_log(LOG_WARNING,
                    "YouTube upload requested but not authenticated — skipping.");
            g_pending_upload_path.clear();
            return;
        }
        std::string path = g_pending_upload_path;
        g_pending_upload_path.clear();
        start_youtube_upload(path);
    }
}

static void on_button_clicked()
{
    if (g_our_recording_active) {
        recorder_stop_our_recording();
    } else {
        recorder_start_our_recording(g_our_recording_active);
        if (g_our_recording_active)
            set_button_recording();
    }
}

static void trigger_auto_edit()
{
    std::string rec_path = recorder_get_last_recording_path();
    if (rec_path.empty()) {
        obs_log(LOG_WARNING, "%s",
                obs_module_text("RizzyTos.Error.NoRecording"));
        set_button_idle();
        return;
    }

    OBSEncoderInfo enc_info = recorder_read_encoder_info(rec_path);

    bool ok = g_launcher.launch(rec_path, g_settings, enc_info);
    if (!ok) {
        obs_log(LOG_WARNING, "Failed to launch rizzytos-worker");
        set_button_idle();
        return;
    }

    g_pending_upload_path = g_launcher.output_path();
    set_button_processing();

    if (g_dock)
        g_dock->start_progress(g_launcher.progress_path(),
                                g_launcher.output_path());
}

static void on_dock_settings_changed(const PluginSettings &s)
{
    g_settings = s;
    settings_save(g_settings, g_config_path);
}

static void on_dock_youtube_settings_changed(const YouTubeSettings &ys)
{
    g_yt_settings = ys;
    youtube_settings_save(g_yt_settings, g_config_path);
}

// ── OBS event callback ────────────────────────────────────────────────────────

static void obs_event_cb(enum obs_frontend_event event, void * /*private_data*/)
{
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
        // Inject button into OBS controls dock
        QMainWindow *mw =
            static_cast<QMainWindow *>(obs_frontend_get_main_window());
        g_button = inject_record_button(mw);
        if (g_button)
            QObject::connect(g_button, &QPushButton::clicked, on_button_clicked);

        // Register settings dock
        auto *dock_widget = new AutoEditDock();
        g_dock = dock_widget;
        QObject::connect(g_dock, &AutoEditDock::settings_changed,
                         on_dock_settings_changed);
        QObject::connect(g_dock, &AutoEditDock::record_requested,
                         on_button_clicked);
        QObject::connect(g_dock, &AutoEditDock::processing_finished,
                         on_processing_finished);
        QObject::connect(g_dock, &AutoEditDock::youtube_settings_changed,
                         on_dock_youtube_settings_changed);
        g_dock->set_settings(g_settings);
        g_dock->set_youtube_settings(g_yt_settings);

        // YouTube auth
        g_yt_auth = new YouTubeAuth(dock_widget);
        bool authenticated = g_yt_auth->is_authenticated();
        g_dock->set_youtube_authenticated(authenticated);

        QObject::connect(g_dock, &AutoEditDock::youtube_connect_requested,
                         g_yt_auth, &YouTubeAuth::start_auth_flow);

        QObject::connect(g_yt_auth, &YouTubeAuth::authenticated,
                         [](const QString &) {
                             if (g_dock) g_dock->set_youtube_authenticated(true);
                         });
        QObject::connect(g_yt_auth, &YouTubeAuth::auth_failed,
                         [](const QString &reason) {
                             obs_log(LOG_WARNING, "YouTube auth failed: %s",
                                     reason.toUtf8().constData());
                             if (g_dock) g_dock->set_youtube_authenticated(false);
                         });

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

    char *conf_dir = obs_module_get_config_path(obs_current_module(), "");
    if (conf_dir) {
        os_mkdirs(conf_dir);
        snprintf(g_config_path, sizeof(g_config_path),
                 "%s/settings.json", conf_dir);
        bfree(conf_dir);
    }

    g_settings    = settings_load(g_config_path);
    g_yt_settings = youtube_settings_load(g_config_path);

    obs_frontend_add_event_callback(obs_event_cb, nullptr);
    return true;
}

void obs_module_unload(void)
{
    obs_log(LOG_INFO, "plugin unloaded");
    obs_frontend_remove_event_callback(obs_event_cb, nullptr);
}
