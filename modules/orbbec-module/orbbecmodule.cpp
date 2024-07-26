#include <deque>
#include <numeric>
#include "orbbecmodule.h"
#include "datactl/frametype.h"
#include <libobsensor/ObSensor.hpp>
#include <vips/vips8>

SYNTALOS_MODULE(OrbbecModule)

class OrbbecModule : public AbstractModule
{
    Q_OBJECT

public:
    explicit OrbbecModule(QObject *parent = nullptr)
        : AbstractModule(parent),
          m_isRecording(false),
          m_pipelineStarted(false),
          m_frameIndex(0),
          m_stopped(true),
          m_fps(30.0)  // Default FPS, adjust if needed
    {
        m_irOut = registerOutputPort<Frame>(QStringLiteral("ir-out"), QStringLiteral("Infrared Frames"));
        m_depthOut = registerOutputPort<Frame>(QStringLiteral("depth-out"), QStringLiteral("Depth Frames"));
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

            auto irProfile = m_pipeline->getStreamProfileList(OB_SENSOR_IR)->getVideoStreamProfile(640, OB_HEIGHT_ANY, OB_FORMAT_Y16, 30);
            auto depthProfile = m_pipeline->getStreamProfileList(OB_SENSOR_DEPTH)->getVideoStreamProfile(640, OB_HEIGHT_ANY, OB_FORMAT_Y16, 30);

            m_config->enableStream(irProfile);
            m_config->enableStream(depthProfile);

            m_irOut->setMetadataValue("framerate", m_fps);
            m_depthOut->setMetadataValue("framerate", m_fps);

            m_irOut->start();
            m_depthOut->start();

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
        auto frameRecordFailedCount = 0;
        m_stopped = false;

        waitCondition->wait(this);

        try {
            m_pipeline->start(m_config);
            m_pipelineStarted = true;

            if (m_isRecording) {
                m_pipeline->startRecord(m_recordingFilename.toStdString().c_str());
            }

            while (m_running) {
                const auto cycleStartTime = currentTimePoint();

                auto frameSet = m_pipeline->waitForFrames(200);
                if (frameSet == nullptr) {
                    frameRecordFailedCount++;
                    if (frameRecordFailedCount > 32) {
                        m_running = false;
                        raiseError(QStringLiteral("Too many attempts to record frames have failed. Is the camera connected properly?"));
                    }
                    continue;
                }

                processFrame(frameSet->irFrame(), m_irOut);
                processFrame(frameSet->depthFrame(), m_depthOut);

                m_frameIndex++;

                const auto totalTime = timeDiffToNowMsec(cycleStartTime);
                currentFps = static_cast<int>(1 / (totalTime.count() / static_cast<double>(1000)));

                // warn if there is a bigger framerate drop
                if (currentFps < (m_fps - 2)) {
                    fpsLow = true;
                    setStatusMessage(QStringLiteral("<html><font color=\"red\"><b>Framerate (%1fps) is too low!</b></font>")
                                         .arg(currentFps));
                } else if (fpsLow) {
                    fpsLow = false;
                    statusMessage("Acquiring frames...");
                }
            }

            if (m_isRecording) {
                m_pipeline->stopRecord();
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

    void setRecordingFilename(const QString &filename)
    {
        m_recordingFilename = filename;
    }

    void startRecording()
    {
        m_isRecording = true;
        if (m_pipeline && m_pipelineStarted) {
            m_pipeline->startRecord(m_recordingFilename.toStdString().c_str());
        }
    }

    void stopRecording()
    {
        m_isRecording = false;
        if (m_pipeline && m_pipelineStarted) {
            m_pipeline->stopRecord();
        }
    }
private:
    std::shared_ptr<DataStream<Frame>> m_irOut;
    std::shared_ptr<DataStream<Frame>> m_depthOut;
    std::shared_ptr<ob::Pipeline> m_pipeline;
    std::shared_ptr<ob::Config> m_config;
    bool m_isRecording;
    QString m_recordingFilename;
    bool m_pipelineStarted;
    uint64_t m_frameIndex;
    double m_fps;
    std::atomic_bool m_stopped;

    std::unique_ptr<SecondaryClockSynchronizer> m_clockSync;
    microseconds_t m_lastMasterTimestamp;
    microseconds_t m_lastDeviceTimestamp;

    template<typename T>
    void processFrame(const T& frame, std::shared_ptr<DataStream<Frame>>& output)
    {
        if (!frame || frame->dataSize() == 0) {
            qWarning() << "Received invalid frame";
            return;
        }

        int width = frame->width();
        int height = frame->height();
        
        if (width <= 0 || height <= 0) {
            qWarning() << "Invalid frame dimensions:" << width << "x" << height;
            return;
        }

        try {
            vips::VImage image = vips::VImage::new_from_memory(
                frame->data(),
                frame->dataSize(),
                width,
                height,
                1,
                std::is_same<T, ob::IRFrame>::value ? VIPS_FORMAT_UCHAR : VIPS_FORMAT_USHORT
            );

            if (std::is_same<T, ob::DepthFrame>::value) {
                image = processDepthImage(image);
            } else {
                image = image.cast(VIPS_FORMAT_UCHAR, vips::VImage::option()->set("shift", 8));
            }

            microseconds_t masterTimestamp = microseconds_t(m_syTimer->timeSinceStartUsec());
            microseconds_t deviceTimestamp = microseconds_t(frame->timeStamp());
            m_clockSync->processTimestamp(masterTimestamp, deviceTimestamp);

            m_lastMasterTimestamp = masterTimestamp;
            m_lastDeviceTimestamp = deviceTimestamp;

            Frame outFrame(image, m_frameIndex, masterTimestamp);
            output->push(outFrame);
        }
        catch (const vips::VError& e) {
            qWarning() << "VIPS error processing frame:" << e.what();
        }
    }

    vips::VImage processDepthImage(vips::VImage depthImage)
    {
        depthImage = depthImage.cast(VIPS_FORMAT_FLOAT);
        auto median = depthImage.median(0);
        depthImage = median - depthImage;
        depthImage = depthImage.ifthenelse(0, depthImage > 150); // set 150 as threshold
        depthImage = depthImage.ifthenelse(0, depthImage < 0);

        double min = depthImage.min();
        double max = depthImage.max();

        if (max > min) {
            depthImage = (depthImage - min) / (max - min) * 255;
        } else {
            depthImage = depthImage * 0;
        }

        return depthImage.cast(VIPS_FORMAT_UCHAR);
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
    return QStringLiteral("Record depth data with an Orbbec sensor");
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