#include "mainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDebug>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSerialPortInfo>
#include <QSlider>
#include <QSpacerItem>
#include <QSpinBox>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

static const char *KEY_PROFILES_LIST = "profiles/list";
static const char *KEY_PROFILES_CURR = "profiles/current";

static int heightForTextLines(const QPlainTextEdit *w, int lines)
{
    QFontMetrics fm(w->font());
    const auto m = w->contentsMargins();
    return lines * fm.lineSpacing() + m.top() + m.bottom() + 8;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , settings("", "SimplePTZ")
{
    buildUi();

    // Populate ports BEFORE loading profiles so we can re-select saved port
    refreshPorts();
    loadProfileList(); // sets currentProfile and loads its settings
    setConnectedUi(false);

    connect(&serial, &QSerialPort::errorOccurred, this, &MainWindow::onSerialError);
    connect(&serial, &QSerialPort::readyRead, this, &MainWindow::onSerialReadyRead);

    setWindowTitle("SimplePTZ");
    resize(260, 650);
}

void MainWindow::buildUi()
{
    using std::unique_ptr;
    using std::make_unique;

    //auto *central = new QWidget(this);
    central = make_unique<QWidget>(this);
    auto *rootV = new QVBoxLayout(central.get());
    rootV->setContentsMargins(6, 6, 6, 6);
    rootV->setSpacing(6);

    // Row 0: Profile
    auto *profileRow = new QHBoxLayout();
    profileCombo = new QComboBox(this);
    profileCombo->setMinimumWidth(100);
    profileCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    profileCombo->setMinimumContentsLength(6);

    profileManageBtn = new QPushButton("Manage…", this);
    profileRow->addWidget(new QLabel("Profile:", this));
    profileRow->addWidget(profileCombo, 1);
    profileRow->addWidget(profileManageBtn);
    rootV->addLayout(profileRow);

    // Row 1: "Serial Port" label + port + connect
    auto *row1 = new QHBoxLayout();
    auto *portLbl = new QLabel("Serial Port", this);

    portCombo = new QComboBox(this);
    portCombo->setMinimumWidth(100); // compact
    portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    portCombo->setMinimumContentsLength(6);

    connectButton = new QPushButton("Connect", this);

    row1->addWidget(portLbl);
    row1->addWidget(portCombo, 1);
    row1->addWidget(connectButton);
    rootV->addLayout(row1);

    // Row 2: power status + toggle
    auto *row2 = new QHBoxLayout();
    powerLabel = new QLabel("Power: Unknown", this);
    powerLabel->setWordWrap(true);
    powerLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    powerButton = new QPushButton("Power On", this);
    row2->addWidget(powerLabel, 1);
    row2->addWidget(powerButton);
    rootV->addLayout(row2);

    // Row 3: "How many presets?" + spin
    auto *row3 = new QHBoxLayout();
    presetCountLabel = new QLabel("How many presets?", this);
    presetCountSpin = new QSpinBox(this);
    presetCountSpin->setRange(1, 16);
    presetCountSpin->setValue(6);
    presetCountSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    presetCountSpin->setLayoutDirection(Qt::LeftToRight);

    row3->addWidget(presetCountLabel);
    row3->addWidget(presetCountSpin);
    row3->addStretch();
    rootV->addLayout(row3);

    // Preset list (narrower, content-sized height)
    presetList = new QListWidget(this);
    presetList->setContextMenuPolicy(Qt::CustomContextMenu);
    presetList->setEditTriggers(QAbstractItemView::EditKeyPressed); // F2 to rename
    presetList->setMinimumWidth(90);
    presetList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    presetList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    rootV->addWidget(presetList);

    // ---- Controls block under the preset list ----
    auto *controlsV = new QVBoxLayout();

    // Row: PTZ grid (left) + stretch + vertical zoom/refocus (right flush)
    auto *controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(6);

    // PTZ grid (3x3, tight spacing)
    auto *ptzGrid = new QGridLayout();
    ptzGrid->setContentsMargins(0, 0, 0, 0);
    ptzGrid->setHorizontalSpacing(4);
    ptzGrid->setVerticalSpacing(4);

    auto mkBtn = [this](const QString &t) -> QPushButton * {
        auto *b = new QPushButton(t, this);
        b->setFixedSize(40, 30); // compact
        return b;
    };
    btnUpLeft = mkBtn("↖");
    btnUp = mkBtn("↑");
    btnUpRight = mkBtn("↗");
    btnLeft = mkBtn("←");
    btnRight = mkBtn("→");
    btnDownLeft = mkBtn("↙");
    btnDown = mkBtn("↓");
    btnDownRight = mkBtn("↘");

    // Arrange arrows
    ptzGrid->addWidget(btnUpLeft, 0, 0);
    ptzGrid->addWidget(btnUp, 0, 1);
    ptzGrid->addWidget(btnUpRight, 0, 2);
    ptzGrid->addWidget(btnLeft, 1, 0);
    ptzGrid->addItem(new QSpacerItem(4, 4), 1, 1);
    ptzGrid->addWidget(btnRight, 1, 2);
    ptzGrid->addWidget(btnDownLeft, 2, 0);
    ptzGrid->addWidget(btnDown, 2, 1);
    ptzGrid->addWidget(btnDownRight, 2, 2);

    controlsRow->addLayout(ptzGrid, 0);
    controlsRow->addStretch(1); // push zoom column all the way right

    // Zoom/refocus column (right)
    auto *zoomCol = new QVBoxLayout();
    zoomCol->setSpacing(6);

    btnZoomIn = new QPushButton("Zoom In", this);
    btnZoomOut = new QPushButton("Zoom Out", this);
    btnRefocus = new QPushButton("Refocus", this);

    const int zoomBtnW = 90;
    const int zoomBtnH = 28;
    for (QPushButton *b : {btnZoomIn, btnZoomOut, btnRefocus}) {
        b->setFixedSize(zoomBtnW, zoomBtnH);
    }
    zoomCol->addWidget(btnZoomIn);
    zoomCol->addWidget(btnZoomOut);
    zoomCol->addWidget(btnRefocus);
    zoomCol->addStretch();
    controlsRow->addLayout(zoomCol, 0);

    controlsV->addLayout(controlsRow);

    // Speed sliders (compact min widths)
    auto mkLabeledSlider =
        [this](const QString &label, int min, int max, int def, QSlider *&out) -> QHBoxLayout * {
        auto *h = new QHBoxLayout();
        auto *lbl = new QLabel(label, this);
        auto *s = new QSlider(Qt::Horizontal, this);
        s->setRange(min, max);
        s->setValue(def);
        s->setTickPosition(QSlider::TicksBelow);
        s->setTickInterval(std::max(1, (max - min) / 4));
        s->setMinimumWidth(120);
        s->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        out = s;
        h->addWidget(lbl);
        h->addWidget(s);
        return h;
    };
    controlsV->addLayout(mkLabeledSlider("Pan Speed", 1, 24, 12, panSpeed));
    controlsV->addLayout(mkLabeledSlider("Tilt Speed", 1, 20, 10, tiltSpeed));
    controlsV->addLayout(mkLabeledSlider("Zoom Speed", 0, 7, 3, zoomSpeed));

    rootV->addLayout(controlsV);

    // --- Label above custom commands ---
    auto *cmdTitle = new QLabel("Other commands", this);
    cmdTitle->setStyleSheet("font-weight:600;");
    rootV->addWidget(cmdTitle);

    // --- Custom VISCA command dropdown + Execute button (bottom) ---
    auto *cmdRow = new QHBoxLayout();
    cmdCombo = new QComboBox(this);
    cmdCombo->setMinimumWidth(100);
    cmdCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cmdCombo->setMinimumContentsLength(6);

    cmdExecButton = new QPushButton("Execute", this);

    auto addCmd = [&](const QString &label, const QByteArray &hex) {
        cmdCombo->addItem(label);
        cmdCombo->setItemData(cmdCombo->count() - 1, hex, Qt::UserRole);
    };

    addCmd("Power Inquiry — report ON/OFF (81 09 04 00 FF)", QByteArray::fromHex("81090400FF"));
    addCmd("Pan/Tilt Home — center position (81 01 06 04 FF)", QByteArray::fromHex("81010604FF"));
    addCmd("AF One-Push — refocus (81 01 04 18 01 FF)", QByteArray::fromHex("8101041801FF"));
    addCmd("Focus Auto ON (81 01 04 38 02 FF)", QByteArray::fromHex("8101043802FF"));
    addCmd("Focus Auto OFF / Manual (81 01 04 38 03 FF)", QByteArray::fromHex("8101043803FF"));

    cmdRow->addWidget(cmdCombo, 1);
    cmdRow->addWidget(cmdExecButton);
    rootV->addLayout(cmdRow);

    // --- Title + Responses box ---
    rxTitle = new QLabel("Commands sent/Responses received", this);
    rxTitle->setStyleSheet("font-weight:600;");
    rootV->addWidget(rxTitle);

    rxView = new QPlainTextEdit(this);
    rxView->setReadOnly(true);
    rxView->setMinimumHeight(60);
    rxView->setMinimumWidth(0);
    rxView->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    rxView->setWordWrapMode(QTextOption::WrapAnywhere);
    {
        QFont mono = rxView->font();
        mono.setStyleHint(QFont::Monospace);
        rxView->setFont(mono);
    }
    rxView->setPlaceholderText("Responses will appear here (TX/RX)...");
    rxView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rootV->addWidget(rxView);

    // Make responses box start around 8 lines tall, then allow it to expand
    int h8 = heightForTextLines(rxView, 8);
    rxView->setMinimumHeight(heightForTextLines(rxView, 2)); // later shrink floor
    rxView->setMaximumHeight(h8);                            // temp cap at startup

    // Release the cap after the first event loop tick so layouts can work normally

    QTimer::singleShot(0, this, [this] {
        if (!rxView)
            return;
        rxView->setMaximumHeight(QWIDGETSIZE_MAX);
        rxView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    });

    setCentralWidget(central.get());

    // Stretch: rxView grows/shrinks first; presetList is more fixed
    rootV->setStretchFactor(presetList, 0);
    rootV->setStretchFactor(rxView, 1);

    // --- Wire signals ---

    // Save port selection immediately on change (per profile)
    connect(portCombo, &QComboBox::currentTextChanged, this, [this](const QString &p) {
        if (!currentProfile.isEmpty())
            settings.setValue("profiles/" + currentProfile + "/lastPort", p);
    });

    // Profiles
    connect(profileCombo, &QComboBox::currentTextChanged, this, &MainWindow::switchProfile);
    connect(profileManageBtn, &QPushButton::clicked, this, &MainWindow::manageProfiles);

    // Preset count & list interactions
    connect(presetCountSpin, &QSpinBox::valueChanged, this, &MainWindow::onPresetCountChanged);
    connect(presetList, &QListWidget::itemDoubleClicked, this, &MainWindow::onPresetDoubleClicked);
    connect(presetList,
            &QListWidget::customContextMenuRequested,
            this,
            &MainWindow::renamePresetRequested);
    connect(presetList, &QListWidget::itemChanged, this, &MainWindow::onPresetNameEdited);

    // Ports & connect
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectOrDisconnect);

    // PTZ pressed/released
    auto hookPtz = [this](QPushButton *btn, int dx, int dy) {
        connect(btn, &QPushButton::pressed, this, [=] { ptzPressed(dx, dy); });
        connect(btn, &QPushButton::released, this, [=] { ptzReleased(); });
    };
    hookPtz(btnUpLeft, -1, -1);
    hookPtz(btnUp, 0, -1);
    hookPtz(btnUpRight, 1, -1);
    hookPtz(btnLeft, -1, 0);
    hookPtz(btnRight, 1, 0);
    hookPtz(btnDownLeft, -1, 1);
    hookPtz(btnDown, 0, 1);
    hookPtz(btnDownRight, 1, 1);

    // Zoom press/release
    connect(btnZoomIn, &QPushButton::pressed, this, &MainWindow::zoomInPressed);
    connect(btnZoomIn, &QPushButton::released, this, &MainWindow::zoomReleased);
    connect(btnZoomOut, &QPushButton::pressed, this, &MainWindow::zoomOutPressed);
    connect(btnZoomOut, &QPushButton::released, this, &MainWindow::zoomReleased);

    // Refocus
    connect(btnRefocus, &QPushButton::clicked, this, &MainWindow::sendRefocus);

    // Power toggle
    connect(powerButton, &QPushButton::clicked, this, &MainWindow::powerToggle);

    // Execute custom command
    connect(cmdExecButton, &QPushButton::clicked, this, &MainWindow::execSelectedCommand);

    // Initial sizing behaviors
    updatePresetListHeight();
    rxView->setMinimumHeight(rxTwoLineMinHeight());
}

