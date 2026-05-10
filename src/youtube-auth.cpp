#include "youtube-auth.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QThread>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <keychain.h>
#include <curl/curl.h>

static constexpr const char *KEYCHAIN_SERVICE = "rizzytos-auto-edit";
static constexpr const char *KEYCHAIN_KEY     = "youtube_refresh_token";
static constexpr const char *TOKEN_ENDPOINT   = "https://oauth2.googleapis.com/token";
static constexpr const char *AUTH_ENDPOINT    = "https://accounts.google.com/o/oauth2/v2/auth";
static constexpr const char *SCOPE            = "https://www.googleapis.com/auth/youtube.upload";

static size_t curl_write_cb(char *ptr, size_t sz, size_t n, void *ud)
{
    static_cast<QByteArray *>(ud)->append(ptr, (qsizetype)(sz * n));
    return sz * n;
}

static QString random_base64url(int bytes)
{
    QByteArray buf(bytes, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32 *>(buf.data()), bytes / 4 + 1);
    return buf.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
               .left(bytes);
}

static QString sha256_base64url(const QByteArray &data)
{
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

YouTubeAuth::YouTubeAuth(QObject *parent)
    : QObject(parent)
{
    refresh_token_ = load_refresh_token();
}

bool YouTubeAuth::is_authenticated() const
{
    return !refresh_token_.isEmpty();
}

void YouTubeAuth::start_auth_flow()
{
    if (!tcp_server_) {
        tcp_server_ = new QTcpServer(this);
        connect(tcp_server_, &QTcpServer::newConnection,
                this, &YouTubeAuth::on_new_connection);
    }
    if (!tcp_server_->isListening()) {
        if (!tcp_server_->listen(QHostAddress::LocalHost, 0)) {
            emit auth_failed("No se pudo iniciar el servidor de redirección OAuth.");
            return;
        }
    }
    redirect_port_ = tcp_server_->serverPort();

    code_verifier_ = random_base64url(32);
    QString code_challenge = sha256_base64url(code_verifier_.toUtf8());
    state_ = random_base64url(16);

    QUrl url(AUTH_ENDPOINT);
    QUrlQuery q;
    q.addQueryItem("client_id",             RIZZYTOS_CLIENT_ID);
    q.addQueryItem("redirect_uri",          QString("http://127.0.0.1:%1").arg(redirect_port_));
    q.addQueryItem("response_type",         "code");
    q.addQueryItem("scope",                 SCOPE);
    q.addQueryItem("code_challenge",        code_challenge);
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state",                 state_);
    q.addQueryItem("access_type",           "offline");
    q.addQueryItem("prompt",                "consent");
    url.setQuery(q);

    QDesktopServices::openUrl(url);
}

void YouTubeAuth::on_new_connection()
{
    QTcpSocket *socket = tcp_server_->nextPendingConnection();
    if (!socket) return;

    socket->waitForReadyRead(3000);
    QByteArray request = socket->readAll();

    QString first_line = QString::fromUtf8(request).split('\n').first();
    QUrl req_url("http://localhost" + first_line.split(' ').value(1));
    QUrlQuery params(req_url);

    QString returned_state = params.queryItemValue("state");
    QString code           = params.queryItemValue("code");
    QString error          = params.queryItemValue("error");

    const char *html_ok =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<html><body><h2>Autenticación completada. Puedes cerrar esta ventana.</h2></body></html>";
    const char *html_err =
        "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n"
        "<html><body><h2>Error en autenticación. Revisa RizzyTos.</h2></body></html>";

    tcp_server_->close();

    if (!error.isEmpty()) {
        socket->write(html_err);
        socket->flush();
        socket->deleteLater();
        emit auth_failed("OAuth rechazado: " + error);
        return;
    }

    if (returned_state != state_ || code.isEmpty()) {
        socket->write(html_err);
        socket->flush();
        socket->deleteLater();
        emit auth_failed("Respuesta OAuth inválida (state mismatch o code vacío).");
        return;
    }

    socket->write(html_ok);
    socket->flush();
    socket->deleteLater();

    exchange_code(code);
}

void YouTubeAuth::exchange_code(const QString &code)
{
    // Capture all needed values by value — the thread must not access members directly.
    QString c_id       = QString(RIZZYTOS_CLIENT_ID);
    QString c_secret   = QString(RIZZYTOS_CLIENT_SECRET);
    QString c_verifier = code_verifier_;
    QString c_redirect = QString("http://127.0.0.1:%1").arg(redirect_port_);

    QPointer<YouTubeAuth> self(this);
    QThread *t = QThread::create([self, code, c_id, c_secret, c_verifier, c_redirect]() {
        QUrlQuery body;
        body.addQueryItem("client_id",     c_id);
        body.addQueryItem("client_secret", c_secret);
        body.addQueryItem("code",          code);
        body.addQueryItem("code_verifier", c_verifier);
        body.addQueryItem("redirect_uri",  c_redirect);
        body.addQueryItem("grant_type",    "authorization_code");
        QByteArray post_data = body.query(QUrl::FullyEncoded).toUtf8();

        QByteArray response;
        CURL *curl = curl_easy_init();
        if (!curl) {
            QMetaObject::invokeMethod(self.data(), [self]() {
                if (self) emit self->auth_failed("No se pudo inicializar cURL.");
            }, Qt::QueuedConnection);
            return;
        }

        struct curl_slist *hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");
        curl_easy_setopt(curl, CURLOPT_URL,           TOKEN_ENDPOINT);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    post_data.constData());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)post_data.size());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    hdrs);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response);
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            QString err = QString("Error de red: ") + curl_easy_strerror(res);
            QMetaObject::invokeMethod(self.data(), [self, err]() {
                if (self) emit self->auth_failed(err);
            }, Qt::QueuedConnection);
            return;
        }

        QJsonObject obj = QJsonDocument::fromJson(response).object();
        if (obj.contains("error")) {
            QString err = "Error OAuth: " + obj["error_description"].toString();
            QMetaObject::invokeMethod(self.data(), [self, err]() {
                if (self) emit self->auth_failed(err);
            }, Qt::QueuedConnection);
            return;
        }

        QString access_tok  = obj["access_token"].toString();
        QString refresh_tok = obj["refresh_token"].toString();
        QMetaObject::invokeMethod(self.data(), [self, access_tok, refresh_tok]() {
            if (!self) return;
            self->access_token_  = access_tok;
            self->refresh_token_ = refresh_tok;
            self->store_refresh_token(refresh_tok);
            emit self->authenticated(access_tok);
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void YouTubeAuth::refresh_access_token()
{
    if (refresh_token_.isEmpty()) {
        emit auth_failed("No hay refresh_token almacenado.");
        return;
    }

    QString c_id      = QString(RIZZYTOS_CLIENT_ID);
    QString c_secret  = QString(RIZZYTOS_CLIENT_SECRET);
    QString c_refresh = refresh_token_;

    QPointer<YouTubeAuth> self(this);
    QThread *t = QThread::create([self, c_id, c_secret, c_refresh]() {
        QUrlQuery body;
        body.addQueryItem("client_id",     c_id);
        body.addQueryItem("client_secret", c_secret);
        body.addQueryItem("refresh_token", c_refresh);
        body.addQueryItem("grant_type",    "refresh_token");
        QByteArray post_data = body.query(QUrl::FullyEncoded).toUtf8();

        QByteArray response;
        CURL *curl = curl_easy_init();
        if (!curl) {
            QMetaObject::invokeMethod(self.data(), [self]() {
                if (self) emit self->auth_failed("No se pudo inicializar cURL.");
            }, Qt::QueuedConnection);
            return;
        }

        struct curl_slist *hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded");
        curl_easy_setopt(curl, CURLOPT_URL,           TOKEN_ENDPOINT);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    post_data.constData());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)post_data.size());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    hdrs);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response);
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            QString err = QString("Error de red: ") + curl_easy_strerror(res);
            QMetaObject::invokeMethod(self.data(), [self, err]() {
                if (self) emit self->auth_failed(err);
            }, Qt::QueuedConnection);
            return;
        }

        QJsonObject obj = QJsonDocument::fromJson(response).object();
        if (obj.contains("error")) {
            QMetaObject::invokeMethod(self.data(), [self]() {
                if (!self) return;
                self->delete_refresh_token();
                self->refresh_token_.clear();
                emit self->auth_failed("Refresh token inválido.");
            }, Qt::QueuedConnection);
            return;
        }

        QString tok = obj["access_token"].toString();
        QMetaObject::invokeMethod(self.data(), [self, tok]() {
            if (!self) return;
            self->access_token_ = tok;
            emit self->token_refreshed(tok);
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void YouTubeAuth::disconnect_account()
{
    delete_refresh_token();
    refresh_token_.clear();
    access_token_.clear();
}

void YouTubeAuth::store_refresh_token(const QString &token)
{
    auto *job = new QKeychain::WritePasswordJob(KEYCHAIN_SERVICE, this);
    job->setKey(KEYCHAIN_KEY);
    job->setTextData(token);
    job->setAutoDelete(true);
    job->start();
}

QString YouTubeAuth::load_refresh_token() const
{
    // Synchronous read at startup (acceptable: called once during module load)
    QKeychain::ReadPasswordJob job(KEYCHAIN_SERVICE);
    job.setKey(KEYCHAIN_KEY);
    QEventLoop loop;
    connect(&job, &QKeychain::ReadPasswordJob::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    if (job.error() != QKeychain::NoError)
        return {};
    return job.textData();
}

void YouTubeAuth::delete_refresh_token()
{
    auto *job = new QKeychain::DeletePasswordJob(KEYCHAIN_SERVICE, this);
    job->setKey(KEYCHAIN_KEY);
    job->setAutoDelete(true);
    job->start();
}
