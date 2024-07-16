/*
 * Copyright (C) 2016-2020 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ueyecameramodule.h"

#include <QDir>
#include <QTimer>

#include "streams/frametype.h"
#include "ueyecamera.h"
#include "ueyecamerasettingsdialog.h"

SYNTALOS_MODULE(UEyeCameraModule)

class UEyeCameraModule : public AbstractModule
{
    Q_OBJECT

private:
    QTimer *m_evTimer;
    UEyeCamera *m_camera;
    std::shared_ptr<DataStream<Frame>> m_outStream;

    UEyeCameraSettingsDialog *m_camSettingsWindow;
    double m_fps;
    std::atomic_int m_currentFps;

public:
    explicit UEyeCameraModule(QObject *parent = nullptr)
        : AbstractModule(parent),
          m_camera(nullptr),
          m_camSettingsWindow(nullptr)
    {
        m_outStream = registerOutputPort<Frame>(QStringLiteral("video"), QStringLiteral("Video"));
        m_camera = new UEyeCamera;

        m_camSettingsWindow = new UEyeCameraSettingsDialog(m_camera);
        addSettingsWindow(m_camSettingsWindow);

        m_evTimer = new QTimer(this);
        m_evTimer->setInterval(200);
        connect(m_evTimer, &QTimer::timeout, this, &UEyeCameraModule::checkCamStatus);

        // set window titles
        setName(name());
    }

    void setName(const QString &name) override
    {
        AbstractModule::setName(name);
        m_camSettingsWindow->setWindowTitle(QStringLiteral("Settings for %1").arg(name));
    }

    ModuleFeatures features() const override
    {
        return ModuleFeature::RUN_THREADED | ModuleFeature::SHOW_SETTINGS;
    }

    bool prepare(const TestSubject &) override
    {
        m_fps = m_camSettingsWindow->framerate();
        m_currentFps = m_fps;

        m_outStream->setMetadataValue("framerate", m_fps);
        m_outStream->setMetadataValue("has_color", true);
        m_outStream->start();

        statusMessage("Connecting camera...");
        if (!m_camera->open(m_camSettingsWindow->resolution())) {
            raiseError(QStringLiteral("Unable to connect camera: %1").arg(m_camera->lastError()));
            return false;
        }
        statusMessage("Waiting...");

        m_camSettingsWindow->setRunning(true);

        return true;
    }

    void start() override
    {
        statusMessage("Acquiring frames...");
        m_evTimer->start();
    }

    void runThread(OptionalWaitCondition *startWaitCondition) override
    {
        auto firstFrame = true;
        time_t startTime = 0;
        auto frameRecordFailedCount = 0;

        // wait until we are actually started
        startWaitCondition->wait(this);

        while (m_running) {
            const auto cycleStartTime = currentTimePoint();

            time_t time;
            auto mat = m_camera->getFrame(&time);
            if (mat.empty()) {
                frameRecordFailedCount++;
                if (frameRecordFailedCount > 32) {
                    m_running = false;
                    raiseError(
                        QStringLiteral("Too many attempts to fetch frames from this camera have failed. Is the camera "
                                       "connected properly?"));
                }
                continue;
            }

            // assume first frame is starting point
            if (firstFrame) {
                firstFrame = false;
                startTime = time;
            }
            auto timestampUsec = msecToUsec(std::chrono::milliseconds(time - startTime));
            m_outStream->push(Frame(mat, timestampUsec));

            // wait a bit if necessary, to keep the right framerate
            const auto cycleTime = timeDiffToNowMsec(cycleStartTime);
            const auto extraWaitTime = std::chrono::milliseconds((1000 / m_fps) - cycleTime.count());
            if (extraWaitTime.count() > 0)
                std::this_thread::sleep_for(extraWaitTime);

            const auto totalTime = timeDiffToNowMsec(cycleStartTime);
            m_currentFps = static_cast<int>(1 / (totalTime.count() / static_cast<double>(1000)));
        }
    }

    void checkCamStatus()
    {
        QString statusText = QStringLiteral("Acquiring frames...");
        // warn if there is a bigger framerate drop
        if (m_currentFps < (m_fps - 2))
            statusText = QStringLiteral("<html><font color=\"red\"><b>Framerate is too low!</b></font>");
        statusMessage(statusText);
    }

    void stop() override
    {
        m_evTimer->stop();
        m_camera->disconnect();
        m_camSettingsWindow->setRunning(false);
        statusMessage("Camera disconnected.");
    }

    void serializeSettings(const QString &confBaseDir, QVariantHash &settings, QByteArray &) override
    {
        QDir cdir(confBaseDir);

        settings.insert("camera", m_camera->camId());
        settings.insert("width", m_camSettingsWindow->resolution().width);
        settings.insert("height", m_camSettingsWindow->resolution().height);
        settings.insert("fps", m_camSettingsWindow->framerate());
        settings.insert("auto_gain", m_camSettingsWindow->automaticGain());
        settings.insert("exposure_time", m_camSettingsWindow->exposure());
        settings.insert("gpio_flash", m_camSettingsWindow->gpioFlash());

        if (!m_camSettingsWindow->uEyeConfigFile().isEmpty())
            settings.insert("ueye_config", cdir.relativeFilePath(m_camSettingsWindow->uEyeConfigFile()));
    }

    bool loadSettings(const QString &confBaseDir, const QVariantHash &settings, const QByteArray &) override
    {
        QDir cdir(confBaseDir);

        m_camSettingsWindow->setCameraId(settings.value("camera").toInt());
        m_camSettingsWindow->setResolution(cv::Size(settings.value("width").toInt(), settings.value("height").toInt()));
        m_camSettingsWindow->setFramerate(settings.value("fps").toInt());
        m_camSettingsWindow->setGpioFlash(settings.value("gpio_flash").toBool());
        m_camSettingsWindow->setAutomaticGain(settings.value("auto_gain").toBool());
        m_camSettingsWindow->setExposure(settings.value("exposure_time").toDouble());

        const auto configFname = settings.value("ueye_config").toString();
        if (!configFname.isEmpty())
            m_camSettingsWindow->setUEyeConfigFile(cdir.absoluteFilePath(configFname));

        return true;
    }
};

QString UEyeCameraModuleInfo::id() const
{
    return QStringLiteral("camera-ueye");
}

QString UEyeCameraModuleInfo::name() const
{
    return QStringLiteral("uEye Camera");
}

QString UEyeCameraModuleInfo::description() const
{
    return QStringLiteral("Capture video with an IDS camera that is compatible with the uEye API.");
}

ModuleCategories UEyeCameraModuleInfo::categories() const
{
    return ModuleCategory::DEVICES;
}

AbstractModule *UEyeCameraModuleInfo::createModule(QObject *parent)
{
    return new UEyeCameraModule(parent);
}

#include "ueyecameramodule.moc"