// -------------------- Profiles --------------------

void MainWindow::loadProfileList()
{
    QStringList profiles = settings.value(KEY_PROFILES_LIST).toStringList();
    if (profiles.isEmpty()) {
        // Persist a default profile so it can be renamed immediately
        profiles << "Default";
        settings.setValue(KEY_PROFILES_LIST, profiles);

        const QString base = "profiles/Default/";
        const int count = 6;
        settings.setValue(base + "presetCount", count);
        QStringList names;
        for (int i = 0; i < count; ++i)
            names << QString("Preset %1").arg(i);
        settings.setValue(base + "presetNames", names);
        settings.setValue(base + "panSpeed", 12);
        settings.setValue(base + "tiltSpeed", 10);
        settings.setValue(base + "zoomSpeed", 3);
        settings.remove(base + "lastPort");
    }

    currentProfile = settings.value(KEY_PROFILES_CURR, profiles.first()).toString();
    if (!profiles.contains(currentProfile))
        currentProfile = profiles.first();
    settings.setValue(KEY_PROFILES_CURR, currentProfile);
    settings.sync();

    profileCombo->blockSignals(true);
    profileCombo->clear();
    profileCombo->addItems(profiles);
    profileCombo->setCurrentText(currentProfile);
    profileCombo->blockSignals(false);

    loadProfileSettings(currentProfile);
}

