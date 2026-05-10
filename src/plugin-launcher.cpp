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
    QString path = data_dir.filePath("rizzytos-worker.exe");
#else
    QString path = data_dir.filePath("rizzytos-worker");
#endif
    return QFileInfo(path).absoluteFilePath().toStdString();
}

std::string PluginLauncher::find_ffmpeg_binary() const
{
    const char *data_path = obs_get_module_data_path(obs_current_module());
    if (!data_path) return "ffmpeg";

    QDir data_dir(QString::fromUtf8(data_path));
#ifdef _WIN32
    QString path = data_dir.filePath("ffmpeg.exe");
#else
    QString path = data_dir.filePath("ffmpeg");
#endif
    QString abs = QFileInfo(path).absoluteFilePath();
    if (!QFileInfo::exists(abs))
        obs_log(LOG_WARNING, "bundled ffmpeg not found at: %s",
                abs.toUtf8().constData());
    return abs.toStdString();
}

bool PluginLauncher::launch(const std::string    &input_path,
                             const PluginSettings &settings,
                             const OBSEncoderInfo &encoder_info)
{
    // Generate unique progress file path using a short UUID
    QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
    progress_path_ = (QDir::tempPath() + "/rizzytos_" + uuid + ".txt").toStdString();

    // Build output path: dir/expanded_name.ext (extension from selected format, not recording)
    std::string expanded_name = recorder_expand_template(settings.output_name_template);
    output_path_ = settings.output_dir
                 + "/" + expanded_name
                 + "." + settings.output_format;

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

    // Resolve resolution string to pixel dimensions
    int out_w = 1920, out_h = 1080;
    if      (settings.output_resolution == "720p")  { out_w = 1280; out_h =  720; }
    else if (settings.output_resolution == "1080p") { out_w = 1920; out_h = 1080; }
    else if (settings.output_resolution == "2k")    { out_w = 2560; out_h = 1440; }
    else if (settings.output_resolution == "4k")    { out_w = 3840; out_h = 2160; }

    args << "--width"  << QString::number(out_w)
         << "--height" << QString::number(out_h)
         << "--format" << QString::fromStdString(settings.output_format);

    if (!settings.intro_path.empty())
        args << "--intro" << QString::fromStdString(settings.intro_path);
    if (!settings.outro_path.empty())
        args << "--outro" << QString::fromStdString(settings.outro_path);

    obs_log(LOG_INFO, "output path: %s", output_path_.c_str());
    obs_log(LOG_INFO, "progress file: %s", progress_path_.c_str());
    obs_log(LOG_INFO, "worker args (%d):", args.size());
    for (int i = 0; i < args.size(); ++i)
        obs_log(LOG_INFO, "  [%d] %s", i, args[i].toUtf8().constData());

    qint64 pid = 0;
    bool ok = QProcess::startDetached(QString::fromStdString(worker), args,
                                      QString(), &pid);
    if (ok)
        obs_log(LOG_INFO, "rizzytos-worker launched (pid=%lld), debug log: %s.log",
                (long long)pid, progress_path_.c_str());
    else
        obs_log(LOG_WARNING, "Failed to start rizzytos-worker");

    return ok;
}
