#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

class AuthSession;
class QNetworkAccessManager;

struct CloudConfirmResult {
    QString shotId;
    QString publicUrl;
    int usedBytes = 0;
    QStringList evictedIds;
};

using CloudUploadProgress = std::function<void(qint64 sent, qint64 total)>;

// ─── Ariadne's Thread [AT-0020] ─────────────────────
// What: API client for presign, confirm, publish, account
// Why:  Cloud save and share only on explicit user action
// Date: 2026-08-25
// Related: client/src/cloud/CloudClient.cpp
// ─────────────────────────────────────────────────────
class CloudClient {
public:
    CloudClient(AuthSession *auth, QNetworkAccessManager *nam);

    bool uploadPrivate(const QByteArray &png, const QString &fileId, CloudConfirmResult *result, QString *errorCode);
    bool uploadAndPublish(const QByteArray &png, const QString &fileId, QString *publicUrl, CloudConfirmResult *result,
                          QString *errorCode, const CloudUploadProgress &progress = {});
    bool publishExisting(const QString &shotId, QString *publicUrl, QString *errorCode);
    bool createCheckoutUrl(QString *url, QString *errorCode);
    bool fetchQuota(int *usedBytes, QString *plan, QString *errorCode);
    bool exportAccount(const QString &zipPath, QString *errorCode);
    bool deleteAccount(QString *errorCode);

private:
    bool authorizedJson(const QString &method, const QString &path, const QByteArray &body, QByteArray *response,
                        QString *errorCode);
    bool presignAndPut(const QByteArray &png, const QString &fileId, QString *shotId, QString *errorCode,
                       const CloudUploadProgress &progress = {});
    bool confirm(const QString &shotId, bool publish, CloudConfirmResult *result, QString *errorCode);
    void postUploadProgress(const QString &shotId, qint64 sent, qint64 total, bool done);

    AuthSession *m_auth = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
};
