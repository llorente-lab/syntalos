#include "orbbec_camera.h"

#include <QDebug>
#include <QFileInfo>
#include "datactl/vipsutils.h"

namespace Syntalos
{
Q_LOGGING_CATEGORY(logOrbbecCamera, "mod.orbbec-camera")
}

class OrbbecCameraData
{
public:
    OrbbecCameraData()
        : deviceIndex(0),
          connected(false),
          failed(false),
          droppedFrameCount(0)
    {
    }

    std::chrono::time_point<symaster_clock> startTime;
    std::shared_ptr<ob::Pipeline> pipe;
    std::shared_ptr<ob::Device> device;
    int deviceIndex;

    bool connected;
    bool failed;

    uint droppedFrameCount;
    QString lastError;

    std::shared_ptr<ob::Config> config;
};

OrbbecCamera::OrbbecCamera()
    : d(new OrbbecCameraData())
{
    d->config = std::make_shared<ob::Config>();
}

OrbbecCamera::~OrbbecCamera()
{
    disconnect();
}

void OrbbecCamera::fail(const QString &msg)
{
    d->failed = true;
    d->lastError = msg;
}

void OrbbecCamera::setDeviceIndex(int index)
{
    d->deviceIndex = index;
}

int OrbbecCamera::deviceIndex() const
{
    return d->deviceIndex;
}

void OrbbecCamera::setStartTime(const symaster_timepoint &time)
{
    d->startTime = time;
}

bool OrbbecCamera::connect()
{
    if (d->connected) {
        if (d->failed) {
            qDebug() << "Reconnecting Orbbec camera" << d->deviceIndex << "to recover from previous failure.";
            disconnect();
        } else {
            qWarning() << "Tried to reconnect already connected Orbbec camera.";
            return false;
        }
    }

    try {
        ob::Context ctx;
        auto devices = ctx.queryDeviceList();
        if (d->deviceIndex >= devices->deviceCount()) {
            fail("Invalid device index.");
            return false;
        }

        d->device = devices->getDevice(d->deviceIndex);
        d->pipe = std::make_shared<ob::Pipeline>(d->device);

        d->failed = false;
        d->connected = true;

        d->startTime = currentTimePoint();

        qDebug() << "Initialized Orbbec camera" << d->deviceIndex;
        return true;
    } catch (const ob::Error& e) {
        fail(QString("Failed to connect: %1").arg(e.getMessage()));
        return false;
    }
}

void OrbbecCamera::disconnect()
{
    if (d->pipe) {
        d->pipe->stop();
    }
    d->pipe.reset();
    d->device.reset();
    if (d->connected)
        qDebug() << "Disconnected Orbbec camera" << d->deviceIndex;
    d->connected = false;
}

QList<OrbbecStreamProfile> OrbbecCamera::readStreamProfiles(OBSensorType sensorType)
{
    QList<OrbbecStreamProfile> result;
    if (!d->pipe) return result;

    try {
        auto profiles = d->pipe->getStreamProfileList(sensorType);
        for (int i = 0; i < profiles->count(); i++) {
            auto videoProfile = profiles->getProfile(i)->as<ob::VideoStreamProfile>();
            if (videoProfile) {
                OrbbecStreamProfile profile;
                profile.width = videoProfile->width();
                profile.height = videoProfile->height();
                profile.fps = videoProfile->fps();
                profile.format = videoProfile->format();
                result.append(profile);
            }
        }
    } catch (const ob::Error& e) {
        qWarning() << "Failed to read stream profiles:" << e.getMessage();
    }

    return result;
}

void OrbbecCamera::setStreamProfile(OBSensorType sensorType, const OrbbecStreamProfile &profile)
{
    if (!d->config) return;

    try {
        auto profiles = d->pipe->getStreamProfileList(sensorType);
        auto videoProfile = profiles->getVideoStreamProfile(profile.width, profile.height, profile.format, profile.fps);
        if (videoProfile) {
            d->config->enableStream(videoProfile);
        }
    } catch (const ob::Error& e) {
        qWarning() << "Failed to set stream profile:" << e.getMessage();
    }
}

