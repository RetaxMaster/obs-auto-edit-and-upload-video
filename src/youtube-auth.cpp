#include "youtube-auth.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QTcpSocket>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <keychain.h>

static constexpr const char *KEYCHAIN_SERVICE = "rizzytos-auto-edit";
static constexpr const char *KEYCHAIN_KEY     = "youtube_refresh_token";
static constexpr const char *TOKEN_ENDPOINT   = "https://oauth2.googleapis.com/token";
static constexpr const char *AUTH_ENDPOINT    = "https://accounts.google.com/o/oauth2/v2/auth";
static constexpr const char *SCOPE            = "https://www.googleapis.com/auth/youtube.upload";

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
    , nam_(new QNetworkAccessManager(this))
{
    refresh_token_ = load_refresh_token();
}

bool YouTubeAuth::is_authenticated() const
{
    return !refresh_token_.isEmpty();
}

void YouTubeAuth::start_auth_flow()
{
    // Spin up loopback server on an OS-assigned port
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

    // PKCE
    code_verifier_ = random_base64url(32);
    QString code_challenge = sha256_base64url(code_verifier_.toUtf8());
    state_         = random_base64url(16);

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

    // Read the HTTP request line (blocking with short timeout is fine here)
    socket->waitForReadyRead(3000);
    QByteArray request = socket->readAll();

    // Parse GET /?code=...&state=... HTTP/1.1
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
    QNetworkRequest req{QUrl(TOKEN_ENDPOINT)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem("client_id",     RIZZYTOS_CLIENT_ID);
    body.addQueryItem("client_secret", RIZZYTOS_CLIENT_SECRET);
    body.addQueryItem("code",          code);
    body.addQueryItem("code_verifier", code_verifier_);
    body.addQueryItem("redirect_uri",  QString("http://127.0.0.1:%1").arg(redirect_port_));
    body.addQueryItem("grant_type",    "authorization_code");

    QNetworkReply *reply = nam_->post(req, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, &YouTubeAuth::on_token_reply_finished);
}

void YouTubeAuth::on_token_reply_finished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit auth_failed("Error de red al intercambiar código: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject   obj = doc.object();

    if (obj.contains("error")) {
        emit auth_failed("Error OAuth: " + obj["error_description"].toString());
        return;
    }

    access_token_  = obj["access_token"].toString();
    refresh_token_ = obj["refresh_token"].toString();
    store_refresh_token(refresh_token_);

    emit authenticated(access_token_);
}

void YouTubeAuth::refresh_access_token()
{
    if (refresh_token_.isEmpty()) {
        emit auth_failed("No hay refresh_token almacenado.");
        return;
    }

    QNetworkRequest req{QUrl(TOKEN_ENDPOINT)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem("client_id",     RIZZYTOS_CLIENT_ID);
    body.addQueryItem("client_secret", RIZZYTOS_CLIENT_SECRET);
    body.addQueryItem("refresh_token", refresh_token_);
    body.addQueryItem("grant_type",    "refresh_token");

    QNetworkReply *reply = nam_->post(req, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, &YouTubeAuth::on_refresh_reply_finished);
}

void YouTubeAuth::on_refresh_reply_finished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit auth_failed("Error de red al refrescar token: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject   obj = doc.object();

    if (obj.contains("error")) {
        // Refresh token revoked — clear stored token so UI shows Connect button
        delete_refresh_token();
        refresh_token_.clear();
        emit auth_failed("Refresh token inválido: " + obj["error_description"].toString());
        return;
    }

    access_token_ = obj["access_token"].toString();
    emit token_refreshed(access_token_);
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
