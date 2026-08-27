#include "cloud/CloudClient.h"

#include "app/Config.h"
#include "auth/AuthSession.h"

#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

CloudClient::CloudClient(AuthSession *auth, QNetworkAccessManager *nam)
    : m_auth(auth)
    , m_nam(nam)
{
}

bool CloudClient::authorizedJson(const QString &method, const QString &path, const QByteArray &body,
                                QByteArray *response, QString *errorCode)
{
    QString token;
    if (!m_auth->ensureIdToken(&token, errorCode)) {
        return false;
    }
    QUrl url(Config::apiBaseUrl() + path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    qInfo() << "CloudClient:" << method << url.toString();
    QNetworkReply *reply = nullptr;
    if (method == QLatin1String("GET")) {
        reply = m_nam->get(request);
    } else if (method == QLatin1String("DELETE")) {
        reply = m_nam->sendCustomRequest(request, "DELETE", body);
    } else {
        reply = m_nam->sendCustomRequest(request, method.toLatin1(), body);
    }
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        if (errorCode) {
            *errorCode = QStringLiteral("UPLOAD_FAILED");
        }
        return false;
    }
    *response = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    qInfo() << "CloudClient: status=" << status << " bytes=" << response->size();
    if (status < 200 || status >= 300) {
        const QJsonObject json = QJsonDocument::fromJson(*response).object();
        const QString code = json.value(QStringLiteral("code")).toString();
        if (errorCode) {
            *errorCode = code.isEmpty() ? QStringLiteral("UPLOAD_FAILED") : code;
        }
        qWarning() << "CloudClient: error body" << QString::fromUtf8(*response).left(400);
        return false;
    }
    return true;
}

// ─── Ariadne's Thread [AT-0170] ─────────────────────
// What: Abort PUT only when no bytes move (QNetworkRequest::setTransferTimeout)
// Why:  Hard 60s QTimer killed 3.8 MB Worker PUTs that were still transferring
// Date: 2026-08-26
// Related: [AT-0169] AnnotateWindow.cpp:share, [AT-0167] backend/src/index.ts:putInbox
// ─────────────────────────────────────────────────────
bool CloudClient::presignAndPut(const QByteArray &png, const QString &fileId, QString *shotId, QString *errorCode,
                               const CloudUploadProgress &progress)
{
    QJsonObject body;
    body.insert(QStringLiteral("bytes"), png.size());
    body.insert(QStringLiteral("contentType"), QStringLiteral("image/png"));
    body.insert(QStringLiteral("fileId"), fileId);
    QByteArray response;
    qInfo() << "CloudClient: presign fileId=" << fileId << " pngBytes=" << png.size();
    if (!authorizedJson(QStringLiteral("POST"), QStringLiteral("/v1/uploads/presign"),
                        QJsonDocument(body).toJson(QJsonDocument::Compact), &response, errorCode)) {
        return false;
    }
    const QJsonObject json = QJsonDocument::fromJson(response).object();
    *shotId = json.value(QStringLiteral("shotId")).toString();
    const QString uploadUrl = json.value(QStringLiteral("uploadUrl")).toString();
    qInfo() << "CloudClient: presign shotId=" << *shotId << " pngBytes=" << png.size() << " fileId=" << fileId;
    QNetworkRequest putRequest{QUrl(uploadUrl)};
    putRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
    putRequest.setHeader(QNetworkRequest::ContentLengthHeader, png.size());
    putRequest.setTransferTimeout(60000);
    QNetworkReply *reply = m_nam->put(putRequest, png);
    QEventLoop loop;
    qint64 lastProgressPostMs = 0;
    const QString progressShotId = *shotId;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::uploadProgress, &loop,
                     [this, &progress, &lastProgressPostMs, progressShotId](qint64 sent, qint64 total) {
                         qInfo() << "CloudClient: R2 PUT progress sent=" << sent << " total=" << total;
                         if (progress) {
                             progress(sent, total);
                         }
                         if (total <= 0) {
                             qInfo() << "CloudClient: skip progress POST total=" << total << " shot=" << progressShotId;
                             return;
                         }
                         const qint64 now = QDateTime::currentMSecsSinceEpoch();
                         const bool done = sent >= total;
                         if (!done && now - lastProgressPostMs < 200) {
                             return;
                         }
                         lastProgressPostMs = now;
                         postUploadProgress(progressShotId, sent, total, done);
                     });
    loop.exec();
    const QNetworkReply::NetworkError netError = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray putBody = reply->readAll();
    const QString netErrorString = reply->errorString();
    reply->deleteLater();
    qInfo() << "CloudClient: R2 PUT status=" << status << " netError=" << static_cast<int>(netError)
            << netErrorString;
    if (netError != QNetworkReply::NoError || status < 200 || status >= 300) {
        qWarning() << "CloudClient: R2 PUT failed status=" << status << " body=" << QString::fromUtf8(putBody).left(400);
        if (errorCode) {
            *errorCode = QStringLiteral("UPLOAD_FAILED");
        }
        return false;
    }
    postUploadProgress(progressShotId, png.size(), png.size(), true);
    return true;
}