void MainWindow::saveCurrentProfileSettings()
{
    if (currentProfile.isEmpty())
        return;
    const QString base = "profiles/" + currentProfile + "/";

    settings.setValue(base + "presetCount", presetCountSpin->value());
    QStringList names;
    for (int i = 0; i < presetList->count(); ++i)
        names << presetList->item(i)->text();
    settings.setValue(base + "presetNames", names);

    settings.setValue(base + "panSpeed", panSpeed->value());
    settings.setValue(base + "tiltSpeed", tiltSpeed->value());
    settings.setValue(base + "zoomSpeed", zoomSpeed->value());

    settings.setValue(base + "lastPort", portCombo->currentText());

    // Keep list/current up to date
    QStringList profiles = settings.value(KEY_PROFILES_LIST).toStringList();
    if (profiles.isEmpty())
        profiles << "Default";
    if (!profiles.contains(currentProfile)) {
        profiles << currentProfile;
        profiles.removeDuplicates();
    }
    settings.setValue(KEY_PROFILES_LIST, profiles);
    settings.setValue(KEY_PROFILES_CURR, currentProfile);
    settings.sync();
}

void MainWindow::loadProfileSettings(const QString &profile)
{
    const QString base = "profiles/" + profile + "/";

    int count = settings.value(base + "presetCount", 6).toInt();
    presetCountSpin->setValue(count);

    QStringList names = settings.value(base + "presetNames").toStringList();
    presetList->clear();
    for (int i = 0; i < count; ++i) {
        auto *item = new QListWidgetItem(names.value(i, QString("Preset %1").arg(i)));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        presetList->addItem(item);
    }

    panSpeed->setValue(settings.value(base + "panSpeed", 12).toInt());
    tiltSpeed->setValue(settings.value(base + "tiltSpeed", 10).toInt());
    zoomSpeed->setValue(settings.value(base + "zoomSpeed", 3).toInt());

    // Restore last port if present (after refreshPorts ran)
    QString last = settings.value(base + "lastPort").toString();
    if (!last.isEmpty()) {
        int idx = portCombo->findText(last);
        if (idx >= 0)
            portCombo->setCurrentIndex(idx);
    }

    updatePresetListHeight();
}

