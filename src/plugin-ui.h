#pragma once
#include <QWidget>
#include <QLineEdit>
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

    // Begin polling progress_file_path every 500 ms.
    void start_progress(const std::string &progress_file_path,
                        const std::string &output_path);
    void stop_progress();

    // Sync the in-dock action button state (mirrors the injected controls button).
    void set_action_button_state(bool recording, bool enabled);

signals:
    void settings_changed(const PluginSettings &s);
    void record_requested();

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
    QProgressBar *progress_bar_     = nullptr;
    QLabel       *status_label_     = nullptr;
    QTimer       *poll_timer_       = nullptr;

    std::string  progress_file_;
    std::string  output_path_;
    QDateTime    last_progress_change_;
    int          last_progress_value_ = -1;
};

// Inject our button into OBS's controls dock directly after the native record
// button. Returns nullptr (and logs a warning) if the widget is not found.
QPushButton *inject_record_button(QWidget *main_window);
