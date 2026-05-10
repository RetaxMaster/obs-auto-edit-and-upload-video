#include "plugin-launcher.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QProcess>
#include <QDir>
#include <QUuid>
#include <QString>
#include <QStringList>
#include <QFileInfo>

std::string PluginLauncher::find_worker_binary() const
{
    const char *data_path = obs_get_module_data_path(obs_current_module());
    if (!data_path) return "";

    QDir data_dir(QString::fromUtf8(data_path));
#ifdef _WIN32
    return data_dir.filePath("rizzytos-worker.exe").toStdString();
#else
    return data_dir.filePath("rizzytos-worker").toStdString();
#endif
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
    // Generate unique progress file path using a short UUID
    QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
    progress_path_ = (QDir::tempPath() + "/rizzytos_" + uuid + ".txt").toStdString();

    // Build output path: dir/expanded_name.ext
    std::string expanded_name = recorder_expand_template(settings.output_name_template);
    output_path_ = settings.output_dir
                 + "/" + expanded_name
                 + "." + encoder_info.ext;

    std::string worker = find_worker_binary();
    std::string ffmpeg = find_ffmpeg_binary();

    obs_log(LOG_INFO, "worker path: %s", worker.c_str());
    obs_log(LOG_INFO, "ffmpeg path: %s", ffmpeg.c_str());

    if (worker.empty()) {
        obs_log(LOG_WARNING, "rizzytos-worker binary not found");
        return false;
    }

    if (!QFileInfo::exists(QString::fromStdString(worker))) {
        obs_log(LOG_WARNING, "rizzytos-worker not found at: %s", worker.c_str());
        return false;
    }

    QStringList args;
    args << "--input"    << QString::fromStdString(input_path)
         << "--output"   << QString::fromStdString(output_path_)
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
    if (!ok)
        obs_log(LOG_WARNING, "Failed to start rizzytos-worker");

    return ok;
}