void MainWindow::switchProfile(const QString &profile)
{
    if (profile.isEmpty() || profile == currentProfile)
        return;

    if (serial.isOpen()) {
        serial.close();
        setConnectedUi(false);
        powerLabel->setText("Power: Unknown");
        powerButton->setText("Power On");
        if (rxView)
            rxView->appendPlainText("--- Disconnected (profile switch) ---");
    }

    saveCurrentProfileSettings();
    currentProfile = profile;
    settings.setValue(KEY_PROFILES_CURR, currentProfile);
    loadProfileSettings(currentProfile);
}

void MainWindow::manageProfiles()
{
    QMenu m(this);
    QAction *aNew = m.addAction("New…");
    QAction *aRen = m.addAction("Rename…");
    QAction *aDel = m.addAction("Delete…");
    QAction *chosen = m.exec(QCursor::pos());
    if (chosen == aNew) {
        createProfile();
    } else if (chosen == aRen) {
        renameCurrentProfile();
    } else if (chosen == aDel) {
        deleteCurrentProfile();
    }
}

void MainWindow::createProfile()
{
    bool ok = false;
    QString name
        = QInputDialog::getText(this, "New Profile", "Profile name:", QLineEdit::Normal, "", &ok)
              .trimmed();
    if (!ok || name.isEmpty())
        return;

    QStringList profiles = settings.value(KEY_PROFILES_LIST).toStringList();
    for (const auto &p : profiles) {
        if (p.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, "Exists", "Profile already exists.");
            return;
        }
    }
    profiles << name;
    profiles.removeDuplicates();
    settings.setValue(KEY_PROFILES_LIST, profiles);

    // Initialize with defaults
    currentProfile = name;
    settings.setValue(KEY_PROFILES_CURR, currentProfile);
    const QString base = "profiles/" + currentProfile + "/";
    const int count = presetCountSpin ? presetCountSpin->value() : 6;
    settings.setValue(base + "presetCount", count);
    QStringList defaultNames;
    for (int i = 0; i < count; ++i)
        defaultNames << QString("Preset %1").arg(i);
    settings.setValue(base + "presetNames", defaultNames);
    settings.setValue(base + "panSpeed", 12);
    settings.setValue(base + "tiltSpeed", 10);
    settings.setValue(base + "zoomSpeed", 3);
    settings.remove(base + "lastPort");
    settings.sync();

    profileCombo->blockSignals(true);
    profileCombo->clear();
    profileCombo->addItems(profiles);
    profileCombo->setCurrentText(currentProfile);
    profileCombo->blockSignals(false);

    loadProfileSettings(currentProfile);
}

