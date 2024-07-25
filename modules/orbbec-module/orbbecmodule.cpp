#include "orbbecmodule.h"
#include "datactl/frametype.h"
#include <libobsensor/ObSensor.hpp>
#include <vips/vips8>
#include <opencv2/opencv.hpp>

using namespace cv;

SYNTALOS_MODULE(OrbbecModule)

class OrbbecModule : public AbstractModule
{
    Q_OBJECT

private:
    std::shared_ptr<DataStream<Frame>> m_irOut;
    std::shared_ptr<DataStream<Frame>> m_depthOut;
    std::shared_ptr<ob::Pipeline> m_pipeline;
    std::shared_ptr<ob::Config> m_config;
    bool m_isRecording;
    QString m_recordingFilename;
    bool m_pipelineStarted;
    uint64_t m_frameIndex;

    std::unique_ptr<SecondaryClockSynchronizer> m_clockSync;
    microseconds_t m_lastValidTimestamp;

public:
    explicit OrbbecModule(QObject *parent = nullptr)
        : AbstractModule(parent),
          m_isRecording(false),
          m_pipelineStarted(false),
          m_frameIndex(0),
          m_lastValidTimestamp(0)
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

            m_irOut->setMetadataValue("framerate", 30);
            m_depthOut->setMetadataValue("framerate", 30);

            m_irOut->start();
            m_depthOut->start();

        } catch (const ob::Error& e) {
            raiseError(QStringLiteral("Orbbec initialization error: %1").arg(e.getMessage()));
            return false;
        }

        m_clockSync = initClockSynchronizer(30);
        m_clockSync->setStrategies(TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD | TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD);

        if (!m_clockSync->start()) {
            raiseError(QStringLiteral("Unable to set up a synchronizer!"));
            return false;
        }

        return true;
    }

    void runThread(OptionalWaitCondition *startWaitCondition) override
    {
        startWaitCondition->wait(this);

        try {
            m_pipeline->start(m_config);
            m_pipelineStarted = true;

            if (m_isRecording) {
                m_pipeline->startRecord(m_recordingFilename.toStdString().c_str());
            }

            while (m_running) {
                auto frameSet = m_pipeline->waitForFrames(200);
                if (frameSet == nullptr) {
                    qWarning() << "Dropped frame";
                    continue;
                }

                processFrame(frameSet->irFrame(), m_irOut);
                processFrame(frameSet->depthFrame(), m_depthOut);

                m_frameIndex++;
            }

            if (m_isRecording) {
                m_pipeline->stopRecord();
            }

            m_pipeline->stop();
            m_pipelineStarted = false;

        } catch (const ob::Error& e) {
            raiseError(QStringLiteral("Orbbec runtime error: %1").arg(e.getMessage()));
        }
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
        safeStopSynchronizer(m_clockSync);
    }

private:
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

        auto deviceTimestamp = microseconds_t(frame->timeStamp());
        auto masterTimestamp = microseconds_t(m_syTimer->timeSinceStartUsec());

        // Handle backwards-moving timestamps
        if (deviceTimestamp < m_lastValidTimestamp) {
            qWarning() << "Timestamp moved backwards. Using last valid timestamp.";
            deviceTimestamp = m_lastValidTimestamp;
        } else {
            m_lastValidTimestamp = deviceTimestamp;
        }

        m_clockSync->processTimestamp(masterTimestamp, deviceTimestamp);

        try {
            vips::VImage image = vips::VImage::new_from_memory(
                frame->data(),
                frame->dataSize(),
                width,
                height,
                1,
                std::is_same<T, ob::IRFrame>::value ? VIPS_FORMAT_UCHAR : VIPS_FORMAT_USHORT
            );

            image = image.cast(VIPS_FORMAT_UCHAR, vips::VImage::option()->set("shift", 8));

            Frame outFrame(image, m_frameIndex, masterTimestamp);
            output->push(outFrame);
        }
        catch (const vips::VError& e) {
            qWarning() << "VIPS error processing frame:" << e.what();
        }
    }
};

QString OrbbecModuleInfo::id() const
{
    return QStringLiteral("Orbbec Camera");
}

QString OrbbecModuleInfo::name() const
{
    return QStringLiteral("Orbbec Depth Sensor");
}

QString OrbbecModuleInfo::description() const
{
    return QStringLiteral("Record data with an Orbbec Femto Bolt");
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