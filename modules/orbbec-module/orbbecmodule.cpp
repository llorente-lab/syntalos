#include <deque>
#include <numeric>
#include "orbbecmodule.h"
#include "datactl/frametype.h"
#include <libobsensor/ObSensor.hpp>
#include <opencv2/opencv.hpp>

SYNTALOS_MODULE(OrbbecModule)

class OrbbecModule : public AbstractModule
{
    Q_OBJECT

public:
    explicit OrbbecModule(QObject *parent = nullptr)
        : AbstractModule(parent),
          m_pipelineStarted(false),
          m_frameIndex(0),
          m_fps(30.0),  // Default FPS, adjust if needed
          m_stopped(true)
    {
        m_depthOut = registerOutputPort<Frame>(QStringLiteral("depth-out"), QStringLiteral("Depth Frames"));
        m_irOut = registerOutputPort<Frame>(QStringLiteral("ir-out"), QStringLiteral("IR Frames"));
    }

    ~OrbbecModule() override 
    {
        if (m_pipeline && m_pipelineStarted) {
            m_pipeline->stop();
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
            auto irProfile = m_pipeline->getStreamProfileList(OB_SENSOR_IR)->getVideoStreamProfile(640, OB_HEIGHT_ANY, OB_FORMAT_Y16, 30);

            m_config->enableStream(depthProfile);
            m_config->enableStream(irProfile);

            m_depthOut->setMetadataValue("framerate", m_fps);
            m_irOut->setMetadataValue("framerate", m_fps);

            m_depthOut->start();
            m_irOut->start();

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
                    processDepthFrame(depthFrame, m_depthOut);
                }

                auto irFrame = frameSet->irFrame();
                if (irFrame) {
                    processIRFrame(irFrame, m_irOut);
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
        safeStopSynchronizer(m_clockSync);
        statusMessage("Camera disconnected.");
    }

private:
    std::shared_ptr<DataStream<Frame>> m_depthOut;
    std::shared_ptr<DataStream<Frame>> m_irOut;
    std::shared_ptr<ob::Pipeline> m_pipeline;
    std::shared_ptr<ob::Config> m_config;
    bool m_pipelineStarted;
    uint64_t m_frameIndex;
    double m_fps;
    std::atomic_bool m_stopped;

    std::unique_ptr<SecondaryClockSynchronizer> m_clockSync;
    microseconds_t m_lastMasterTimestamp;
    microseconds_t m_lastDeviceTimestamp;

    void processDepthFrame(std::shared_ptr<ob::DepthFrame> depthFrame, std::shared_ptr<DataStream<Frame>>& output)
    {
        if (!depthFrame || depthFrame->dataSize() == 0) {
            qWarning() << "Received invalid depth frame";
            return;
        }

        float scale = depthFrame->getValueScale();
    
        // Convert depth data to cv::Mat
        int width = depthFrame->width();
        int height = depthFrame->height();
        cv::Mat depthMat(height, width, CV_16UC1, (void*)depthFrame->data());
    
        // Apply scaling
        cv::Mat scaledDepthMat;
        depthMat.convertTo(scaledDepthMat, CV_32F);
        scaledDepthMat *= scale;
        scaledDepthMat.convertTo(scaledDepthMat, CV_16UC1);

        // Create Frame object and push to output
        microseconds_t timestamp = microseconds_t(depthFrame->timeStamp());
    
        microseconds_t masterTimestamp = microseconds_t(m_syTimer->timeSinceStartUsec());
        m_clockSync->processTimestamp(masterTimestamp, timestamp);

        m_lastMasterTimestamp = masterTimestamp;
        m_lastDeviceTimestamp = timestamp;

        Frame outFrame(scaledDepthMat, m_frameIndex, masterTimestamp);
        output->push(outFrame);

        m_frameIndex++;
    }

    void processIRFrame(std::shared_ptr<ob::IRFrame> irFrame, std::shared_ptr<DataStream<Frame>>& output)
    {
        if (!irFrame || irFrame->dataSize() == 0) {
            qWarning() << "Received invalid IR frame";
            return;
        }

        int width = irFrame->width();
        int height = irFrame->height();

        cv::Mat irMat(height, width, CV_8UC1, (void*)irFrame->data());
        cv::Mat processedIR = processIRMat(irMat);

        microseconds_t masterTimestamp = microseconds_t(m_syTimer->timeSinceStartUsec());
        microseconds_t deviceTimestamp = microseconds_t(irFrame->timeStamp());
        m_clockSync->processTimestamp(masterTimestamp, deviceTimestamp);

        Frame outFrame(processedIR, m_frameIndex, masterTimestamp);
        output->push(outFrame);
    }

    cv::Mat processDepthMat(const cv::Mat& depthMat, float scale)
    {
        // Convert to 8-bit for visualization
        cv::Mat depthVis;
        depthMat.convertTo(depthVis, CV_8UC1, scale);  // Assuming max depth of 5000mm

        // Apply color map for better visualization
        cv::Mat colorMappedDepth;
        cv::applyColorMap(depthVis, colorMappedDepth, cv::COLORMAP_JET);

        return colorMappedDepth;
    }

    cv::Mat processIRMat(const cv::Mat& irMat)
    {
        cv::Mat irVis;
        irMat.convertTo(irVis, CV_8UC1, 1.0 / 256.0);  // Convert 16-bit to 8-bit

        cv::Mat colorMappedIR;
        cv::applyColorMap(irVis, colorMappedIR, cv::COLORMAP_HOT);  // Apply a color map for visualization

        return colorMappedIR;
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