void MainWindow::renameCurrentProfile()
{
    // Build list from UI
    QStringList profiles;
    for (int i = 0; i < profileCombo->count(); ++i)
        profiles << profileCombo->itemText(i);
    if (profiles.isEmpty())
        profiles << "Default";

    const QString oldName = currentProfile;

    bool ok = false;
    QString newName
        = QInputDialog::getText(this, "Rename Profile", "New name:", QLineEdit::Normal, oldName, &ok)
              .trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    for (const auto &p : profiles)
        if (p.compare(newName, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, "Exists", "A profile with that name already exists.");
            return;
        }

    const QString from = "profiles/" + oldName + "/";
    const QString to = "profiles/" + newName + "/";
    const QStringList keys
        = {"presetCount", "presetNames", "panSpeed", "tiltSpeed", "zoomSpeed", "lastPort"};
    for (const QString &k : keys)
        settings.setValue(to + k, settings.value(from + k));
    settings.remove(from);

    int idx = profiles.indexOf(oldName);
    if (idx >= 0)
        profiles[idx] = newName;
    settings.setValue(KEY_PROFILES_LIST, profiles);
    currentProfile = newName;
    settings.setValue(KEY_PROFILES_CURR, currentProfile);
    settings.sync();

    profileCombo->blockSignals(true);
    profileCombo->clear();
    profileCombo->addItems(profiles);
    profileCombo->setCurrentText(currentProfile);
    profileCombo->blockSignals(false);

    loadProfileSettings(currentProfile);
}

void MainWindow::deleteCurrentProfile()
{
    QStringList profiles = settings.value(KEY_PROFILES_LIST).toStringList();
    if (profiles.size() <= 1) {
        QMessageBox::information(this, "Cannot Delete", "At least one profile must exist.");
        return;
    }
    if (QMessageBox::question(this,
                              "Delete Profile",
                              QString("Delete profile \"%1\"?").arg(currentProfile))
        != QMessageBox::Yes)
        return;

    const QString base = "profiles/" + currentProfile + "/";
    settings.remove(base);

    profiles.removeAll(currentProfile);
    settings.setValue(KEY_PROFILES_LIST, profiles);

    currentProfile = profiles.first();
    settings.setValue(KEY_PROFILES_CURR, currentProfile);
    settings.sync();

    profileCombo->blockSignals(true);
    profileCombo->clear();
    profileCombo->addItems(profiles);
    profileCombo->setCurrentText(currentProfile);
    profileCombo->blockSignals(false);

    loadProfileSettings(currentProfile);
}

// -------------------- Ports & Connection --------------------

void MainWindow::refreshPorts()
{
    portCombo->clear();
#ifdef Q_OS_WIN
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        portCombo->addItem(info.portName()); // "COM4"
#else
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        portCombo->addItem(info.systemLocation()); // "/dev/tty.usbserial-xxxx"
#endif

    // Re-select saved port if available (profile may not yet be loaded on app start)
    const QString base = "profiles/" + currentProfile + "/";
    const QString last = settings.value(base + "lastPort").toString();
    if (!last.isEmpty()) {
        int idx = portCombo->findText(last);
        if (idx >= 0)
            portCombo->setCurrentIndex(idx);
    }
}

