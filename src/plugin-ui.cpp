#include "plugin-ui.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMainWindow>
#include <QSizePolicy>
#include <fstream>
#include <string>

// ── AutoEditDock ─────────────────────────────────────────────────────────────

AutoEditDock::AutoEditDock(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Action button — always present in the dock panel
    action_btn_ = new QPushButton(obs_module_text("RizzyTos.Button.Record"), this);
    layout->addWidget(action_btn_);
    connect(action_btn_, &QPushButton::clicked, this, &AutoEditDock::record_requested);

    // Separator between action button and settings
    {
        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        layout->addWidget(sep);
    }

    // Helper: build a label + line-edit + browse-button row
    auto make_browse_row = [&](const char *label_key,
                                QLineEdit *&edit,
                                const char *slot) {
        auto *row  = new QHBoxLayout;
        auto *lbl  = new QLabel(obs_module_text(label_key), this);
        lbl->setFixedWidth(100);
        edit = new QLineEdit(this);
        auto *btn  = new QPushButton(obs_module_text("RizzyTos.Settings.Browse"), this);
        btn->setFixedWidth(32);
        row->addWidget(lbl);
        row->addWidget(edit);
        row->addWidget(btn);
        layout->addLayout(row);
        connect(btn, SIGNAL(clicked()), this, slot);
    };

    make_browse_row("RizzyTos.Settings.OutputDir",  output_dir_edit_,  SLOT(browse_output_dir()));
    make_browse_row("RizzyTos.Settings.Intro",       intro_edit_,       SLOT(browse_intro()));
    make_browse_row("RizzyTos.Settings.Outro",       outro_edit_,       SLOT(browse_outro()));

    // Output name template (no browse button)
    {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(obs_module_text("RizzyTos.Settings.OutputName"), this);
        lbl->setFixedWidth(100);
        output_name_edit_ = new QLineEdit(this);
        row->addWidget(lbl);
        row->addWidget(output_name_edit_);
        layout->addLayout(row);
    }

    // Separator
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    // Status label
    status_label_ = new QLabel(obs_module_text("RizzyTos.Status.Idle"), this);
    layout->addWidget(status_label_);

    // Progress bar (hidden until a job is running)
    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 100);
    progress_bar_->hide();
    layout->addWidget(progress_bar_);

    layout->addStretch();

    // Emit settings_changed when any field loses focus
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
    progress_file_        = progress_file_path;
    output_path_          = output_path;
    last_progress_value_  = -1;
    last_progress_change_ = QDateTime::currentDateTime();

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

void AutoEditDock::set_action_button_state(bool recording, bool enabled)
{
    if (!action_btn_) return;
    action_btn_->setText(recording
        ? obs_module_text("RizzyTos.Button.Stop")
        : obs_module_text("RizzyTos.Button.Record"));
    action_btn_->setEnabled(enabled);
}

void AutoEditDock::on_poll_timer()
{
    std::ifstream f(progress_file_);
    if (!f.is_open()) return;

    std::string content;
    std::getline(f, content);
    f.close();
    if (content.empty()) return;

    // Error from worker
    if (content.rfind("error: ", 0) == 0) {
        stop_progress();
        std::string msg = std::string(obs_module_text("RizzyTos.Status.Error"))
                        + " " + content.substr(7);
        status_label_->setText(QString::fromStdString(msg));
        return;
    }

    int pct = 0;
    try { pct = std::stoi(content); } catch (...) { return; }

    if (pct != last_progress_value_) {
        last_progress_value_  = pct;
        last_progress_change_ = QDateTime::currentDateTime();
        progress_bar_->setValue(pct);
    }

    // Stale detection: no progress update for 30 s
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
        this,
        obs_module_text("RizzyTos.Settings.OutputDir"),
        output_dir_edit_->text());
    if (!dir.isEmpty()) {
        output_dir_edit_->setText(dir);
        emit settings_changed(get_settings());
    }
}

void AutoEditDock::browse_intro()
{
    QString f = QFileDialog::getOpenFileName(
        this,
        obs_module_text("RizzyTos.Settings.Intro"),
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
        this,
        obs_module_text("RizzyTos.Settings.Outro"),
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

    // Exact match first, then fuzzy fallback (contains "record", case-insensitive)
    QPushButton *record_btn =
        main_window->findChild<QPushButton *>("recordButton");
    if (!record_btn) {
        auto all_buttons = main_window->findChildren<QPushButton *>();
        for (auto *b : all_buttons) {
            if (b->objectName().contains("record", Qt::CaseInsensitive)) {
                record_btn = b;
                break;
            }
        }
    }
    if (!record_btn) {
        obs_log(LOG_WARNING,
                "Could not find record button in OBS main window — "
                "injection skipped. Use the button in the RizzyTos dock instead.");
        return nullptr;
    }

    QWidget *parent = record_btn->parentWidget();
    if (!parent) return nullptr;

    QLayout *layout = parent->layout();
    if (!layout) return nullptr;

    // Find index of the native record button in its parent layout
    int idx = -1;
    for (int i = 0; i < layout->count(); i++) {
        if (layout->itemAt(i)->widget() == record_btn) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return nullptr;

    auto *btn = new QPushButton(
        obs_module_text("RizzyTos.Button.Record"), parent);
    btn->setObjectName("rizzytos_record_button");
    btn->setSizePolicy(record_btn->sizePolicy());
    btn->setMinimumHeight(record_btn->minimumHeight());

    auto *vbox = qobject_cast<QVBoxLayout *>(layout);
    if (vbox)
        vbox->insertWidget(idx + 1, btn);
    else
        layout->addWidget(btn);

    return btn;
}