// ─── Ariadne's Thread [AT-0212] ─────────────────────
// What: Fire-and-forget POST of PUT bytes to /v1/uploads/progress/{shotId}
// Why:  Counting inside putInbox TransformStream 500'd a 5.4MB upload; Mac already has sent/total
// Date: 2026-08-27
// Related: [AT-0170] CloudClient.cpp:presignAndPut, [AT-0213] backend/src/index.ts:putProgress
// ─────────────────────────────────────────────────────
void CloudClient::postUploadProgress(const QString &shotId, qint64 sent, qint64 total, bool done)
{
    if (shotId.isEmpty() || total <= 0 || sent < 0) {
        qWarning() << "CloudClient: progress POST skipped shot=" << shotId << " sent=" << sent << " total=" << total
                   << " done=" << done;
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("sent"), sent);
    body.insert(QStringLiteral("total"), total);
    body.insert(QStringLiteral("done"), done);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QUrl url(Config::apiBaseUrl() + QStringLiteral("/v1/uploads/progress/")
                   + QString::fromUtf8(QUrl::toPercentEncoding(shotId)));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_nam->post(request, payload);
    qInfo() << "CloudClient: progress POST shot=" << shotId << " sent=" << sent << " total=" << total
            << " done=" << done << " bytes=" << payload.size();
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, shotId]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        qInfo() << "CloudClient: progress POST finished shot=" << shotId << " status=" << status
                << " netError=" << static_cast<int>(netError);
        reply->deleteLater();
    });
}

bool CloudClient::confirm(const QString &shotId, bool publish, CloudConfirmResult *result, QString *errorCode)
{
    QJsonObject body;
    body.insert(QStringLiteral("shotId"), shotId);
    body.insert(QStringLiteral("publish"), publish);
    QByteArray response;
    if (!authorizedJson(QStringLiteral("POST"), QStringLiteral("/v1/uploads/confirm"),
                        QJsonDocument(body).toJson(QJsonDocument::Compact), &response, errorCode)) {
        return false;
    }
    const QJsonObject json = QJsonDocument::fromJson(response).object();
    result->shotId = json.value(QStringLiteral("shotId")).toString();
    result->publicUrl = json.value(QStringLiteral("publicUrl")).toString();
    result->usedBytes = json.value(QStringLiteral("usedBytes")).toInt();
    result->evictedIds.clear();
    const QJsonArray evicted = json.value(QStringLiteral("evictedIds")).toArray();
    for (const QJsonValue &value : evicted) {
        result->evictedIds.append(value.toString());
    }
    qInfo() << "CloudClient: confirm shot=" << result->shotId << " used=" << result->usedBytes
            << " evicted=" << result->evictedIds.size() << " publish=" << publish;
    return true;
}

