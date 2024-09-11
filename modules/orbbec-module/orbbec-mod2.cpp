// very inefficient and very dirty potential implementation
// i think the solution is to fix the video recording so its getting saved in the proper format
// bc saving the depth videos as 8 bit integers is a problem

#include <deque>
#include <numeric>
#include "orbbecmodule.h"
#include "datactl/frametype.h"
#include <libobsensor/ObSensor.hpp>
#include <opencv2/opencv.hpp>
#include <QProcess>
#include <QDir>
#include <QDateTime>

SYNTALOS_MODULE(OrbbecModule)

QString setupFFmpegCommand(const QString& filename, int width, int height, int fps)
{
    return QString("ffmpeg -y -loglevel fatal -framerate %1 -f rawvideo -s %2x%3 "
                   "-pix_fmt gray16 -i - -an -crf 10 -vcodec ffv1 -preset ultrafast "
                   "-threads 6 -slices 24 -slicecrc 1 -r %1 %4")
            .arg(fps).arg(width).arg(height).arg(filename);
}

class OrbbecModule : public AbstractModule
{
    Q_OBJECT

private:
    std::shared_ptr<DataStream<Frame>> m_depthRawOut;
    std::shared_ptr<DataStream<Frame>> m_depthDispOut;

    std::shared_ptr<ob::Pipeline> m_pipeline;
    std::shared_ptr<ob::Config> m_config;
    bool m_pipelineStarted;
    uint64_t m_frameIndex;
    double m_fps;
    std::atomic_bool m_stopped;

    std::unique_ptr<SecondaryClockSynchronizer> m_clockSync;
    microseconds_t m_lastMasterTimestamp;
    microseconds_t m_lastDeviceTimestamp;

    QProcess m_ffmpegProcess;
    QString m_outputFile;

public:
    explicit OrbbecModule(QObject *parent = nullptr)
        : AbstractModule(parent),
          m_pipelineStarted(false),
          m_frameIndex(0),
          m_fps(30.0),
          m_stopped(true)
    {
        m_depthRawOut = registerOutputPort<Frame>(QStringLiteral("depth-raw-out"), QStringLiteral("Raw Depth Frames"));
        m_depthDispOut = registerOutputPort<Frame>(QStringLiteral("depth-disp-out"), QStringLiteral("Display Depth Frames"));
    }

    ~OrbbecModule() override 
    {
        if (m_pipeline && m_pipelineStarted) {
            m_pipeline->stop();
        }
        if (m_ffmpegProcess.state() == QProcess::Running) {
            m_ffmpegProcess.closeWriteChannel();
            m_ffmpegProcess.waitForFinished();
        }
    }

    ModuleFeatures features() const override
    {
        return ModuleFeature::SHOW_SETTINGS;
    }

    ModuleDriverKind driver() const override
    {
        return ModuleDriverKind::THREAD_DEDICATED;
    }

    bool prepare(const TestSubject &) override
    {
        try {
            m_pipeline = std::make_shared<ob::Pipeline>();
            m_config = std::make_shared<ob::Config>();

            auto depthProfile = m_pipeline->getStreamProfileList(OB_SENSOR_DEPTH)->getVideoStreamProfile(640, OB_HEIGHT_ANY, OB_FORMAT_Y16, 30);
            m_config->enableStream(depthProfile);

            m_depthRawOut->setMetadataValue("framerate", m_fps);
            m_depthRawOut->setMetadataValue("has_color", false);
            m_depthRawOut->setMetadataValue("depth", CV_16U);
            m_depthRawOut->setSuggestedDataName(QStringLiteral("%1/depth_raw").arg(datasetNameSuggestion()));

            m_depthDispOut->setMetadataValue("framerate", m_fps);
            m_depthDispOut->setMetadataValue("has_color", true);
            m_depthDispOut->setMetadataValue("depth", CV_8U);
            m_depthDispOut->setSuggestedDataName(QStringLiteral("%1/depth_display").arg(datasetNameSuggestion()));

            m_depthRawOut->start();
            m_depthDispOut->start();

            // Set up FFmpeg process for video writing
            QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
            QString homeDir = QDir::homePath();
            QString outputDir = QDir(homeDir).filePath("llorentelab/depth_videos");
            QDir().mkpath(outputDir);
            m_outputFile = QDir(outputDir).filePath(QString("depth_%1.avi").arg(timestamp));
            
            QString ffmpegCommand = setupFFmpegCommand(m_outputFile, depthProfile->width(), depthProfile->height(), m_fps);
            m_ffmpegProcess.setProgram("sh");
            m_ffmpegProcess.setArguments(QStringList() << "-c" << ffmpegCommand);
            m_ffmpegProcess.start();

            if (!m_ffmpegProcess.waitForStarted()) {
                raiseError("Failed to start FFmpeg process");
                return false;
            }

        } catch (const ob::Error& e) {
            raiseError(QStringLiteral("Orbbec initialization error: %1").arg(e.getMessage()));
            return false;
        }

        // set up clock synchronizer
        m_clockSync = initClockSynchronizer(m_fps);
        m_clockSync->setStrategies(TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD);

        m_lastMasterTimestamp = microseconds_t(0);
        m_lastDeviceTimestamp = microseconds_t(0);
        // start the synchronizer
        if (!m_clockSync->start()) {
            raiseError(QStringLiteral("Unable to set up clock synchronizer!"));
            return false;
        }

        return true;
    }

