#pragma once

#include "auth/ISecureStore.h"

class KeychainStore final : public ISecureStore {
public:
    bool write(const QString &key, const QByteArray &value, QString *errorCode) override;
    QByteArray read(const QString &key, QString *errorCode) override;
    bool remove(const QString &key, QString *errorCode) override;
};
