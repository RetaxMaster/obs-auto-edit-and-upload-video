// Prevent windows.h (pulled in by curl) from defining min/max macros
#define NOMINMAX
#include "youtube-uploader.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <curl/curl.h>
#include <algorithm>
#include <string>

static constexpr const char *UPLOAD_ENDPOINT =
    "https://www.googleapis.com/upload/youtube/v3/videos"
    "?uploadType=resumable&part=snippet,status";

static size_t write_cb(char *ptr, size_t sz, size_t n, void *ud)
{
    static_cast<QByteArray *>(ud)->append(ptr, (qsizetype)(sz * n));
    return sz * n;
}

// Used to capture the Location header from the initiation response.
static size_t location_header_cb(char *buf, size_t sz, size_t n, void *ud)
{
    QString line = QString::fromUtf8(buf, (int)(sz * n)).trimmed();
    if (line.startsWith("location:", Qt::CaseInsensitive))
        *static_cast<QString *>(ud) = line.mid(9).trimmed();
    return sz * n;
}

// Used to capture the Range header from 308 responses.
static size_t range_header_cb(char *buf, size_t sz, size_t n, void *ud)
{
    QString line = QString::fromUtf8(buf, (int)(sz * n)).trimmed();
    if (line.startsWith("range:", Qt::CaseInsensitive))
        *static_cast<QString *>(ud) = line.mid(6).trimmed();
    return sz * n;
}

struct ReadData {
    const char *ptr;
    size_t      size;
    size_t      pos;
};

static size_t read_cb(char *buf, size_t sz, size_t n, void *ud)
{
    auto *rd = static_cast<ReadData *>(ud);
    size_t avail   = rd->size - rd->pos;
    size_t to_copy = std::min(avail, sz * n);
    memcpy(buf, rd->ptr + rd->pos, to_copy);
    rd->pos += to_copy;
    return to_copy;
}

YouTubeUploader::YouTubeUploader(QObject *parent)
    : QObject(parent)
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
    start_upload_thread();
}

void YouTubeUploader::retry()
{
    start_upload_thread();
}