    void start() override
    {
        statusMessage("Acquiring frames...");
        AbstractModule::start();
    }

    void runThread(OptionalWaitCondition *waitCondition) override
    {
        auto fpsLow = false;
        auto currentFps = m_fps;
        auto frameProcessFailedCount = 0;
        m_stopped = false;

        waitCondition->wait(this);

        try {
            m_pipeline->start(m_config);
            m_pipelineStarted = true;

            while (m_running) {
                const auto cycleStartTime = currentTimePoint();

                auto frameSet = m_pipeline->waitForFrames(200);
                
                if (frameSet == nullptr) {
                    frameProcessFailedCount++;
                    std::cout << "Dropped frame. Frame process failed count is now " << frameProcessFailedCount << std::endl;
                    if (frameProcessFailedCount > 50) {
                        raiseError(QStringLiteral("Too many attempts to process frames have failed. Is the camera connected properly?"));
                        m_running = false;
                        m_pipeline->stop();
                    }
                    continue;
                }

                auto depthFrame = frameSet->depthFrame();
                if (depthFrame) {
                    processDepthFrame(depthFrame);
                }

                const auto totalTime = timeDiffToNowMsec(cycleStartTime);
                currentFps = static_cast<int>(1 / (totalTime.count() / static_cast<double>(1000)));

                // warn if there is a bigger framerate drop
                if (currentFps < (m_fps - 10)) {
                    fpsLow = true;
                    setStatusMessage(QStringLiteral("<html><font color=\"red\"><b>Framerate (%1fps) is too low!</b></font>")
                                         .arg(currentFps));
                } else if (fpsLow) {
                    fpsLow = false;
                    statusMessage("Acquiring frames...");
                }
            }

            m_pipeline->stop();
            m_pipelineStarted = false;

        } catch (const ob::Error& e) {
            raiseError(QStringLiteral("Orbbec runtime error: %1").arg(e.getMessage()));
        }

        m_stopped = true;
    }

    void stop() override
    {
        statusMessage("Cleaning up...");
        AbstractModule::stop();

        while (!m_stopped) {
        }

        if (m_pipeline && m_pipelineStarted) {
            m_pipeline->stop();
            m_pipelineStarted = false;
        }

        // Close FFmpeg process
        if (m_ffmpegProcess.state() == QProcess::Running) {
            m_ffmpegProcess.closeWriteChannel();
            m_ffmpegProcess.waitForFinished();
        }

        safeStopSynchronizer(m_clockSync);
        statusMessage(QStringLiteral("Camera disconnected. Video saved to: %1").arg(m_outputFile));
    }

private:
    void processDepthFrame(std::shared_ptr<ob::DepthFrame> depthFrame)
    {
        if (!depthFrame || depthFrame->dataSize() == 0) {
            qWarning() << "Received invalid depth frame";
            return;
        }

        int width = depthFrame->width();
        int height = depthFrame->height();
        float scale = depthFrame->getValueScale();

        // Create raw depth data (16-bit)
        cv::Mat rawDepth(height, width, CV_16UC1, (void*)depthFrame->data());

        // Create display frame (8-bit, color mapped)
        cv::Mat scaledDepth;
        rawDepth.convertTo(scaledDepth, CV_32F, scale);
        cv::Mat displayDepth;
        scaledDepth.convertTo(displayDepth, CV_8U, 255.0 / 5000);  // Assuming max depth of 5000mm
        cv::applyColorMap(displayDepth, displayDepth, cv::COLORMAP_JET);

        microseconds_t masterTimestamp = microseconds_t(m_syTimer->timeSinceStartUsec());
        microseconds_t deviceTimestamp = microseconds_t(depthFrame->timeStamp());
        m_clockSync->processTimestamp(masterTimestamp, deviceTimestamp);

        m_lastMasterTimestamp = masterTimestamp;
        m_lastDeviceTimestamp = deviceTimestamp;

        // Push raw frame (16-bit depth)
        Frame rawFrame(rawDepth, m_frameIndex, masterTimestamp);
        m_depthRawOut->push(rawFrame);

        // Push display frame (8-bit color mapped)
        Frame dispFrame(displayDepth, m_frameIndex, masterTimestamp);
        m_depthDispOut->push(dispFrame);

        // Write raw frame to FFmpeg process
        if (m_ffmpegProcess.state() == QProcess::Running) {
            m_ffmpegProcess.write(reinterpret_cast<const char*>(rawDepth.data), rawDepth.total() * rawDepth.elemSize());
        }

        // Print center pixel distance every 30 frames
        if (m_frameIndex % 30 == 0) {
            uint16_t* data = (uint16_t*)depthFrame->data();
            float centerDistance = data[width * height / 2 + width / 2] * scale;
            std::cout << "Facing an object " << centerDistance << " mm away." << std::endl;
        }

        m_frameIndex++;
    }
};

QString OrbbecModuleInfo::id() const
{
    return QStringLiteral("orbbec-cam");
}

QString OrbbecModuleInfo::name() const
{
    return QStringLiteral("Orbbec Depth Sensor");
}

QString OrbbecModuleInfo::description() const
{
    return QStringLiteral("Process depth data with an Orbbec sensor");
}

ModuleCategories OrbbecModuleInfo::categories() const
{
    return ModuleCategory::DEVICES;
}

AbstractModule *OrbbecModuleInfo::createModule(QObject *parent)
{
    return new OrbbecModule(parent);
}

#include "orbbecmodule.moc"
