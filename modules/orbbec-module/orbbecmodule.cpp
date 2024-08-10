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

    void processDepthFrame(std::shared_ptr<ob::DepthFrame> depthFrame, std::shared_ptr<DataStream<Frame>>& output, microseconds_t frameRecvTime)
    {
        if (!depthFrame || depthFrame->dataSize() == 0) {
            qWarning() << "Dropped frame";
            return;
        }

        int width = depthFrame->width();
        int height = depthFrame->height();
        float scale = depthFrame->getValueScale();
        
        cv::Mat depthMat(height, width, CV_16UC1, (void*)depthFrame->data());
        cv::Mat processedDepth = processDepthMat(depthMat, scale);

        microseconds_t masterTimestamp = microseconds_t(m_syTimer->timeSinceStartUsec());
        microseconds_t deviceTimestamp = microseconds_t(depthFrame->timeStamp());
        m_clockSync->processTimestamp(frameRecvTime, deviceTimestamp);

        m_lastMasterTimestamp = masterTimestamp;
        m_lastDeviceTimestamp = deviceTimestamp;

        Frame outFrame(processedDepth, m_frameIndex, frameRecvTime);
        output->push(outFrame);

        m_frameIndex++;
    }

    void processIRFrame(std::shared_ptr<ob::IRFrame> irFrame, std::shared_ptr<DataStream<Frame>>& output, microseconds_t frameRecvTime)
    {
        if (!irFrame || irFrame->dataSize() == 0) {
            qWarning() << "Received invalid IR frame";
            return;
        }

        int width = irFrame->width();
        int height = irFrame->height();
        
        cv::Mat irMat(height, width, CV_16UC1, (void*)irFrame->data());
        cv::Mat processedIR = processIRMat(irMat);

        microseconds_t masterTimestamp = microseconds_t(m_syTimer->timeSinceStartUsec());
        microseconds_t deviceTimestamp = microseconds_t(irFrame->timeStamp());
        m_clockSync->processTimestamp(frameRecvTime, deviceTimestamp);

        Frame outFrame(processedIR, m_frameIndex, frameRecvTime);
        output->push(outFrame);
    }

    cv::Mat processDepthMat(const cv::Mat& depthMat, float scale)
    {
        cv::Mat depthVis;
        depthMat.convertTo(depthVis, CV_8UC1, 255.0 / 5000);  // Assuming max depth of 5000mm

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

public:
    explicit OrbbecModule(QObject *parent = nullptr)
        : AbstractModule(parent),
          m_pipelineStarted(false),
          m_frameIndex(0),
          m_fps(30.0),  
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
        safeStopSynchronizer(m_clockSync);
    }

    ModuleFeatures features() const override
    {
        return ModuleFeature::REALTIME | ModuleFeature::SHOW_SETTINGS;
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

        m_clockSync = initClockSynchronizer(m_fps);
        m_clockSync->setStrategies(TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD);

        m_lastMasterTimestamp = microseconds_t(0);
        m_lastDeviceTimestamp = microseconds_t(0);
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
        auto frameRecordFailedCount = 0;
        m_stopped = false;

        waitCondition->wait(this);

        try {
            m_pipeline->start(m_config);
            m_pipelineStarted = true;

            while (m_running) {
                const auto cycleStartTime = currentTimePoint();
                std::shared_ptr<ob::FrameSet> frameSet = nullptr;
                auto frameRecvTime = MTIMER_FUNC_TIMESTAMP(frameSet = m_pipeline->waitForFrames(200));

                if (frameSet == nullptr) {
                    frameRecordFailedCount++;
                    if (frameRecordFailedCount > 32) {
                        m_running = false;
                        raiseError(QStringLiteral("Too many attempts to record frames have failed. Is the camera connected properly?"));
                    }
                    continue;
                }

                auto depthFrame = frameSet->depthFrame();
                if (depthFrame) {
                    processDepthFrame(depthFrame, m_depthOut, frameRecvTime);
                }

                auto irFrame = frameSet->irFrame();
                if (irFrame) {
                    processIRFrame(irFrame, m_irOut, frameRecvTime);
                }

                const auto totalTime = timeDiffToNowMsec(cycleStartTime);
                currentFps = static_cast<int>(1 / (totalTime.count() / static_cast<double>(1000)));

                if (currentFps < (m_fps - 2)) {
                    fpsLow = true;
                    setStatusMessage(QStringLiteral("<html><font color=\"red\"><b>Framerate (%1fps) is too low!</b></font>")
                                         .arg(currentFps));
                } else if (fpsLow) {
                    fpsLow = false;
                    statusMessage("Acquiring frames...");
                }
            }
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
};

class OrbbecModuleInfo : public AbstractModuleInfo
{
public:
    QString id() const override
    {
        return QStringLiteral("orbbec-cam");
    }

    QString name() const override
    {
        return QStringLiteral("Orbbec Depth Sensor");
    }

    QString description() const override
    {
        return QStringLiteral("Capture depth and IR data with an Orbbec depth sensor");
    }

    ModuleCategories categories() const override
    {
        return ModuleCategory::DEVICES;
    }

    AbstractModule *createModule(QObject *parent) override
    {
        return new OrbbecModule(parent);
    }
};

#include "orbbecmodule.moc"
