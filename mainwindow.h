#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QListWidgetItem>
#include <QMainWindow>
#include <QSerialPort>
#include <QSettings>

class QLabel;
class QSpinBox;
class QComboBox;
class QPushButton;
class QListWidget;
class QSlider;
class QPlainTextEdit;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private slots:
    // Profiles
    void switchProfile(const QString &profile);
    void manageProfiles();

    // Ports / connection
    void refreshPorts();
    void connectOrDisconnect();
    void onSerialError(QSerialPort::SerialPortError err);
    void onSerialReadyRead();

    // Presets
    void onPresetCountChanged(int count);
    void onPresetDoubleClicked();
    void renamePresetRequested(const QPoint &pos);
    void onPresetNameEdited(QListWidgetItem *item);

    // PTZ / Zoom (press & release)
    void ptzPressed(int dx, int dy);
    void ptzReleased();
    void zoomInPressed();
    void zoomOutPressed();
    void zoomReleased();

    // Power
    void powerToggle();

    // Custom VISCA command
    void execSelectedCommand();

private:
    // UI: Pointers
    std::unique_ptr<QWidget> central{nullptr};

    // UI: Profiles
    QComboBox *profileCombo{};
    QPushButton *profileManageBtn{};

    // UI: Ports
    QComboBox *portCombo{};
    QPushButton *connectButton{};

    // UI: Power
    QLabel *powerLabel{};
    QPushButton *powerButton{};

    // UI: Presets
    QLabel *presetCountLabel{};
    QSpinBox *presetCountSpin{};
    QListWidget *presetList{};

    // UI: PTZ pad
    QPushButton *btnUpLeft{};
    QPushButton *btnUp{};
    QPushButton *btnUpRight{};
    QPushButton *btnLeft{};
    QPushButton *btnRight{};
    QPushButton *btnDownLeft{};
    QPushButton *btnDown{};
    QPushButton *btnDownRight{};

    // UI: Zoom + refocus
    QPushButton *btnZoomIn{};
    QPushButton *btnZoomOut{};
    QPushButton *btnRefocus{};

    // UI: Speeds
    QSlider *panSpeed{};
    QSlider *tiltSpeed{};
    QSlider *zoomSpeed{};

    // UI: Custom VISCA command
    QComboBox *cmdCombo{};
    QPushButton *cmdExecButton{};

    // UI: Responses view + title
    QLabel *rxTitle{};
    QPlainTextEdit *rxView{};

    // Core
    QSerialPort serial;
    QByteArray rxBuf;
    QSettings settings; // ("", "SimplePTZ")

    enum class PowerState { Unknown, On, Off };
    PowerState powerState{PowerState::Unknown};

    // Profiles
    QString currentProfile;
    void buildUi();
    void loadProfileList();
    void saveCurrentProfileSettings();
    void loadProfileSettings(const QString &profile);
    void createProfile();
    void renameCurrentProfile();
    void deleteCurrentProfile();

    // Preset helpers
    void savePresetState();
    void setConnectedUi(bool connected);
    void populatePresets(int count);
    void ensurePresetNamesSize(const QString &profile, int count);
    void updatePresetListHeight();
    int rxTwoLineMinHeight() const;

    // VISCA helpers
    void sendVisca(const QByteArray &bytes);
    void appendTx(const QByteArray &bytes);
    void appendRx(const QByteArray &bytes, const QString &note = QString());
    static QString toHexSpaced(const QByteArray &bytes);

    void viscaPowerInquiry();
    void viscaPowerOn();
    void viscaPowerOff();
    void setPowerUi(PowerState s);

    void sendRecallPreset(int n);     // n = 0..15
    void sendStorePreset(int n);      // n = 0..15
    void sendPanTilt(int dx, int dy); // dx,dy ∈ {-1,0,1}
    void sendPanTiltStop();
    void sendZoom(bool tele, int speed); // tele=true zoom in; speed 0..7
    void sendZoomStop();
    void sendRefocus();

    // Parsing
    void processIncomingFrames();
};

#endif // MAINWINDOW_H
