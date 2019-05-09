/*
 * Copyright (C) 2016-2019 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ueyecameramodule.h"

#include <QMutexLocker>
#include <QMessageBox>
#include <opencv2/opencv.hpp>

#include "ueyecamera.h"
#include "videoviewwidget.h"
#include "ueyecamerasettingsdialog.h"

#include "modules/videorecorder/videowriter.h"

UEyeCameraModule::UEyeCameraModule(QObject *parent)
    : ImageSourceModule(parent),
      m_camera(nullptr),
      m_videoView(nullptr),
      m_camSettingsWindow(nullptr),
      m_thread(nullptr)
{
    m_name = QStringLiteral("uEye Camera");
    m_camera = new UEyeCamera;

    m_frameRing = boost::circular_buffer<FrameData>(32);
}

UEyeCameraModule::~UEyeCameraModule()
{
    finishCaptureThread();
    if (m_videoView != nullptr)
        delete m_videoView;
    if (m_camSettingsWindow != nullptr)
        delete m_camSettingsWindow;
}

QString UEyeCameraModule::id() const
{
    return QStringLiteral("ueye-camera");
}

QString UEyeCameraModule::description() const
{
    return QStringLiteral("Capture video with an IDS camera that is compatible with the uEye API.");
}

QPixmap UEyeCameraModule::pixmap() const
{
    return QPixmap(":/module/ueye-camera");
}

void UEyeCameraModule::setName(const QString &name)
{
    ImageSourceModule::setName(name);
    if (initialized()) {
        m_videoView->setWindowTitle(name);
        m_camSettingsWindow->setWindowTitle(QStringLiteral("Settings for %1").arg(name));
    }
}

void UEyeCameraModule::attachVideoWriter(VideoWriter *vwriter)
{
    m_vwriters.append(vwriter);
}

int UEyeCameraModule::selectedFramerate() const
{
    assert(initialized());
    return static_cast<double>(m_camSettingsWindow->selectedFps());
}

cv::Size UEyeCameraModule::selectedResolution() const
{
    assert(initialized());
    return m_camSettingsWindow->selectedSize();
}

bool UEyeCameraModule::initialize(ModuleManager *manager)
{
    assert(!initialized());
    Q_UNUSED(manager);

    m_videoView = new VideoViewWidget;
    m_camSettingsWindow = new UEyeCameraSettingsDialog(m_camera);

    setState(ModuleState::READY);
    setInitialized();

    // set all window titles
    setName(name());

    return true;
}

bool UEyeCameraModule::prepare(HRTimer *timer)
{
    m_started = false;
    m_timer = timer;

    setState(ModuleState::PREPARING);
    if (!startCaptureThread())
        return false;
    setState(ModuleState::WAITING);
    return true;
}

void UEyeCameraModule::start()
{
    m_started = true;
    statusMessage("Acquiring frames...");
    setState(ModuleState::RUNNING);
}

bool UEyeCameraModule::runCycle()
{
    QMutexLocker locker(&m_mutex);

    if (m_frameRing.size() == 0)
        return true;

    if (!m_frameRing.empty()) {
        auto frameInfo = m_frameRing.front();
        m_videoView->showImage(frameInfo.first);
        m_frameRing.pop_front();

        // send frame away to connected image sinks, and hope they are
        // handling this efficiently and don't block the loop
        emit newFrame(frameInfo);

        // show framerate directly in the window title, to make reduced framerate very visible
        m_videoView->setWindowTitle(QStringLiteral("%1 (%2 fps)").arg(m_name).arg(m_currentFps));
    }

    return true;
}

void UEyeCameraModule::stop()
{
    finishCaptureThread();
}

void UEyeCameraModule::showDisplayUi()
{
    assert(initialized());
    m_videoView->show();
}

void UEyeCameraModule::hideDisplayUi()
{
    assert(initialized());
    m_videoView->hide();
}

void UEyeCameraModule::showSettingsUi()
{
    assert(initialized());
    m_camSettingsWindow->show();
}

void UEyeCameraModule::hideSettingsUi()
{
    assert(initialized());
    m_camSettingsWindow->hide();
}

void UEyeCameraModule::captureThread(void *gcamPtr)
{
    UEyeCameraModule *self = static_cast<UEyeCameraModule*> (gcamPtr);

    self->m_currentFps = self->m_fps;
    auto firstFrame = true;
    time_t startTime = 0;

    while (self->m_running) {
        const auto cycleStartTime = currentTimePoint();

        // wait until we actually start
        while (!self->m_started) { }

        cv::Mat frame;
        time_t time;
        if (!self->m_camera->getFrame(&frame, &time)) {
            continue;
        }

        // assume first frame is starting point
        if (firstFrame) {
            firstFrame = false;
            startTime = time;
        }
        auto timestampMsec = std::chrono::milliseconds(time - startTime);

        // record this frame, if we have any video writers registered
        Q_FOREACH(auto vwriter, self->m_vwriters)
            vwriter->pushFrame(frame, timestampMsec);

        self->m_mutex.lock();
        self->m_frameRing.push_back(std::pair<cv::Mat, std::chrono::milliseconds>(frame, timestampMsec));
        self->m_mutex.unlock();

        // wait a bit if necessary, to keep the right framerate
        const auto cycleTime = timeDiffToNowMsec(cycleStartTime);
        const auto extraWaitTime = std::chrono::milliseconds((1000 / self->m_fps) - cycleTime.count());
        if (extraWaitTime.count() > 0)
            std::this_thread::sleep_for(extraWaitTime);

        const auto totalTime = timeDiffToNowMsec(cycleStartTime);
        self->m_currentFps = static_cast<int>(1 / (totalTime.count() / static_cast<double>(1000)));
    }
}

bool UEyeCameraModule::startCaptureThread()
{
    finishCaptureThread();

    statusMessage("Connecting camera...");
    if (!m_camera->open(m_camSettingsWindow->selectedSize())) {
        raiseError(QStringLiteral("Unable to connect camera: %1").arg(m_camera->lastError()));
        return false;
    }
    statusMessage("Launching DAQ thread...");

    m_camSettingsWindow->setRunning(true);
    m_fps = m_camSettingsWindow->selectedFps();
    m_running = true;
    m_thread = new std::thread(captureThread, this);
    statusMessage("Waiting.");
    return true;
}

void UEyeCameraModule::finishCaptureThread()
{
    if (!initialized())
        return;

    // ensure we unregister all video writers before starting another run,
    // and after finishing the current one, as the modules they belong to
    // may meanwhile have been removed
    m_vwriters.clear();

    statusMessage("Cleaning up...");
    if (m_thread != nullptr) {
        m_running = false;
        m_thread->join();
        delete m_thread;
        m_thread = nullptr;
    }
    m_camera->disconnect();
    m_camSettingsWindow->setRunning(false);
    statusMessage("Camera disconnected.");
}