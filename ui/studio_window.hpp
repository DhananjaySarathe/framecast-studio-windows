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
    int width = 1280, height = 720;
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
