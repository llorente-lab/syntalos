#ifndef ORBBEC_CAMERA_H
#define ORBBEC_CAMERA_H

#include <QObject>
#include <QScopedPointer>
#include <opencv2/core.hpp>
#include "datactl/frametype.h"
#include "datactl/syclock.h"
#include "datactl/timesync.h"
#include <libobsensor/ObSensor.hpp>

namespace Syntalos
{
Q_DECLARE_LOGGING_CATEGORY(logOrbbecCamera)
}

struct OrbbecStreamProfile {
    int width;
    int height;
    int fps;
    OBFormat format;
};

class OrbbecCameraData;

class OrbbecCamera
{
public:
    OrbbecCamera();
    ~OrbbecCamera();

    void setDeviceIndex(int index);
    int deviceIndex() const;

    void setStartTime(const symaster_timepoint &time);

    bool connect();
    void disconnect();

    QList<OrbbecStreamProfile> readStreamProfiles(OBSensorType sensorType);
    void setStreamProfile(OBSensorType sensorType, const OrbbecStreamProfile &profile);

    bool recordFrame(Frame &frame, SecondaryClockSynchronizer *clockSync);

    QString lastError() const;

    static QList<QPair<QString, int>> availableOrbbecCameras();

    // Orbbec-specific methods
    void enableDepthStream(bool enable);
    void enableColorStream(bool enable);
    void enableIRStream(bool enable);

private:
    QScopedPointer<OrbbecCameraData> d;
    void fail(const QString &msg);
};

#endif // ORBBEC_CAMERA_H
