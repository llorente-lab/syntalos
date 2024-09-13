#include <deque>
#include <numeric>
#include "orbbecmodule.h"
#include "datactl/frametype.h"
#include <libobsensor/ObSensor.hpp>
#include <opencv2/opencv.hpp>
#include <QProcess>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDialog>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QPushButton>

SYNTALOS_MODULE(OrbbecModule)

// Very barebones settings dialogue. I can update it later with more info but for right now this works
// Basically: just two checkboxes -> start IR stream or start Depth Stream. By default, depth stream is set to true and IR stream is set to false
class OrbbecSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OrbbecSettingsDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Orbbec Stream Settings");

        QVBoxLayout *layout = new QVBoxLayout(this);

        m_depthStreamCheckBox = new QCheckBox("Enable Depth Stream", this);
        m_irStreamCheckBox = new QCheckBox("Enable IR Stream", this);

        m_depthStreamCheckBox->setChecked(true);  // Default: Depth Stream enabled
        m_irStreamCheckBox->setChecked(false);    // Default: IR Stream disabled

        layout->addWidget(m_depthStreamCheckBox);
        layout->addWidget(m_irStreamCheckBox);

        QPushButton *okButton = new QPushButton("OK", this);
        connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
        layout->addWidget(okButton);

        setLayout(layout);
    }

    bool isDepthStreamEnabled() const { return m_depthStreamCheckBox->isChecked(); }
    bool isIRStreamEnabled() const { return m_irStreamCheckBox->isChecked(); }

    void setDepthStreamEnabled(bool enabled) { m_depthStreamCheckBox->setChecked(enabled); }
    void setIRStreamEnabled(bool enabled) { m_irStreamCheckBox->setChecked(enabled); }

private:
    QCheckBox *m_depthStreamCheckBox;
    QCheckBox *m_irStreamCheckBox;
};

class OrbbecModule : public AbstractModule
{
    Q_OBJECT

private:
    std::shared_ptr<DataStream<Frame>> m_depthRawOut;
    // MoSeq expects raw depth videos encoded in FFV1 and with a Gray16LE or Gray16BE pixel format
    // So we need to separate the raw depth pixels and display depth pixels by color
    // The basic idea here is to take the Orbbec frame, convert it to a OpenCV matrix, and push it directly to output
    std::shared_ptr<DataStream<Frame>> m_depthDispOut;
    // This is the processed depth frame (for visualization)
    std::shared_ptr<DataStream<Frame>> m_irOut;
    // IR Stream
    // I don't think we really need to separate this by raw vs display since we don't need infrared videos for MoSeq analysis
    // However, if its more computationally efficient TO separate them, this might make more sense

    std::shared_ptr<ob::Pipeline> m_pipeline;
    // OB Pipeline object
    // This basically starts the stream process
    // read Orbbec docs for more
    std::shared_ptr<ob::Config> m_config;
    // Configuration for the pipeline
    // We could use a context but it might be overkill here
    bool m_pipelineStarted; // internal
    uint64_t m_frameIndex; // current frame index
    double m_fps; // fps
    std::atomic_bool m_stopped; // internal

    std::unique_ptr<SecondaryClockSynchronizer> m_clockSync; // clock synchronizer. we let syntalos handle this
    microseconds_t m_lastMasterTimestamp; // master timestamp (from the system)
    microseconds_t m_lastDeviceTimestamp; // device timestamp (from the sensor). the idea is to synchronize different cameras based on the system timestamp

    QString m_metadataFilePath;
    //metadata goes here
    // moseq structure is like this:
    // - base_dir
    //   - depth.avi
    //   - ir.avi
    //   - metadata.json
    //   - timestamps file
    QJsonObject m_metadataDict;

