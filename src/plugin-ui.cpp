#include "plugin-ui.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QMainWindow>
#include <QSizePolicy>
#include <QGroupBox>
#include <QButtonGroup>
#include <fstream>
#include <string>

// UTF-8 encoded × (U+00D7) for combo item labels
static constexpr const char *TIMES_SIGN = "\xC3\x97";

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

    // Resolution selector
    {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(obs_module_text("RizzyTos.Settings.OutputResolution"), this);
        lbl->setFixedWidth(100);
        resolution_combo_ = new QComboBox(this);
        resolution_combo_->addItem(
            QString("720p (1280%1720)").arg(TIMES_SIGN),  QString("720p"));
        resolution_combo_->addItem(
            QString("1080p (1920%11080)").arg(TIMES_SIGN), QString("1080p"));
        resolution_combo_->addItem(
            QString("2K (2560%11440)").arg(TIMES_SIGN),   QString("2k"));
        resolution_combo_->addItem(
            QString("4K (3840%12160)").arg(TIMES_SIGN),   QString("4k"));
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

    // ── YouTube section ────────────────────────────────────────────────────

    // Separator
    auto *yt_sep = new QFrame(this);
    yt_sep->setFrameShape(QFrame::HLine);
    yt_sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(yt_sep);

    // Connect button (shown when NOT authenticated)
    yt_connect_btn_ = new QPushButton(
        obs_module_text("RizzyTos.YouTube.ConnectButton"), this);
    layout->addWidget(yt_connect_btn_);
    connect(yt_connect_btn_, &QPushButton::clicked,
            this, &AutoEditDock::youtube_connect_requested);

    // Controls widget (shown when authenticated)
    yt_controls_ = new QWidget(this);
    yt_controls_->hide();
    auto *yt_vbox = new QVBoxLayout(yt_controls_);
    yt_vbox->setContentsMargins(0, 0, 0, 0);
    yt_vbox->setSpacing(4);

    yt_upload_check_ = new QCheckBox(
        obs_module_text("RizzyTos.YouTube.UploadCheckbox"), yt_controls_);
    yt_vbox->addWidget(yt_upload_check_);

    // Privacy radio buttons
    {
        auto *row = new QHBoxLayout;
        yt_private_radio_ = new QRadioButton(
            obs_module_text("RizzyTos.YouTube.PrivacyPrivate"), yt_controls_);
        yt_public_radio_  = new QRadioButton(
            obs_module_text("RizzyTos.YouTube.PrivacyPublic"),  yt_controls_);
        yt_private_radio_->setChecked(true);
        row->addWidget(yt_private_radio_);
        row->addWidget(yt_public_radio_);
        row->addStretch();
        yt_vbox->addLayout(row);
    }

    // Title
    {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(
            obs_module_text("RizzyTos.YouTube.TitleLabel"), yt_controls_);
        lbl->setFixedWidth(100);
        yt_title_edit_ = new QLineEdit(yt_controls_);
        row->addWidget(lbl);
        row->addWidget(yt_title_edit_);
        yt_vbox->addLayout(row);
    }

    // Description
    {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(
            obs_module_text("RizzyTos.YouTube.DescLabel"), yt_controls_);
        lbl->setFixedWidth(100);
        lbl->setAlignment(Qt::AlignTop);
        yt_desc_edit_ = new QTextEdit(yt_controls_);
        yt_desc_edit_->setFixedHeight(60);
        yt_desc_edit_->setPlainText(
            QString::fromUtf8("Mira mis streams en https://www.twitch.tv/nansulli \xF0\x9F\x92\x96"));
        row->addWidget(lbl);
        row->addWidget(yt_desc_edit_);
        yt_vbox->addLayout(row);
    }

    // Upload progress bar (hidden until upload is running)
    yt_progress_bar_ = new QProgressBar(yt_controls_);
    yt_progress_bar_->setRange(0, 100);
    yt_progress_bar_->hide();
    yt_vbox->addWidget(yt_progress_bar_);

    // Upload status label
    yt_status_label_ = new QLabel(yt_controls_);
    yt_status_label_->setWordWrap(true);
    yt_status_label_->setOpenExternalLinks(true);
    yt_vbox->addWidget(yt_status_label_);

    layout->addWidget(yt_controls_);

    // Emit youtube_settings_changed when any YT field changes
    auto emit_yt = [this]() { emit youtube_settings_changed(get_youtube_settings()); };
    connect(yt_upload_check_,  &QCheckBox::toggled,
            this, &AutoEditDock::on_youtube_upload_toggled);
    connect(yt_private_radio_, &QRadioButton::toggled, this, emit_yt);
    connect(yt_public_radio_,  &QRadioButton::toggled, this, emit_yt);
    connect(yt_title_edit_,    &QLineEdit::editingFinished, this, emit_yt);
    connect(yt_desc_edit_,     &QTextEdit::textChanged, this, emit_yt);

    layout->addStretch();
}

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

