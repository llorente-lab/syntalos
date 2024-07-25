#include "orbbecmodule.h"
#include "datactl/frametype.h"
#include <libobsensor/ObSensor.hpp>
#include <vips/vips8>

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

public:
    explicit OrbbecModule(QObject *parent = nullptr)
        : AbstractModule(parent),
          m_isRecording(false),
          m_pipelineStarted(false),
          m_frameIndex(0)
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
                    continue;
                }

                auto irFrame = frameSet->irFrame();
                if (irFrame) {
                    size_t irSize = irFrame->dataSize();
                    vips::VImage irImage = vips::VImage::new_from_memory(
                        irFrame->data(),
                        irSize,
                        irFrame->width(),
                        irFrame->height(),
                        1,  // Assuming RGB
                        VIPS_FORMAT_UCHAR
                    );

                    irImage = irImage.cast(VIPS_FORMAT_UCHAR, vips::VImage::option()->set("shift", 8));
                    
                    Frame frame(irImage, m_frameIndex, microseconds_t(irFrame->timeStamp()));
                    m_irOut->push(frame);
                }

                auto depthFrame = frameSet->depthFrame();
                if (depthFrame) {
                    size_t depthSize = depthFrame->dataSize();
                    vips::VImage depthImage = vips::VImage::new_from_memory(
                        depthFrame->data(),
                        depthSize,
                        depthFrame->width(),
                        depthFrame->height(),
                        1,  // Depth is typically single-channel
                        VIPS_FORMAT_USHORT  // Assuming 16-bit depth
                    );
                    
                    Frame frame(depthImage, m_frameIndex, microseconds_t(depthFrame->timeStamp()));
                    m_depthOut->push(frame);
                }

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
    }

private:
};

QString OrbbecModuleInfo::id() const
{
    return QStringLiteral("orbbec-cam");
}

QString OrbbecModuleInfo::name() const
{
    return QStringLiteral("Orbbec Cam Example");
}

QString OrbbecModuleInfo::description() const
{
    return QStringLiteral("Orbbec depth sensor");
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
