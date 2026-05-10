#pragma once
#include <QObject>
#include <QString>
#include <QTcpServer>

// Handles OAuth 2.0 PKCE flow for YouTube and secure token persistence.
// Emits authenticated() when a valid access token is available,
// auth_failed(reason) on any unrecoverable error.
class YouTubeAuth : public QObject {
    Q_OBJECT
public:
    explicit YouTubeAuth(QObject *parent = nullptr);

    // Returns true if a stored refresh_token exists (user has connected before).
    bool is_authenticated() const;

    // Start the PKCE authorization flow: open browser, spin up loopback server.
    void start_auth_flow();

    // Exchange refresh_token for a new access_token. Emits token_refreshed() or
    // auth_failed() depending on outcome.
    void refresh_access_token();

    // Revoke stored tokens and clear keychain entry.
    void disconnect_account();

    QString access_token() const { return access_token_; }

signals:
    void authenticated(const QString &access_token);
    void token_refreshed(const QString &access_token);
    void auth_failed(const QString &reason);

private slots:
    void on_new_connection();

private:
    void exchange_code(const QString &code);
    void store_refresh_token(const QString &token);
    QString load_refresh_token() const;
    void delete_refresh_token();

    QTcpServer *tcp_server_    = nullptr;
    QString     code_verifier_;
    QString     state_;
    QString     access_token_;
    QString     refresh_token_;
    int         redirect_port_ = 0;
};