void AutoEditDock::set_settings(const PluginSettings &s)
{
    output_name_edit_->setText(QString::fromStdString(s.output_name_template));

    auto apply_path = [](QLineEdit *edit, const std::string &path) -> bool {
        QString qpath = QString::fromStdString(path);
        if (!qpath.isEmpty() && !QFileInfo::exists(qpath)) {
            edit->setText("");
            edit->setPlaceholderText(obs_module_text("RizzyTos.Error.FileNotFound"));
            return true; // missing
        }
        edit->setText(qpath);
        edit->setPlaceholderText("");
        return false;
    };

    bool any_missing = false;
    any_missing |= apply_path(output_dir_edit_, s.output_dir);
    any_missing |= apply_path(intro_edit_,      s.intro_path);
    any_missing |= apply_path(outro_edit_,      s.outro_path);

    for (int i = 0; i < resolution_combo_->count(); ++i) {
        if (resolution_combo_->itemData(i).toString().toStdString() == s.output_resolution) {
            resolution_combo_->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < format_combo_->count(); ++i) {
        if (format_combo_->itemData(i).toString().toStdString() == s.output_format) {
            format_combo_->setCurrentIndex(i);
            break;
        }
    }

    if (any_missing)
        emit settings_changed(get_settings());
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

        // Read the worker's debug log (written next to the progress file)
        std::ifstream dlog(progress_file_ + ".log");
        if (dlog.is_open()) {
            std::string line;
            while (std::getline(dlog, line))
                obs_log(LOG_INFO, "[rizzytos-worker] %s", line.c_str());
        }
        emit processing_finished();
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
        emit processing_finished();
        return;
    }

    if (pct >= 100) {
        stop_progress();
        QString done = QString(obs_module_text("RizzyTos.Status.Done"))
                     + " " + QString::fromStdString(output_path_);
        status_label_->setText(done);
        emit processing_finished();
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

// ── YouTube methods ───────────────────────────────────────────────────────────

YouTubeSettings AutoEditDock::get_youtube_settings() const
{
    YouTubeSettings ys;
    ys.upload_enabled = yt_upload_check_ && yt_upload_check_->isChecked();
    ys.privacy        = (yt_public_radio_ && yt_public_radio_->isChecked())
                        ? "public" : "private";
    ys.title          = yt_title_edit_
                        ? yt_title_edit_->text().toStdString() : "";
    ys.description    = yt_desc_edit_
                        ? yt_desc_edit_->toPlainText().toStdString() : "";
    return ys;
}

void AutoEditDock::set_youtube_settings(const YouTubeSettings &ys)
{
    if (yt_upload_check_) yt_upload_check_->setChecked(ys.upload_enabled);
    if (yt_private_radio_ && yt_public_radio_) {
        if (ys.privacy == "public")
            yt_public_radio_->setChecked(true);
        else
            yt_private_radio_->setChecked(true);
    }
    if (yt_title_edit_)
        yt_title_edit_->setText(QString::fromStdString(ys.title));
    if (yt_desc_edit_ && !ys.description.empty())
        yt_desc_edit_->setPlainText(QString::fromStdString(ys.description));
}

void AutoEditDock::set_youtube_authenticated(bool authenticated)
{
    if (yt_connect_btn_) yt_connect_btn_->setVisible(!authenticated);
    if (yt_controls_)    yt_controls_->setVisible(authenticated);
}

void AutoEditDock::set_youtube_upload_progress(int percent)
{
    if (!yt_progress_bar_) return;
    yt_progress_bar_->setValue(percent);
    yt_progress_bar_->show();
}

void AutoEditDock::set_youtube_upload_status(const QString &text)
{
    if (yt_status_label_) yt_status_label_->setText(text);
}

void AutoEditDock::on_youtube_upload_toggled(bool /*checked*/)
{
    emit youtube_settings_changed(get_youtube_settings());
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