void YouTubeUploader::start_upload_thread()
{
    // Snapshot all state the thread needs — never let the thread touch members directly.
    QString upload_url = upload_url_;
    qint64  bytes_sent = bytes_sent_;
    qint64  file_size  = file_size_;
    QString video_path = video_path_;
    QString access_tok = access_token_;
    QString metadata   = build_metadata_json();

    QPointer<YouTubeUploader> self(this);

    QThread *t = QThread::create([self, upload_url, bytes_sent, file_size,
                                   video_path, access_tok, metadata]() mutable {
        // ── Initiate resumable session if no URL yet ──────────────────────────
        if (upload_url.isEmpty()) {
            QByteArray meta_bytes = metadata.toUtf8();
            QByteArray response;
            QString    location;
            long       http_code = 0;

            CURL *curl = curl_easy_init();
            if (!curl) {
                QMetaObject::invokeMethod(self.data(), [self]() {
                    if (self) emit self->failed("No se pudo inicializar cURL.");
                }, Qt::QueuedConnection);
                return;
            }

            std::string auth_hdr = "Authorization: Bearer " + access_tok.toStdString();
            std::string len_hdr  = "X-Upload-Content-Length: " + std::to_string(file_size);
            struct curl_slist *hdrs = nullptr;
            hdrs = curl_slist_append(hdrs, "Content-Type: application/json; charset=UTF-8");
            hdrs = curl_slist_append(hdrs, "X-Upload-Content-Type: video/*");
            hdrs = curl_slist_append(hdrs, len_hdr.c_str());
            hdrs = curl_slist_append(hdrs, auth_hdr.c_str());

            curl_easy_setopt(curl, CURLOPT_URL,            UPLOAD_ENDPOINT);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     meta_bytes.constData());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  (long)meta_bytes.size());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdrs);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, location_header_cb);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA,     &location);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
            CURLcode res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_slist_free_all(hdrs);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) {
                QString err = QString(obs_module_text("RizzyTos.YouTube.StatusError"))
                              + " cURL: " + curl_easy_strerror(res);
                QMetaObject::invokeMethod(self.data(), [self, err]() {
                    if (self) emit self->failed(err);
                }, Qt::QueuedConnection);
                return;
            }
            if (http_code == 401) {
                QMetaObject::invokeMethod(self.data(), [self]() {
                    if (self) emit self->token_expired();
                }, Qt::QueuedConnection);
                return;
            }
            if (http_code != 200 || location.isEmpty()) {
                QString err = QString(obs_module_text("RizzyTos.YouTube.StatusError"))
                              + " HTTP " + QString::number(http_code);
                if (response.contains("\"quotaExceeded\""))
                    err = obs_module_text("RizzyTos.YouTube.ErrorQuota");
                else if (response.contains("\"forbidden\"") || http_code == 403)
                    err = obs_module_text("RizzyTos.YouTube.ErrorPermissions");
                QMetaObject::invokeMethod(self.data(), [self, err]() {
                    if (self) emit self->failed(err);
                }, Qt::QueuedConnection);
                return;
            }

            upload_url = location;
            QString saved_url = upload_url;
            QMetaObject::invokeMethod(self.data(), [self, saved_url]() {
                if (!self) return;
                self->upload_url_ = saved_url;
                emit self->status_changed(obs_module_text("RizzyTos.YouTube.StatusUploading"));
            }, Qt::QueuedConnection);
        }

        // ── Chunked upload loop ───────────────────────────────────────────────
        while (bytes_sent < file_size) {
            if (self.isNull()) return;

            QFile file(video_path);
            if (!file.open(QIODevice::ReadOnly)) {
                QMetaObject::invokeMethod(self.data(), [self]() {
                    if (self) emit self->failed("No se pudo abrir el archivo de video.");
                }, Qt::QueuedConnection);
                return;
            }
            file.seek(bytes_sent);
            qint64 chunk_sz = qMin((qint64)YouTubeUploader::CHUNK_SIZE, file_size - bytes_sent);
            QByteArray chunk = file.read(chunk_sz);
            file.close();

            qint64 range_end = bytes_sent + chunk.size() - 1;
            std::string auth_hdr = "Authorization: Bearer " + access_tok.toStdString();
            std::string cr_hdr   = "Content-Range: bytes " +
                                   std::to_string(bytes_sent) + "-" +
                                   std::to_string(range_end)  + "/" +
                                   std::to_string(file_size);

            ReadData rd{chunk.constData(), (size_t)chunk.size(), 0};
            QByteArray response;
            QString    range_hdr;
            long       http_code = 0;

            CURL *curl = curl_easy_init();
            if (!curl) {
                QMetaObject::invokeMethod(self.data(), [self]() {
                    if (self) emit self->failed("No se pudo inicializar cURL.");
                }, Qt::QueuedConnection);
                return;
            }

            struct curl_slist *hdrs = nullptr;
            hdrs = curl_slist_append(hdrs, auth_hdr.c_str());
            hdrs = curl_slist_append(hdrs, "Content-Type: video/*");
            hdrs = curl_slist_append(hdrs, cr_hdr.c_str());
            hdrs = curl_slist_append(hdrs, "Expect:");  // suppress 100-continue

            curl_easy_setopt(curl, CURLOPT_URL,              upload_url.toUtf8().constData());
            curl_easy_setopt(curl, CURLOPT_UPLOAD,           1L);
            curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)chunk.size());
            curl_easy_setopt(curl, CURLOPT_READFUNCTION,     read_cb);
            curl_easy_setopt(curl, CURLOPT_READDATA,         &rd);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER,       hdrs);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,   range_header_cb);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA,       &range_hdr);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,    write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA,        &response);
            CURLcode res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_slist_free_all(hdrs);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) {
                QString err = QString(obs_module_text("RizzyTos.YouTube.StatusError"))
                              + " cURL: " + curl_easy_strerror(res);
                QMetaObject::invokeMethod(self.data(), [self, err]() {
                    if (self) emit self->failed(err);
                }, Qt::QueuedConnection);
                return;
            }

            if (http_code == 401) {
                QMetaObject::invokeMethod(self.data(), [self]() {
                    if (self) emit self->token_expired();
                }, Qt::QueuedConnection);
                return;
            }

            if (http_code == 308) {
                // Server accepted up to the Range header offset
                if (!range_hdr.isEmpty())
                    bytes_sent = range_hdr.split('-').last().toLongLong() + 1;
                else
                    bytes_sent = 0; // server wants restart from 0
                qint64 bs  = bytes_sent;
                int    pct = file_size > 0 ? (int)(bs * 100 / file_size) : 0;
                QMetaObject::invokeMethod(self.data(), [self, bs, pct]() {
                    if (!self) return;
                    self->bytes_sent_ = bs;
                    emit self->progress(pct);
                }, Qt::QueuedConnection);
                continue;
            }

            if (http_code == 200 || http_code == 201) {
                QString vid_id = QJsonDocument::fromJson(response).object()["id"].toString();
                QString url    = "https://www.youtube.com/watch?v=" + vid_id;
                QMetaObject::invokeMethod(self.data(), [self, url]() {
                    if (!self) return;
                    emit self->progress(100);
                    emit self->status_changed(obs_module_text("RizzyTos.YouTube.StatusDone"));
                    emit self->completed(url);
                }, Qt::QueuedConnection);
                return;
            }

            // Any other HTTP error
            QString err = QString(obs_module_text("RizzyTos.YouTube.StatusError"))
                          + " HTTP " + QString::number(http_code);
            if (response.contains("\"quotaExceeded\""))
                err = obs_module_text("RizzyTos.YouTube.ErrorQuota");
            else if (response.contains("\"forbidden\"") || http_code == 403)
                err = obs_module_text("RizzyTos.YouTube.ErrorPermissions");
            obs_log(LOG_WARNING, "YouTube chunk upload failed (%ld): %s",
                    http_code, response.constData());
            QMetaObject::invokeMethod(self.data(), [self, err]() {
                if (self) emit self->failed(err);
            }, Qt::QueuedConnection);
            return;
        }
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

QString YouTubeUploader::build_metadata_json() const
{
    QJsonObject snippet;
    snippet["title"]       = QString::fromStdString(yt_settings_.title);
    snippet["description"] = QString::fromStdString(yt_settings_.description);

    QJsonObject status;
    status["privacyStatus"]           = QString::fromStdString(yt_settings_.privacy);
    status["selfDeclaredMadeForKids"] = false;
    status["containsSyntheticMedia"]  = false;
    status["notifySubscribers"]       = true;

    QJsonObject root;
    root["snippet"] = snippet;
    root["status"]  = status;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