bool CloudClient::uploadPrivate(const QByteArray &png, const QString &fileId, CloudConfirmResult *result, QString *errorCode)
{
    QString shotId;
    qInfo() << "CloudClient: uploadPrivate fileId=" << fileId << " pngBytes=" << png.size();
    if (!presignAndPut(png, fileId, &shotId, errorCode)) {
        return false;
    }
    return confirm(shotId, false, result, errorCode);
}

bool CloudClient::uploadAndPublish(const QByteArray &png, const QString &fileId, QString *publicUrl, CloudConfirmResult *result,
                                  QString *errorCode, const CloudUploadProgress &progress)
{
    QString shotId;
    qInfo() << "CloudClient: uploadAndPublish fileId=" << fileId << " pngBytes=" << png.size();
    if (!presignAndPut(png, fileId, &shotId, errorCode, progress)) {
        return false;
    }
    if (!confirm(shotId, true, result, errorCode)) {
        return false;
    }
    *publicUrl = result->publicUrl;
    qInfo() << "CloudClient: uploadAndPublish publicUrl=" << *publicUrl << " shotId=" << shotId;
    return !publicUrl->isEmpty();
}

bool CloudClient::publishExisting(const QString &shotId, QString *publicUrl, QString *errorCode)
{
    QJsonObject body;
    body.insert(QStringLiteral("shotId"), shotId);
    QByteArray response;
    if (!authorizedJson(QStringLiteral("POST"), QStringLiteral("/v1/shots/publish"),
                        QJsonDocument(body).toJson(QJsonDocument::Compact), &response, errorCode)) {
        return false;
    }
    *publicUrl = QJsonDocument::fromJson(response).object().value(QStringLiteral("publicUrl")).toString();
    qInfo() << "CloudClient: publishExisting" << shotId << *publicUrl;
    return !publicUrl->isEmpty();
}

bool CloudClient::createCheckoutUrl(QString *url, QString *errorCode)
{
    QByteArray response;
    if (!authorizedJson(QStringLiteral("POST"), QStringLiteral("/v1/billing/checkout"), QByteArrayLiteral("{}"),
                        &response, errorCode)) {
        return false;
    }
    *url = QJsonDocument::fromJson(response).object().value(QStringLiteral("url")).toString();
    qInfo() << "CloudClient: checkout url empty=" << url->isEmpty();
    return !url->isEmpty();
}

bool CloudClient::fetchQuota(int *usedBytes, QString *plan, QString *errorCode)
{
    QByteArray response;
    if (!authorizedJson(QStringLiteral("GET"), QStringLiteral("/v1/quota"), {}, &response, errorCode)) {
        return false;
    }
    const QJsonObject json = QJsonDocument::fromJson(response).object();
    *usedBytes = json.value(QStringLiteral("usedBytes")).toInt();
    *plan = json.value(QStringLiteral("plan")).toString();
    qInfo() << "CloudClient: quota used=" << *usedBytes << " plan=" << *plan;
    return true;
}

bool CloudClient::exportAccount(const QString &zipPath, QString *errorCode)
{
    QByteArray response;
    if (!authorizedJson(QStringLiteral("GET"), QStringLiteral("/v1/account/export"), {}, &response, errorCode)) {
        return false;
    }
    QFile file(zipPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorCode) {
            *errorCode = QStringLiteral("EXPORT_FAILED");
        }
        return false;
    }
    file.write(response);
    qInfo() << "CloudClient: export wrote" << zipPath << " bytes=" << response.size();
    return true;
}

bool CloudClient::deleteAccount(QString *errorCode)
{
    QByteArray response;
    if (!authorizedJson(QStringLiteral("DELETE"), QStringLiteral("/v1/account"), {}, &response, errorCode)) {
        if (errorCode && errorCode->isEmpty()) {
            *errorCode = QStringLiteral("DELETE_ACCOUNT_FAILED");
        }
        return false;
    }
    qInfo() << "CloudClient: account deleted";
    return true;
}
