#include "app/MacLoginItem.h"

#include <QDebug>

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

bool MacLoginItem::isEnabled()
{
    SMAppService *service = [SMAppService mainAppService];
    const SMAppServiceStatus status = service.status;
    const bool on = status == SMAppServiceStatusEnabled;
    qInfo() << "MacLoginItem: status=" << static_cast<int>(status) << " enabled=" << on;
    return on;
}

bool MacLoginItem::ensureEnabled(QString *errorCode)
{
    if (isEnabled()) {
        qInfo() << "MacLoginItem: already enabled";
        return true;
    }
    qInfo() << "MacLoginItem: register default on";
    return setEnabled(true, errorCode);
}

bool MacLoginItem::setEnabled(bool on, QString *errorCode)
{
    SMAppService *service = [SMAppService mainAppService];
    NSError *error = nil;
    BOOL ok = NO;
    if (on) {
        ok = [service registerAndReturnError:&error];
        qInfo() << "MacLoginItem: register ok=" << static_cast<bool>(ok);
    } else {
        ok = [service unregisterAndReturnError:&error];
        qInfo() << "MacLoginItem: unregister ok=" << static_cast<bool>(ok);
    }
    if (!ok) {
        const QString desc = error ? QString::fromNSString(error.localizedDescription) : QString();
        qWarning() << "MacLoginItem: failed on=" << on << " error=" << desc;
        if (errorCode) {
            *errorCode = QStringLiteral("LOGIN_ITEM_FAILED");
        }
        return false;
    }
    return true;
}