void MainWindow::connectOrDisconnect()
{
    if (serial.isOpen()) {
        serial.close();
        setConnectedUi(false);
        powerLabel->setText("Power: Unknown");
        powerButton->setText("Power On");
        if (rxView)
            rxView->appendPlainText("--- Disconnected ---");
        return;
    }

    const QString sel = portCombo->currentText();
    if (sel.isEmpty()) {
        QMessageBox::warning(this, "No Port", "No serial port selected.");
        return;
    }

    serial.setPortName(sel);
    serial.setBaudRate(QSerialPort::Baud9600);

    if (!serial.open(QIODevice::ReadWrite)) {
        QMessageBox::critical(this, "Error", QString("Failed to open %1").arg(sel));
        return;
    }

    setConnectedUi(true);
    if (rxView)
        rxView->appendPlainText(QString("--- Connected %1 ---").arg(sel));

    // Persist last port for this profile
    settings.setValue("profiles/" + currentProfile + "/lastPort", sel);
    settings.sync();

    // Query power on connect
    viscaPowerInquiry();
}

void MainWindow::setConnectedUi(bool connected)
{
    connectButton->setText(connected ? "Disconnect" : "Connect");
    portCombo->setEnabled(!connected);

    const bool e = connected;
    QList<QPushButton *> btns = {btnUpLeft,
                                 btnUp,
                                 btnUpRight,
                                 btnLeft,
                                 btnRight,
                                 btnDownLeft,
                                 btnDown,
                                 btnDownRight,
                                 btnZoomIn,
                                 btnZoomOut,
                                 btnRefocus,
                                 powerButton,
                                 cmdExecButton};
    for (auto *b : btns)
        b->setEnabled(e);
    if (cmdCombo)
        cmdCombo->setEnabled(e);
}

void MainWindow::onSerialError(QSerialPort::SerialPortError err)
{
    if (err == QSerialPort::NoError)
        return;
    if (serial.isOpen())
        serial.close();
    setConnectedUi(false);
    powerLabel->setText("Power: Unknown");
    powerButton->setText("Power On");
    qWarning() << "Serial error:" << err << serial.errorString();
    QMessageBox::warning(this, "Serial Error", serial.errorString());
    refreshPorts();
    if (rxView)
        rxView->appendPlainText("--- Serial error, disconnected ---");
}

void MainWindow::onSerialReadyRead()
{
    rxBuf += serial.readAll();
    processIncomingFrames();
}

// -------------------- Presets UI --------------------

void MainWindow::onPresetCountChanged(int count)
{
    ensurePresetNamesSize(currentProfile, count);
    presetList->clear();
    const QString base = "profiles/" + currentProfile + "/";
    QStringList names = settings.value(base + "presetNames").toStringList();
    for (int i = 0; i < count; ++i) {
        auto *item = new QListWidgetItem(names.value(i, QString("Preset %1").arg(i)));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        presetList->addItem(item);
    }
    saveCurrentProfileSettings();
    updatePresetListHeight();
}

void MainWindow::ensurePresetNamesSize(const QString &profile, int count)
{
    const QString base = "profiles/" + profile + "/";
    QStringList names = settings.value(base + "presetNames").toStringList();
    if (names.size() < count) {
        for (int i = names.size(); i < count; ++i)
            names << QString("Preset %1").arg(i);
    } else if (names.size() > count) {
        names = names.mid(0, count);
    }
    settings.setValue(base + "presetNames", names);
}

void MainWindow::populatePresets(int count)
{
    presetList->clear();
    QStringList names;
    for (int i = 0; i < count; ++i) {
        names << QString("Preset %1").arg(i);
        auto *item = new QListWidgetItem(QString("Preset %1").arg(i));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        presetList->addItem(item);
    }
    const QString base = "profiles/" + currentProfile + "/";
    settings.setValue(base + "presetNames", names);
}

void MainWindow::onPresetDoubleClicked()
{
    if (!serial.isOpen()) {
        QMessageBox::information(this, "Not connected", "Connect to a serial port first.");
        return;
    }
    if (auto *item = presetList->currentItem()) {
        Q_UNUSED(item);
        int row = presetList->currentRow(); // 0..N-1
        sendRecallPreset(row);
    }
}

