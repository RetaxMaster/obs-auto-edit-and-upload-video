#include "youtube-uploader.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

static constexpr const char *UPLOAD_ENDPOINT =
    "https://www.googleapis.com/upload/youtube/v3/videos"
    "?uploadType=resumable&part=snippet,status";

YouTubeUploader::YouTubeUploader(QObject *parent)
    : QObject(parent)
    , nam_(new QNetworkAccessManager(this))
{
}

void YouTubeUploader::set_access_token(const QString &token)
{
    access_token_ = token;
}

void YouTubeUploader::start(const QString &video_path,
                             const YouTubeSettings &settings)
{
    video_path_  = video_path;
    yt_settings_ = settings;
    upload_url_.clear();
    bytes_sent_ = 0;

    QFileInfo fi(video_path_);
    if (!fi.exists() || !fi.isFile()) {
        emit failed(obs_module_text("RizzyTos.YouTube.ErrorNoFile"));
        return;
    }
    file_size_ = fi.size();

    emit status_changed(obs_module_text("RizzyTos.YouTube.StatusPreparing"));
    initiate_session();
}

void YouTubeUploader::retry()
{
    if (upload_url_.isEmpty())
        initiate_session();
    else
        upload_next_chunk();
}

QString YouTubeUploader::build_metadata_json() const
{
    QJsonObject snippet;
    snippet["title"]       = QString::fromStdString(yt_settings_.title);
    snippet["description"] = QString::fromStdString(yt_settings_.description);

    QJsonObject status;
    status["privacyStatus"]          = QString::fromStdString(yt_settings_.privacy);
    status["selfDeclaredMadeForKids"] = false;
    status["containsSyntheticMedia"] = false;
    status["notifySubscribers"]      = true;

    QJsonObject root;
    root["snippet"] = snippet;
    root["status"]  = status;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void YouTubeUploader::initiate_session()
{
    QNetworkRequest req(QUrl(UPLOAD_ENDPOINT));
    req.setRawHeader("Authorization",
                     ("Bearer " + access_token_).toUtf8());
    req.setRawHeader("Content-Type",   "application/json; charset=UTF-8");
    req.setRawHeader("X-Upload-Content-Type", "video/*");
    req.setRawHeader("X-Upload-Content-Length",
                     QByteArray::number(file_size_));

    QByteArray body = build_metadata_json().toUtf8();
    QNetworkReply *reply = nam_->post(req, body);
    connect(reply, &QNetworkReply::finished,
            this, &YouTubeUploader::on_initiate_finished);
}

void YouTubeUploader::on_initiate_finished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    reply->deleteLater();

    int status_code = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status_code == 401) {
        emit token_expired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError || status_code != 200) {
        QByteArray body = reply->readAll();
        obs_log(LOG_WARNING, "YouTube initiate failed (%d): %s",
                status_code, body.constData());

        if (body.contains("\"quotaExceeded\""))
            emit failed(obs_module_text("RizzyTos.YouTube.ErrorQuota"));
        else if (body.contains("\"forbidden\"") || status_code == 403)
            emit failed(obs_module_text("RizzyTos.YouTube.ErrorPermissions"));
        else
            emit failed(QString(obs_module_text("RizzyTos.YouTube.StatusError"))
                        + " HTTP " + QString::number(status_code));
        return;
    }

    upload_url_ = QString::fromUtf8(reply->rawHeader("Location"));
    if (upload_url_.isEmpty()) {
        emit failed("YouTube no devolvió una URL de subida.");
        return;
    }

    emit status_changed(obs_module_text("RizzyTos.YouTube.StatusUploading"));
    upload_next_chunk();
}

void YouTubeUploader::upload_next_chunk()
{
    QFile *file = new QFile(video_path_, this);
    if (!file->open(QIODevice::ReadOnly)) {
        emit failed("No se pudo abrir el archivo de video.");
        file->deleteLater();
        return;
    }

    file->seek(bytes_sent_);
    qint64 remaining  = file_size_ - bytes_sent_;
    qint64 chunk_size = qMin(CHUNK_SIZE, remaining);
    QByteArray chunk  = file->read(chunk_size);
    file->close();
    file->deleteLater();

    qint64 range_end = bytes_sent_ + chunk.size() - 1;

    QNetworkRequest req(QUrl(upload_url_));
    req.setRawHeader("Authorization",
                     ("Bearer " + access_token_).toUtf8());
    req.setRawHeader("Content-Type", "video/*");
    req.setRawHeader("Content-Range",
                     QString("bytes %1-%2/%3")
                         .arg(bytes_sent_).arg(range_end).arg(file_size_)
                         .toUtf8());

    QNetworkReply *reply = nam_->put(req, chunk);
    connect(reply, &QNetworkReply::finished,
            this, &YouTubeUploader::on_chunk_finished);
}

void YouTubeUploader::on_chunk_finished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    reply->deleteLater();

    int status_code = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status_code == 401) {
        emit token_expired();
        return;
    }

    if (reply->error() != QNetworkReply::NoError
        && status_code != 308  // Resume Incomplete
        && status_code != 200
        && status_code != 201) {
        QByteArray body = reply->readAll();
        obs_log(LOG_WARNING, "YouTube chunk upload failed (%d): %s",
                status_code, body.constData());

        if (body.contains("\"quotaExceeded\""))
            emit failed(obs_module_text("RizzyTos.YouTube.ErrorQuota"));
        else if (body.contains("\"forbidden\"") || status_code == 403)
            emit failed(obs_module_text("RizzyTos.YouTube.ErrorPermissions"));
        else
            emit failed(QString(obs_module_text("RizzyTos.YouTube.StatusError"))
                        + " HTTP " + QString::number(status_code));
        return;
    }

    // 308 = more chunks needed
    if (status_code == 308) {
        // Parse Range header to find how many bytes were accepted
        QByteArray range_hdr = reply->rawHeader("Range");
        if (!range_hdr.isEmpty()) {
            // Format: bytes=0-N
            qint64 accepted = range_hdr.split('-').last().toLongLong() + 1;
            bytes_sent_ = accepted;
        } else {
            bytes_sent_ = 0; // server wants restart from 0
        }

        int pct = file_size_ > 0
                  ? static_cast<int>(bytes_sent_ * 100 / file_size_)
                  : 0;
        emit progress(pct);
        upload_next_chunk();
        return;
    }

    // 200 or 201 — upload finished
    emit status_changed(obs_module_text("RizzyTos.YouTube.StatusProcessing"));

    QJsonDocument doc  = QJsonDocument::fromJson(reply->readAll());
    QString video_id   = doc.object()["id"].toString();
    QString url        = "https://www.youtube.com/watch?v=" + video_id;

    emit progress(100);
    emit status_changed(QString(obs_module_text("RizzyTos.YouTube.StatusDone"))
                        + " " + url);
    emit completed(url);
}
