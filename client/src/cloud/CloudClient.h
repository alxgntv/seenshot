#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

class AuthSession;
class QNetworkAccessManager;
class QNetworkReply;

struct CloudConfirmResult {
    QString shotId;
    QString publicUrl;
    int usedBytes = 0;
    QStringList evictedIds;
};

using CloudUploadProgress = std::function<void(qint64 sent, qint64 total)>;
using CloudQuotaCallback =
    std::function<void(bool ok, int usedBytes, QString plan, int limitBytes, QString errorCode)>;

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
    // ─── Ariadne's Thread [AT-0654] ─────────────────────
    // What: GET /v1/quota on QNetworkReply::finished, never QEventLoop
    // Why:  Settings and annotate cannot freeze until the Worker answers
    // Date: 2026-09-10
    // Related: [AT-0642] CloudClient.cpp:fetchQuota, [AT-0655] AuthSession.cpp:cachedIdToken,
    //          [AT-0643] SettingsWindow.cpp:refreshQuota, [AT-0657] AnnotateWindow.cpp:applyWatermarkQuotaResult
    // ─────────────────────────────────────────────────────
    void fetchQuota(const CloudQuotaCallback &done);
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
    QNetworkReply *m_quotaReply = nullptr;
    int m_quotaGeneration = 0;
};
