#pragma once

#include "capture/ICaptureBackend.h"

class ScreenCaptureBackend final : public ICaptureBackend {
public:
    QImage captureRegion(const QRect &screenRect, QString *errorCode) override;
};
