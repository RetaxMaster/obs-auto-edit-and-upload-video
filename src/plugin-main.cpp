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

static PluginSettings g_settings;
static PluginLauncher g_launcher;
static bool           g_our_recording_active = false;
static char           g_config_path[2048]    = {};

static QPointer<AutoEditDock> g_dock;
static QPointer<QPushButton>  g_button;

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
        g_dock->set_settings(g_settings);
        QObject::connect(g_dock, &AutoEditDock::settings_changed,
                         on_dock_settings_changed);
        QObject::connect(g_dock, &AutoEditDock::record_requested,
                         on_button_clicked);

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
        snprintf(g_config_path, sizeof(g_config_path),
                 "%s/settings.json", conf_dir);
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