bool OrbbecCamera::recordFrame(Frame &frame, SecondaryClockSynchronizer *clockSync)
{
    if (!d->pipe) return false;

    try {
        auto frameSet = d->pipe->waitForFrames(100);
        if (!frameSet) {
            d->droppedFrameCount++;
            if (d->droppedFrameCount > 80)
                fail("Too many dropped frames. Giving up.");
            return false;
        }

        auto depthFrame = frameSet->depthFrame();
        if (depthFrame) {
            auto frameRecvTime = std::chrono::duration_cast<microseconds_t>(
                std::chrono::steady_clock::now().time_since_epoch());
            auto driverFrameTimestamp = microseconds_t(depthFrame->timeStamp());

            clockSync->processTimestamp(frameRecvTime, driverFrameTimestamp);

            frame.time = usecToMsec(frameRecvTime);
            frame.mat = depthFrameToVips(depthFrame);
            return true;
        }
    } catch (const ob::Error& e) {
        fail(QString("Failed to record frame: %1").arg(e.getMessage()));
    }

    return false;
}

QString OrbbecCamera::lastError() const
{
    return d->lastError;
}

QList<QPair<QString, int>> OrbbecCamera::availableOrbbecCameras()
{
    QList<QPair<QString, int>> res;

    try {
        ob::Context ctx;
        auto devices = ctx.queryDeviceList();
        for (int i = 0; i < devices->deviceCount(); ++i) {
            auto device = devices->getDevice(i);
            auto info = device->getDeviceInfo();
            QString name = QString::fromStdString(info->name());
            res.append(qMakePair(name, i));
        }
    } catch (const ob::Error& e) {
        qWarning() << "Failed to enumerate Orbbec cameras:" << e.getMessage();
    }

    return res;
}

void OrbbecCamera::enableDepthStream(bool enable)
{
    if (d->config) {
        if (enable) {
            auto profiles = d->pipe->getStreamProfileList(OB_SENSOR_DEPTH);
            if (profiles->count() > 0) {
                d->config->enableStream(profiles->getProfile(0));
            }
        } else {
            d->config->disableStream(OB_STREAM_DEPTH);
        }
    }
}

void OrbbecCamera::enableColorStream(bool enable)
{
    if (d->config) {
        if (enable) {
            auto profiles = d->pipe->getStreamProfileList(OB_SENSOR_COLOR);
            if (profiles->count() > 0) {
                d->config->enableStream(profiles->getProfile(0));
            }
        } else {
            d->config->disableStream(OB_STREAM_COLOR);
        }
    }
}

void OrbbecCamera::enableIRStream(bool enable)
{
    if (d->config) {
        if (enable) {
            auto profiles = d->pipe->getStreamProfileList(OB_SENSOR_IR);
            if (profiles->count() > 0) {
                d->config->enableStream(profiles->getProfile(0));
            }
        } else {
            d->config->disableStream(OB_STREAM_IR);
        }
    }
}

static vips::VImage depthFrameToVips(std::shared_ptr<ob::DepthFrame> depthFrame)
{
    // Implement conversion from Orbbec depth frame to VIPS image
    // This is a placeholder implementation and needs to be adapted to your specific needs
    int width = depthFrame->width();
    int height = depthFrame->height();
    uint16_t* data = (uint16_t*)depthFrame->data();
    
    // Create a VIPS image from the depth data
    vips::VImage depthImage = vips::VImage::new_from_memory(
        data, 
        width * height * sizeof(uint16_t), 
        width, 
        height, 
        1, 
        VIPS_FORMAT_USHORT
    );
    
    return depthImage;
}

static vips::VImage colorFrameToVips(std::shared_ptr<ob::ColorFrame> colorFrame)
{
    // Implement conversion from Orbbec color frame to VIPS image
    // This is a placeholder implementation and needs to be adapted to your specific needs
    int width = colorFrame->width();
    int height = colorFrame->height();
    uint8_t* data = (uint8_t*)colorFrame->data();
    
    // Assume RGB format, adjust if necessary
    vips::VImage colorImage = vips::VImage::new_from_memory(
        data, 
        width * height * 3, 
        width, 
        height, 
        3, 
        VIPS_FORMAT_UCHAR
    );
    
    return colorImage;
}

static vips::VImage irFrameToVips(std::shared_ptr<ob::IRFrame> irFrame)
{
    // Implement conversion from Orbbec IR frame to VIPS image
    // This is a placeholder implementation and needs to be adapted to your specific needs
    int width = irFrame->width();
    int height = irFrame->height();
    uint16_t* data = (uint16_t*)irFrame->data();
    
    // Create a VIPS image from the IR data
    vips::VImage irImage = vips::VImage::new_from_memory(
        data, 
        width * height * sizeof(uint16_t), 
        width, 
        height, 
        1, 
        VIPS_FORMAT_USHORT
    );
    
    return irImage;
}
/*
TO DO:
Do orbbec acquire
Create orbbec settings
Test
Deploy
*/