    OrbbecSettingsDialog *m_settingsDialog;
    // booleans to track which streams to enable
    bool m_depthStreamEnabled; 
    bool m_irStreamEnabled;

public:
    explicit OrbbecModule(ModuleInfo *modInfo, QObject *parent = nullptr)
        : AbstractModule(parent),
          m_pipelineStarted(false),
          m_frameIndex(0),
          m_fps(30.0),
          m_stopped(true),
          m_depthStreamEnabled(true),
          m_irStreamEnabled(false)
    {
        m_depthRawOut = registerOutputPort<Frame>(QStringLiteral("depth-raw-out"), QStringLiteral("Raw Depth Frames"));
        m_depthDispOut = registerOutputPort<Frame>(QStringLiteral("depth-disp-out"), QStringLiteral("Display Depth Frames"));
        m_irOut = registerOutputPort<Frame>(QStringLiteral("ir-out"), QStringLiteral("IR Frames"));

        m_settingsDialog = new OrbbecSettingsDialog();
        m_settingsDialog->setWindowIcon(modInfo->icon());
        addSettingsWindow(m_settingsDialog);
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

    bool prepare(const TestSubject &subject) override
    // "Prepare" the recording (what metadata do we set? what port metadata is there? etc.)
    // No actual recording here, just getting ready for it
    {
        try {
            m_pipeline = std::make_shared<ob::Pipeline>();
            m_config = std::make_shared<ob::Config>();

            m_depthStreamEnabled = m_settingsDialog->isDepthStreamEnabled(); // T or F
            m_irStreamEnabled = m_settingsDialog->isIRStreamEnabled(); // T or F

            if (m_depthStreamEnabled) { // if depth stream is enabled
                auto depthProfile = m_pipeline->getStreamProfileList(OB_SENSOR_DEPTH)->getVideoStreamProfile(640, OB_HEIGHT_ANY, OB_FORMAT_Y16, 30);
                // query device for depth profiles and select the one we want. i've hardcoded this for simplicity's sake
                m_config->enableStream(depthProfile);

                // set port metadata
                m_depthRawOut->setMetadataValue("framerate", m_fps);
                m_depthRawOut->setMetadataValue("has_color", false);
                m_depthRawOut->setMetadataValue("depth", CV_16U);
                m_depthRawOut->setSuggestedDataName(QStringLiteral("%1/depth").arg(datasetNameSuggestion()));

                m_depthDispOut->setMetadataValue("framerate", m_fps);
                m_depthDispOut->setMetadataValue("has_color", true);
                m_depthDispOut->setMetadataValue("depth", CV_8U);
                m_depthDispOut->setSuggestedDataName(QStringLiteral("%1/depth_display").arg(datasetNameSuggestion()));
                // We don't REALLY need this since there's no point in saving the display video (unless you wanna look at it), but why not

                m_depthRawOut->start(); // start the port
                m_depthDispOut->start(); // start the port
            }

            if (m_irStreamEnabled) { // if ir stream is enabled
                auto irProfile = m_pipeline->getStreamProfileList(OB_SENSOR_IR)->getVideoStreamProfile(640, OB_HEIGHT_ANY, OB_FORMAT_Y16, 30);
                m_config->enableStream(irProfile);

                m_irOut->setMetadataValue("framerate", m_fps);
                m_irOut->setMetadataValue("has_color", false);
                m_irOut->setMetadataValue("depth", CV_16U);
                m_irOut->setSuggestedDataName(QStringLiteral("%1/ir").arg(datasetNameSuggestion()));

                m_irOut->start();
            }

            // Create default dataset
            // This is where we still store the metadata file, etc.
            auto dstore = createDefaultDataset();
            if (dstore.get() == nullptr)
                return false;

            // Set up metadata
            m_metadataFilePath = dstore->setDataFile("metadata.json");
            m_metadataDict["SubjectName"] = subject.id;
            m_metadataDict["SessionName"] = datasetNameSuggestion();
            m_metadataDict["DepthStreamEnabled"] = m_depthStreamEnabled;
            m_metadataDict["IRStreamEnabled"] = m_irStreamEnabled;
            m_metadataDict["IsLittleEndian"] = true; // hard coded this but again, we ca
            m_metadataDict["StartTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);

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

    void start() override // lol just start the module
    // this is more from syntalos internal purposes i think
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

                auto frameSet = m_pipeline->waitForFrames(200); // wait 200 ms for frames
                
                if (frameSet == nullptr) {
                    frameProcessFailedCount++;
                    std::cout << "Dropped frame. Frame process failed count is now " << frameProcessFailedCount << std::endl;
                    if (frameProcessFailedCount > 50) { // too many dropped frames -> stop the recording
                        raiseError(QStringLiteral("Too many attempts to process frames have failed. Is the camera connected properly?"));
                        m_running = false;
                        m_pipeline->stop();
                    }
                    continue;
                }

                if (!m_depthStreamEnabled || !m_irStreamEnabled) {
                    raiseError(QStringLiteral("A stream must be enabled!"));
                }

                if (m_depthStreamEnabled) {
                    auto depthFrame = frameSet->depthFrame(); // get depth frame
                    if (depthFrame) {
                        microseconds_t depthFrameRecvTime = microseconds_t(m_syTimer->timeSinceStartUsec());
                        processDepthFrame(depthFrame, depthFrameRecvTime);
                    }
                }

                if (m_irStreamEnabled) {
                    auto irFrame = frameSet->irFrame(); // get ir frame
                    if (irFrame) {
                        microseconds_t irFrameRecvTime = microseconds_t(m_syTimer->timeSinceStartUsec());
                        processIRFrame(irFrame, m_irOut, irFrameRecvTime);
                    }
                }

                m_frameIndex++;  // Increment frame index after processing both frames
                // make sure this is out of the main loop or else processIRFrame and processDepthFrame will be incrementing frame index simultaneously

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

        // Write metadata to file
        QJsonDocument doc(m_metadataDict);
        QFile metadataFile(m_metadataFilePath);
        if (metadataFile.open(QIODevice::WriteOnly)) {
            metadataFile.write(doc.toJson(QJsonDocument::Indented));
            metadataFile.close();
            statusMessage(QStringLiteral("Metadata saved to: %1").arg(m_metadataFilePath));
        } else {
            raiseError(QStringLiteral("Failed to save metadata to: %1").arg(m_metadataFilePath));
        }

        safeStopSynchronizer(m_clockSync);
    }

    void serializeSettings(const QString &, QVariantHash &settings, QByteArray &) override
    {
        settings.insert("depth_stream_enabled", m_depthStreamEnabled);
        settings.insert("ir_stream_enabled", m_irStreamEnabled);
    }

    bool loadSettings(const QString &, const QVariantHash &settings, const QByteArray &) override
    {
        m_depthStreamEnabled = settings.value("depth_stream_enabled", true).toBool();
        m_irStreamEnabled = settings.value("ir_stream_enabled", false).toBool();

        m_settingsDialog->setDepthStreamEnabled(m_depthStreamEnabled);
        m_settingsDialog->setIRStreamEnabled(m_irStreamEnabled);

        return true;
    }

private:
    void processDepthFrame(std::shared_ptr<ob::DepthFrame> depthFrame, microseconds_t frameRecvTime)
    {
        // quick check for valid
        if (!depthFrame || depthFrame->dataSize() == 0) {
            qWarning() << "Received invalid depth frame";
            return;
        }

        int width = depthFrame->width();
        int height = depthFrame->height();
        float scale = depthFrame->getValueScale(); // scale for the depth videos

        // Create raw depth data (16-bit). This is straightforward:
        // OpenCV matrices (cv::Mat) are defined with 4 main parameters: height, width, pixel format, and a pointer to the data.
        // 1. 'height' and 'width' specify the dimensions of the image, which should match the resolution of the depth camera output.
        // 2. We use 'CV_16UC1' for the pixel format, meaning each pixel is represented by a 16-bit unsigned integer ('16U') and the image has one channel ('C1').
        //    This format is ideal for depth data, as depth sensors typically provide high-precision 16-bit depth values.
        // 3. '(void*)depthFrame->data()' provides a pointer to the raw depth data captured by the depth sensor.
        //    This data is passed to OpenCV so it can wrap and manage the depth data within its matrix structure.
        // By passing the pointer to the data, OpenCV doesn't copy the data; it directly uses the provided memory, which is efficient for real-time processing.
        // We're gonna send this directly to raw depth port
        // This is not processed because processing is being handled by moseq
        cv::Mat rawDepth(height, width, CV_16UC1, (void*)depthFrame->data());

        // Create display frame (8-bit, color mapped)
        cv::Mat scaledDepth;
        rawDepth.convertTo(scaledDepth, CV_32F, scale);
        cv::Mat displayDepth;
        scaledDepth.convertTo(displayDepth, CV_8U, 255.0 / 5000);  // lets assume a max depth of 5000
        cv::applyColorMap(displayDepth, displayDepth, cv::COLORMAP_JET);

        microseconds_t deviceTimestamp = microseconds_t(depthFrame->timeStamp());
        m_clockSync->processTimestamp(frameRecvTime, deviceTimestamp);

        // Push raw frame (16-bit depth)
        Frame rawFrame(rawDepth, m_frameIndex, frameRecvTime);
        m_depthRawOut->push(rawFrame);

        // Push display frame (8-bit color mapped)
        Frame dispFrame(displayDepth, m_frameIndex, frameRecvTime);
        m_depthDispOut->push(dispFrame);

        // Print center pixel distance every 30 frames
        // this is useful for debugging and make sure we're actually scaling the video correctly
        if (m_frameIndex % 30 == 0) {
            uint16_t* data = (uint16_t*)depthFrame->data();
            float centerDistance = data[width * height / 2 + width / 2] * scale;
            std::cout << "Facing an object " << centerDistance << " mm away." << std::endl;
        }
    }

    void processIRFrame(std::shared_ptr<ob::IRFrame> irFrame, std::shared_ptr<DataStream<Frame>>& output, microseconds_t frameRecvTime)
    {
        // more straightforward
        if (!irFrame || irFrame->dataSize() == 0) {
            qWarning() << "Received invalid IR frame";
            return;
        }   

        if (!output) {
            qWarning() << "Invalid output DataStream";
            return;}

        try {
            int width = irFrame->width();
            int height = irFrame->height();
        
            cv::Mat irMat(height, width, CV_16UC1, (void*)irFrame->data());
        
            // Process IR Mat
            cv::Mat irVis;
            irMat.convertTo(irVis, CV_8UC1, 1.0 / 256.0);  // Convert 16-bit to 8-bit which we need to work with the canvas module
        
            cv::Mat colorMappedIR;
            cv::applyColorMap(irVis, colorMappedIR, cv::COLORMAP_HOT);  // Apply a color map for visualization

            microseconds_t deviceTimestamp = microseconds_t(irFrame->timeStamp()); // IR timestamps
            // we could probably remove this since its not very useful to have them, but for downstream analysis purposes, just why not lol
            if (m_clockSync) {
                m_clockSync->processTimestamp(frameRecvTime, deviceTimestamp);
            } else {
                qWarning() << "Clock synchronizer is not initialized";
            }

            Frame outFrame(colorMappedIR, m_frameIndex, frameRecvTime);
            output->push(outFrame);
        } catch (const cv::Exception& e) {
            qWarning() << "OpenCV error in processIRFrame:" << e.what();
        } catch (const std::exception& e) {
            qWarning() << "Error in processIRFrame:" << e.what();
        }
    }
};

// internal syntalos module info stuff
QString OrbbecModuleInfo::id() const
{
    return QStringLiteral("orbbec-cam");
}

QString OrbbecModuleInfo::name() const
{
    return QStringLiteral("Orbbec Femto Camera");
}

QString OrbbecModuleInfo::description() const
{
    return QStringLiteral("Capture depth and infrared data with an Orbbec Femto sensor!");
}

ModuleCategories OrbbecModuleInfo::categories() const
{
    return ModuleCategory::DEVICES;
}

AbstractModule* OrbbecModuleInfo::createModule(QObject* parent)
{
    return new OrbbecModule(this, parent);
}

#include "orbbecmodule.moc"