void MainWindow::renamePresetRequested(const QPoint &pos)
{
    if (QListWidgetItem *item = presetList->itemAt(pos)) {
        int row = presetList->row(item); // 0..N-1
        QMenu menu(this);
        QAction *actRename = menu.addAction("Rename…");
        QAction *actStore = menu.addAction("Set current position as this preset");
        QAction *chosen = menu.exec(presetList->viewport()->mapToGlobal(pos));
        if (chosen == actRename) {
            presetList->edit(presetList->indexFromItem(item));
        } else if (chosen == actStore) {
            if (!serial.isOpen()) {
                QMessageBox::information(this, "Not connected", "Connect to a serial port first.");
                return;
            }
            sendStorePreset(row);
        }
    }
}

void MainWindow::onPresetNameEdited(QListWidgetItem *item)
{
    if (!item)
        return;
    if (item->text().trimmed().isEmpty()) {
        int row = presetList->row(item);
        item->setText(QString("Preset %1").arg(row));
    }
    saveCurrentProfileSettings();
}

// -------------------- VISCA RX/TX + Parsing --------------------

void MainWindow::processIncomingFrames()
{
    while (true) {
        int end = rxBuf.indexOf(char(0xFF));
        if (end < 0)
            break;
        QByteArray frame = rxBuf.left(end + 1);
        rxBuf.remove(0, end + 1);

        QString note;

        // Power inquiry reply: 90 50 02 FF (ON), 90 50 03 FF (OFF)
        if (frame.size() >= 4 && quint8(frame[0]) == 0x90 && quint8(frame[1]) == 0x50) {
            if (quint8(frame[2]) == 0x02) {
                setPowerUi(PowerState::On);
                note = "power=On";
            } else if (quint8(frame[2]) == 0x03) {
                setPowerUi(PowerState::Off);
                note = "power=Off";
            }
        }

        appendRx(frame, note);
    }
}

QString MainWindow::toHexSpaced(const QByteArray &bytes)
{
    QString s;
    s.reserve(bytes.size() * 3);
    for (unsigned char b : bytes)
        s += QString("%1 ").arg(b, 2, 16, QLatin1Char('0')).toUpper();
    return s.trimmed();
}

void MainWindow::appendTx(const QByteArray &bytes)
{
    if (!rxView)
        return;
    rxView->appendPlainText("TX: " + toHexSpaced(bytes));
}

void MainWindow::appendRx(const QByteArray &bytes, const QString &note)
{
    if (!rxView)
        return;
    if (note.isEmpty())
        rxView->appendPlainText("RX: " + toHexSpaced(bytes));
    else
        rxView->appendPlainText("RX: " + toHexSpaced(bytes) + "    // " + note);
}

void MainWindow::sendVisca(const QByteArray &bytes)
{
    if (!serial.isOpen())
        return;
    appendTx(bytes);
    serial.write(bytes);
    serial.flush();
}

// -------------------- Power --------------------

void MainWindow::viscaPowerInquiry()
{
    sendVisca(QByteArray::fromHex("81090400FF")); // Inquiry: power
}

void MainWindow::viscaPowerOn()
{
    sendVisca(QByteArray::fromHex("8101040002FF")); // Power On
}

void MainWindow::viscaPowerOff()
{
    sendVisca(QByteArray::fromHex("8101040003FF")); // Power Off
}

void MainWindow::setPowerUi(PowerState s)
{
    powerState = s;
    switch (s) {
    case PowerState::On:
        powerLabel->setText("Power: On");
        powerButton->setText("Power Off");
        break;
    case PowerState::Off:
        powerLabel->setText("Power: Off");
        powerButton->setText("Power On");
        break;
    case PowerState::Unknown:
    default:
        powerLabel->setText("Power: Unknown");
        powerButton->setText("Power On");
        break;
    }
}

void MainWindow::powerToggle()
{
    if (!serial.isOpen())
        return;

    bool currentlyOn = (powerButton->text().contains("Off", Qt::CaseInsensitive));
    if (currentlyOn) {
        if (QMessageBox::question(this,
                                  "Confirm Power Off",
                                  "Are you sure you want to turn the camera off?")
            == QMessageBox::Yes) {
            viscaPowerOff();
            setPowerUi(PowerState::Off);
        }
    } else {
        viscaPowerOn();
        setPowerUi(PowerState::On);
    }
}

// -------------------- PTZ / Zoom / Presets --------------------

