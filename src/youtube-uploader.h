#pragma once
#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include "plugin-settings.h"

// Performs a YouTube Data API v3 resumable upload.
// Caller must supply a valid access_token before calling start().
// Emits token_expired() if the API returns 401 — caller should refresh the
// token and call set_access_token() + retry().
class YouTubeUploader : public QObject {
    Q_OBJECT
public:
    explicit YouTubeUploader(QObject *parent = nullptr);

    void set_access_token(const QString &token);
    void start(const QString &video_path, const YouTubeSettings &settings);
    void retry();

signals:
    void progress(int percent);           // 0–100
    void status_changed(const QString &); // human-readable status text
    void completed(const QString &url);   // e.g. https://www.youtube.com/watch?v=...
    void failed(const QString &reason);
    void token_expired();                 // caller must refresh then call retry()

private slots:
    void on_initiate_finished();
    void on_chunk_finished();

private:
    void initiate_session();
    void upload_next_chunk();
    QString build_metadata_json() const;

    QNetworkAccessManager *nam_           = nullptr;
    QString                access_token_;
    QString                video_path_;
    YouTubeSettings        yt_settings_;

    QString                upload_url_;   // resumable upload URI
    qint64                 file_size_     = 0;
    qint64                 bytes_sent_    = 0;

    static constexpr qint64 CHUNK_SIZE = 8 * 1024 * 1024; // 8 MB
};
