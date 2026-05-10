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
#include <QCheckBox>
#include <QRadioButton>
#include <QTextEdit>
#include "plugin-settings.h"

class AutoEditDock : public QWidget {
    Q_OBJECT
public:
    explicit AutoEditDock(QWidget *parent = nullptr);

    PluginSettings   get_settings() const;
    void             set_settings(const PluginSettings &s);

    YouTubeSettings  get_youtube_settings() const;
    void             set_youtube_settings(const YouTubeSettings &ys);

    void start_progress(const std::string &progress_file_path,
                        const std::string &output_path);
    void stop_progress();

    void set_action_button_state(bool recording, bool enabled);

    // Called by plugin-main when auth state changes
    void set_youtube_authenticated(bool authenticated);

    // Called by plugin-main to drive upload progress/status/result
    void set_youtube_upload_progress(int percent);
    void set_youtube_upload_status(const QString &text);
    void set_youtube_url(const QString &url);

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
    void on_youtube_upload_toggled(bool checked);

private:
    // ── Recording settings widgets ───────────────────────────────────────────
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

    // ── Recording extra widgets ──────────────────────────────────────────────
    QCheckBox    *delete_recording_check_ = nullptr;

    // ── YouTube widgets ──────────────────────────────────────────────────────
    QPushButton  *yt_connect_btn_    = nullptr; // shown when not authenticated
    QWidget      *yt_controls_       = nullptr; // shown when authenticated
    QCheckBox    *yt_upload_check_   = nullptr;
    QRadioButton *yt_private_radio_  = nullptr;
    QRadioButton *yt_public_radio_   = nullptr;
    QLineEdit    *yt_title_edit_     = nullptr;
    QTextEdit    *yt_desc_edit_      = nullptr;
    QProgressBar *yt_progress_bar_   = nullptr;
    QLabel       *yt_status_label_   = nullptr;
    QLineEdit    *yt_url_edit_       = nullptr; // readonly URL after upload
    QPushButton  *yt_copy_btn_       = nullptr; // copy URL to clipboard
};

QPushButton *inject_record_button(QWidget *main_window);