void MainWindow::sendRecallPreset(int n)
{
    if (n < 0 || n > 15)
        return;
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x3F));
    cmd.append(char(0x02)); // recall
    cmd.append(char(n));    // 0x00..0x0F
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void MainWindow::sendStorePreset(int n)
{
    if (n < 0 || n > 15)
        return;
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x3F));
    cmd.append(char(0x01)); // set
    cmd.append(char(n));    // 0x00..0x0F
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void MainWindow::ptzPressed(int dx, int dy)
{
    if (!serial.isOpen())
        return;
    int pan = std::clamp(panSpeed->value(), 1, 24);
    int tilt = std::clamp(tiltSpeed->value(), 1, 20);

    quint8 panDir = (dx < 0) ? 0x01 : (dx > 0 ? 0x02 : 0x03);  // 01 left, 02 right, 03 stop
    quint8 tiltDir = (dy < 0) ? 0x01 : (dy > 0 ? 0x02 : 0x03); // 01 up, 02 down, 03 stop

    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x06));
    cmd.append(char(0x01));
    cmd.append(char(pan));
    cmd.append(char(tilt));
    cmd.append(char(panDir));
    cmd.append(char(tiltDir));
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void MainWindow::ptzReleased()
{
    if (!serial.isOpen())
        return;
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x06));
    cmd.append(char(0x01));
    cmd.append(char(std::clamp(panSpeed->value(), 1, 24)));
    cmd.append(char(std::clamp(tiltSpeed->value(), 1, 20)));
    cmd.append(char(0x03)); // stop pan
    cmd.append(char(0x03)); // stop tilt
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void MainWindow::zoomInPressed()
{
    if (!serial.isOpen())
        return;
    int p = std::clamp(zoomSpeed->value(), 0, 7);
    quint8 op = 0x20 | p; // 2p
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x07));
    cmd.append(char(op));
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void MainWindow::zoomOutPressed()
{
    if (!serial.isOpen())
        return;
    int p = std::clamp(zoomSpeed->value(), 0, 7);
    quint8 op = 0x30 | p; // 3p
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x07));
    cmd.append(char(op));
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void MainWindow::zoomReleased()
{
    if (!serial.isOpen())
        return;
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x07));
    cmd.append(char(0x00)); // stop
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void MainWindow::sendRefocus()
{
    if (!serial.isOpen())
        return;
    sendVisca(QByteArray::fromHex("8101041801FF")); // AF one-push
}

// -------------------- Custom Commands --------------------

void MainWindow::execSelectedCommand()
{
    if (!serial.isOpen()) {
        QMessageBox::information(this, "Not connected", "Connect to a serial port first.");
        return;
    }
    int idx = cmdCombo->currentIndex();
    if (idx < 0)
        return;
    QByteArray cmd = cmdCombo->itemData(idx, Qt::UserRole).toByteArray();
    if (cmd.isEmpty())
        return;
    sendVisca(cmd);
}

// -------------------- Events / sizing --------------------

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveCurrentProfileSettings();
    QMainWindow::closeEvent(e);
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    if (rxView)
        rxView->setMinimumHeight(rxTwoLineMinHeight());
    updatePresetListHeight();
    QMainWindow::resizeEvent(e);
}

// -------------------- Helpers for sizing --------------------

int MainWindow::rxTwoLineMinHeight() const
{
    QFontMetrics fm(rxView->font());
    int lines = fm.lineSpacing() * 2;
    int margins = rxView->contentsMargins().top() + rxView->contentsMargins().bottom();
    return lines + margins + 8;
}

void MainWindow::updatePresetListHeight()
{
    if (!presetList)
        return;

    const int rows = presetList->count();
    // We’ll reserve space for up to 16 visible rows (or fewer if rows < 16)
    const int visibleRows = std::min(rows > 0 ? rows : 1, 16);

    // Get a reasonable per-row height (fallback to font height + padding)
    int rowH = 0;
    if (rows > 0) {
        // Try a real item’s size hint
        rowH = presetList->sizeHintForRow(0);
    }
    if (rowH <= 0) {
        rowH = presetList->fontMetrics().height() + 8; // safe fallback
    }

    // Frame/margins
    const int frame = 2 * presetList->frameWidth();

    // Target minimum height = exactly 'visibleRows' rows worth, so the list
    // will try to show that many rows; it can still grow, and it will show a
    // scrollbar if the window is too short.
    const int minH = visibleRows * rowH + frame;

    presetList->setMinimumHeight(minH);
    presetList->setMaximumHeight(QWIDGETSIZE_MAX); // no artificial cap
    presetList->updateGeometry();
}
