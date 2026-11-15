#pragma once
#include "studio_model.hpp"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>
#include <set>
class QLabel;
class QPushButton;
class QToolButton;
class QStackedWidget;
class QVBoxLayout;
class QSlider;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;

namespace framecast::ui {
class CanvasView;
class SceneButton;
class Meter;
struct UiOptions {
    bool synthetic = false, persistent = true;
    // Full HD by default: hardware encoders handle it, and without one the encoder records at 720p itself.
    int width = 1920, height = 1080;
    bool autoStart = true;
};
class StudioWindow final : public QMainWindow {
  public:
    explicit StudioWindow(UiOptions options = {});
    ~StudioWindow() override;
    void saveScreenshots(const QString &directory);
    StudioController &controller() {
        return controller_;
    }
    QString programName() const;
    QString recordDirectory() const {
        return recordDirectory_;
    }
    void setRecordDirectory(QString directory) {
        recordDirectory_ = std::move(directory);
    }
    void toggleRecord();
    void toggleBroadcast();
    void selectScene(int index);
    void setPanel(int index);
    void takePreview();

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    UiOptions options_;
    EngineConfig config_;
    StudioController controller_;
    std::vector<SceneEntry> scenes_;
    Scene program_, candidate_;
    int programH_ = 3, programV_ = 3, selected_ = 3, canvas_ = 0, stage_ = 2;
    int previewH_ = 3, previewV_ = 3;
    bool previewMode_ = false, updatingLayer_ = false, closing_ = false;
    // Record or Start broadcast pressed before the preview was running: start the preview (asking for
    // sources first if none were ever chosen) and begin that output as soon as the preview is live.
    enum class PendingOutput { none, record, broadcast } pendingOutput_ = PendingOutput::none;
    // New sources chosen while the preview runs: the preview stops and restarts with them.
    bool restartPending_ = false;
    // Sources have been chosen at least once (kept between launches), so Record can start without asking.
    bool sourcesChosen_ = false;
    // The Add dialog is open. Its event loop keeps refresh() running, which must not start a queued preview
    // or output underneath it (seen on the laptop: the preview started before any source was picked).
    bool choosingSources_ = false;
    QString recordDirectory_, container_ = "mkv";
    std::map<std::string, OutputSpec> outputs_;
    std::vector<std::string> recording_, broadcasting_;
    QElapsedTimer recordClock_, broadcastClock_;
    struct Destination {
        QString name, server, key;
        Canvas canvas = Canvas::horizontal;
        bool enabled = true;
    };
    std::vector<Destination> destinations_;
    QTimer timer_;
    QLabel *sceneLabel_, *sceneHint_, *notice_, *health_, *recordStatus_, *broadcastStatus_, *elapsed_,
        *dropLabel_;
    QPushButton *record_, *broadcast_, *preview_, *take_, *sources_, *destinationsButton_;
    QWidget *programArea_;
    QStackedWidget *panels_;
    CanvasView *horizontal_, *vertical_;
    CanvasView *airHorizontal_, *airVertical_;
    QWidget *airMonitor_;
    std::vector<SceneButton *> sceneButtons_;
    std::vector<QToolButton *> panelButtons_, stageButtons_;
    std::vector<QSlider *> faders_;
    std::vector<QPushButton *> mutes_;
    std::vector<Meter *> meters_;
    QListWidget *layers_;
    std::vector<QDoubleSpinBox *> transforms_;
    QComboBox *fit_, *mask_;
    QPushButton *mirror_;
    int layerIndex_ = -1;
    QSettings settings_;
    int refreshCount_ = 0;
    std::set<std::string> seenFailures_;
    QString outputError_;
    LiveState last_;
    void buildUi();
    QWidget *makeSidebar();
    QWidget *makeInspector();
    QWidget *makeMixer();
    QWidget *makeLayers();
    void showSources();
    void showSettings();
    void showDestinations();
    void startPreview();
    void startForOutput(PendingOutput output);
    bool previewRunning();
    void publishScenes();
    void publishGains();
    void refresh();
    void setCanvas(int canvas);
    void setStage(int stage);
    void updateSceneButtons();
    void updateLayers();
    void editLayer();
    void persist();
    void restore();
    bool hasPendingOutputs() const;
    bool groupPending(const std::vector<std::string> &ids) const;
    OutputSpec outputSpec(Canvas canvas, const QString &destination, bool streaming) const;
};
} // namespace framecast::ui
