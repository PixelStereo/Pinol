#include "SettingsPanel.h"
#include "Widgets.h"
#include "ColorPicker.h"
#include "SequenceFeedback.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QUrl>
#include <QNetworkInterface>
#include <QProgressBar>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QTimer>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include "RemoteServer.h"
#include <QLineEdit>
#include <algorithm>
#include <cmath>

static const char *kPlayModeKey = "playback/defaultMode";
static const char *kHardwareKey = "playback/hardwareDecoding";
static const char *kFollowKey = "ui/followFades";
static const char *kOscKey = "osc/enabled";
static const char *kOscPortKey = "osc/udpPort";        // (osc/port, osc/queryPort: earlier defaults, ignored)
static const char *kQueryPortKey = "osc/oscQueryPort";
static const char *kOscListenKey = "osc/listen"; // the address listened on ("": every interface)
static const char *kRemoteKey = "remote/enabled", *kRemotePortKey = "remote/port", *kRemotePinKey = "remote/pin",
                   *kRemotePinUsedKey = "remote/pinRequired";
static constexpr int kDefaultRemotePort = 8765;
static const char *kFeedbackKey = "osc/feedback/enabled";      // the sequence's state sent by OSC
static const char *kFeedbackTargetsKey = "osc/feedback/targets"; // "host:port" each
static const char *kTransitionKey = "snapshots/transition";
static const char *kRateKey = "render/frameRate";
static const char *kSamplesKey = "render/antialiasing";
static const char *kMipmapsKey = "render/mipmaps";
static const char *kDepthKey = "render/depth";
static const char *kShareLateKey = "render/gpuShareOneFrameLate";
static const char *kStartupUseKey = "startup/useProject"; // false: an empty project at launch
static const char *kStartupPathKey = "startup/project";

QList<double> renderChoice::frameRates() { return {0, 24, 25, 30, 50, 60, 120}; }
QString renderChoice::frameRateName(double fps)
{
    return fps <= 0 ? QStringLiteral("Screen refresh") : QStringLiteral("%1 fps").arg(fps);
}
QList<int> renderChoice::depths() { return {10, 8}; }
QString renderChoice::depthName(int bits)
{
    return bits >= 10 ? QStringLiteral("16 bits/channel (RGBA16)") : QStringLiteral("8 bits/channel (lighter)");
}
QList<int> renderChoice::samples() { return {0, 2, 4, 8}; }
QString renderChoice::samplesName(int n) { return n <= 1 ? QStringLiteral("Off") : QStringLiteral("%1× MSAA").arg(n); }

Engine::RenderSettings SettingsPanel::renderDefaults()
{
    QSettings s;
    Engine::RenderSettings r;
    r.frameRate = std::max(0.0, s.value(kRateKey, 0.0).toDouble());
    r.samples = std::max(0, s.value(kSamplesKey, 4).toInt());
    r.mipmaps = s.value(kMipmapsKey, true).toBool() ? 1 : 0;
    r.depth = s.value(kDepthKey, 10).toInt() >= 10 ? 10 : 8; // RGBA16 unless 8 bits are chosen
    return r;
}
static constexpr int kDefaultOscPort = 1234, kDefaultQueryPort = 5678;

PlayMode SettingsPanel::defaultPlayMode()
{
    return playModeFromKey(QSettings().value(kPlayModeKey).toString(), PlayMode::Loop);
}
bool SettingsPanel::gpuShareOneFrameLate() { return QSettings().value(kShareLateKey, true).toBool(); }
bool SettingsPanel::hardwareDecoding() { return QSettings().value(kHardwareKey, true).toBool(); }
static int s_followFades = -1; // read once, then kept up to date by the check box (asked 10 times a second)
bool SettingsPanel::followFades()
{
    if (s_followFades < 0) s_followFades = QSettings().value(kFollowKey, true).toBool() ? 1 : 0;
    return s_followFades == 1;
}
QString SettingsPanel::startupProject()
{
    QSettings s;
    return s.value(kStartupUseKey, false).toBool() ? s.value(kStartupPathKey).toString() : QString();
}
bool SettingsPanel::oscEnabled() { return QSettings().value(kOscKey, true).toBool(); }
bool SettingsPanel::remoteEnabled() { return QSettings().value(kRemoteKey, false).toBool(); }
int SettingsPanel::remotePort() { return std::clamp(QSettings().value(kRemotePortKey, kDefaultRemotePort).toInt(), 1024, 65535); }
bool SettingsPanel::remotePinRequired() { return QSettings().value(kRemotePinUsedKey, true).toBool(); }
QString SettingsPanel::remotePin()
{
    QSettings s;
    if (!s.value(kRemotePinUsedKey, true).toBool()) return QString(); // no code asked
    QString pin = s.value(kRemotePinKey).toString();
    if (!RemoteServer::pinValid(pin)) {
        pin = RemoteServer::makePin(6);
        s.setValue(kRemotePinKey, pin);
    }
    return pin;
}
int SettingsPanel::oscPort() { return QSettings().value(kOscPortKey, kDefaultOscPort).toInt(); }
int SettingsPanel::oscQueryPort() { return QSettings().value(kQueryPortKey, kDefaultQueryPort).toInt(); }
QString SettingsPanel::oscListen() { return QSettings().value(kOscListenKey).toString(); }
bool SettingsPanel::feedbackEnabled() { return QSettings().value(kFeedbackKey, true).toBool(); }
QStringList SettingsPanel::feedbackTargets()
{
    return QSettings().value(kFeedbackTargetsKey, SequenceFeedback::defaultTargets()).toStringList();
}

QString SettingsPanel::defaultTransition(const IsfLibrary &library)
{
    const QString saved = QSettings().value(kTransitionKey, QStringLiteral("Crossfade.fs")).toString();
    if (QFileInfo::exists(saved)) return saved;
    return library.findByFileName(QFileInfo(saved).fileName()); // empty: a built-in crossfade
}

void SettingsPanel::setTransitions(const QVector<IsfEntry> &transitions, const QString &current)
{
    QSignalBlocker b(m_transition);
    m_transition->clear();
    m_transition->addItem(QStringLiteral("Crossfade (built in)"), QString());
    for (const IsfEntry &t : transitions) {
        m_transition->addItem(t.name, t.path);
        m_transition->setItemData(m_transition->count() - 1, t.description, Qt::ToolTipRole);
    }
    if (!current.isEmpty() && m_transition->findData(current) < 0)
        m_transition->addItem(QFileInfo(current).completeBaseName(), current);
    m_transition->setCurrentIndex(std::max(0, m_transition->findData(current)));
}

static QLabel *note(const QString &t)
{
    auto *l = new QLabel(t);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setStyleSheet("color:#888; font-size:11px;");
    return l;
}

SettingsPanel::SettingsPanel(Engine *engine, QWidget *parent) : QWidget(parent), m_engine(engine)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->setStyleSheet(theme::tabStyle()); // as the inspector's sub-tabs
    connect(theme::notifier(), &theme::Notifier::changed, tabs, [tabs] { tabs->setStyleSheet(theme::tabStyle()); });
    root->addWidget(tabs);
    // One page per subject; the page shown is kept from one session to the next
    auto page = [tabs](const QString &title) {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(8, 8, 8, 8);
        l->setSpacing(10);
        tabs->addTab(w, title);
        return l;
    };
    QVBoxLayout *playbackPage = page(QStringLiteral("Playback"));
    QVBoxLayout *renderPage = page(QStringLiteral("Rendering"));
    QVBoxLayout *audioPage = page(QStringLiteral("Audio"));
    QVBoxLayout *interfacePage = page(QStringLiteral("Interface"));
    QVBoxLayout *oscPage = page(QStringLiteral("OSC"));
    QVBoxLayout *isfPage = page(QStringLiteral("ISF"));
    tabs->setCurrentIndex(std::clamp(QSettings().value(QStringLiteral("ui/settingsTab"), 0).toInt(), 0, tabs->count() - 1));
    connect(tabs, &QTabWidget::currentChanged, this, [](int i) { QSettings().setValue(QStringLiteral("ui/settingsTab"), i); });

    auto *playback = new QGroupBox(QStringLiteral("Playback"));
    auto *form = new QFormLayout(playback);
    m_playMode = new QComboBox;
    for (PlayMode m : {PlayMode::OneShot, PlayMode::Loop, PlayMode::PingPong, PlayMode::Stop})
        m_playMode->addItem(playModeName(m), int(m));
    m_playMode->setCurrentIndex(m_playMode->findData(int(defaultPlayMode())));
    form->addRow(new ResetLabel(QStringLiteral("Default play mode"), [this] { m_playMode->setCurrentIndex(1); }), m_playMode);
    form->addRow(note(QStringLiteral("Given to a video or a sound when it is loaded into a layer. "
                                     "Layers already loaded keep their own mode.")));
    m_transition = new QComboBox;
    m_transition->setObjectName(QStringLiteral("snapshotTransition"));
    form->addRow(new ResetLabel(QStringLiteral("Snapshot transition"),
                                [this] {
                                    const int k = m_transition->findText(QStringLiteral("Crossfade"));
                                    m_transition->setCurrentIndex(std::max(0, k));
                                }),
                 m_transition);
    form->addRow(note(QStringLiteral("When a snapshot gives a layer another source: the outgoing one keeps playing and "
                                     "this ISF transition takes it to the new one, over the snapshot's fade. "
                                     "A layer can choose its own (Source tab).")));
    m_hardware = new FlagBox(QStringLiteral("Hardware decoding"));
    m_hardware->setChecked(hardwareDecoding());
    form->addRow(m_hardware);
    form->addRow(note(QStringLiteral("H.264, HEVC, ProRes… decoded by the graphics hardware when it can (VideoToolbox on "
                                     "macOS, Direct3D 11 on Windows, VA-API on Linux), which leaves the processor to the "
                                     "other videos. HAP never needs it. For the videos loaded from now on.")));
    connect(m_hardware, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(kHardwareKey, on);
        emit hardwareDecodingChanged();
    });
    connect(m_transition, qOverload<int>(&QComboBox::activated), this, [this] {
        const QString path = m_transition->currentData().toString();
        QSettings().setValue(kTransitionKey, path);
        emit transitionChanged(path);
    });
    playbackPage->addWidget(playback);
    connect(m_playMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        QSettings().setValue(kPlayModeKey, playModeKey(PlayMode(m_playMode->currentData().toInt())));
        emit playModeChanged();
    });

    // Rendering: the machine's, for every project
    {
        auto *box = new QGroupBox(QStringLiteral("Rendering"));
        auto *rf = new QFormLayout(box);
        const Engine::RenderSettings d = renderDefaults();
        m_rate = new QComboBox;
        for (double r : renderChoice::frameRates()) m_rate->addItem(renderChoice::frameRateName(r), r);
        m_rate->setCurrentIndex(std::max(0, m_rate->findData(d.frameRate)));
        m_samples = new QComboBox;
        for (int n : renderChoice::samples()) m_samples->addItem(renderChoice::samplesName(n), n);
        m_samples->setCurrentIndex(std::max(0, m_samples->findData(d.samples)));
        m_depth = new QComboBox;
        for (int b : renderChoice::depths()) m_depth->addItem(renderChoice::depthName(b), b);
        m_depth->setCurrentIndex(std::max(0, m_depth->findData(d.depth)));
        m_mipmaps = new FlagBox(QStringLiteral("Smooth pictures drawn smaller (mipmaps)"));
        m_mipmaps->setChecked(d.mipmaps > 0);
        rf->addRow(new ResetLabel(QStringLiteral("Frame rate"), [this] { m_rate->setCurrentIndex(0); }), m_rate);
        rf->addRow(new ResetLabel(QStringLiteral("Antialiasing"), [this] { m_samples->setCurrentIndex(2); }), m_samples);
        rf->addRow(new ResetLabel(QStringLiteral("Internal precision"), [this] { m_depth->setCurrentIndex(0); }), m_depth);
        rf->addRow(note(QStringLiteral("16 bits (default): composition, ISF and effect render targets in RGBA16, so "
                                       "generated gradients are not cut to 256 shades inside NYXMAP. 8 bits: half the GPU "
                                       "memory and bandwidth, for heavy 4K / 8K shows. Displays, media decoding and "
                                       "network outputs can still be 8-bit: no HDR or 10-bit output signal is implied.")));
        rf->addRow(m_mipmaps);
        auto *shareLate = new FlagBox(QStringLiteral("Syphon / Spout one frame later, without waiting for the GPU"));
        shareLate->setChecked(gpuShareOneFrameLate());
        shareLate->setToolTip(QStringLiteral("On (default): publish the prior completed picture without glFinish "
                                             "using a third viewport target. Off: wait for the current frame to "
                                             "finish on the GPU before sharing it."));
        rf->addRow(shareLate);
        connect(shareLate, &QCheckBox::toggled, this, [this](bool on) {
            QSettings().setValue(kShareLateKey, on);
            emit renderDefaultsChanged();
        });
        rf->addRow(note(QStringLiteral("For every project opened on this machine. Screen refresh: "
                                       "the outputs' vertical sync, or the main screen's rate. Antialiasing smooths the "
                                       "edges of the mapped layers; mipmaps the pictures drawn much smaller than they are.")));
        renderPage->addWidget(box);
        auto save = [this] {
            QSettings s;
            s.setValue(kRateKey, m_rate->currentData().toDouble());
            s.setValue(kSamplesKey, m_samples->currentData().toInt());
            s.setValue(kMipmapsKey, m_mipmaps->isChecked());
            s.setValue(kDepthKey, m_depth->currentData().toInt());
            emit renderDefaultsChanged();
        };
        connect(m_rate, qOverload<int>(&QComboBox::activated), this, save);
        connect(m_samples, qOverload<int>(&QComboBox::activated), this, save);
        connect(m_depth, qOverload<int>(&QComboBox::activated), this, save);
        connect(m_mipmaps, &QCheckBox::toggled, this, save);
    }

    // Accent color of the interface
    auto *look = new QGroupBox(QStringLiteral("Interface"));
    auto *lookForm = new QFormLayout(look);
    auto *swatch = new QPushButton;
    swatch->setFixedSize(60, 22);
    swatch->setToolTip(QStringLiteral("Color of the selection, the bars and the controls that are on"));
    auto paintSwatch = [swatch] {
        swatch->setStyleSheet(QStringLiteral("background:%1; border:1px solid #55555c; border-radius:3px;")
                                  .arg(theme::css()));
    };
    paintSwatch();
    lookForm->addRow(new ResetLabel(QStringLiteral("Accent color"),
                                    [paintSwatch] { theme::setAccent(theme::defaultAccent()); paintSwatch(); }),
                     swatch);
    lookForm->addRow(note(QStringLiteral("Used for the selected layer, the bars, the buttons that are on and the "
                                         "links. A click on the name puts it back to the default light grey.")));
    // Magnetism: its state when NyxMap starts (the button next to the zoom changes it meanwhile), its reach
    auto *magnetOn = new FlagBox(QStringLiteral("Magnetism on at start"));
    magnetOn->setChecked(magnet::enabledAtStart());
    auto *reach = new IntBox;
    reach->setRange(2, 40);
    reach->setSuffix(QStringLiteral(" px"));
    reach->setValue(magnet::distance());
    lookForm->addRow(magnetOn);
    lookForm->addRow(new ResetLabel(QStringLiteral("Magnet distance"), [reach] { reach->setValue(8); }), reach);
    lookForm->addRow(note(QStringLiteral("Dragged points, layers and viewports are caught by the edges, centers and corners "
                                         "within this distance on screen, and the bars by their notable values. The "
                                         "magnet button next to the zoom turns it on or off; Ctrl / ⌘ held while dragging "
                                         "moves freely.")));
    connect(magnetOn, &QCheckBox::toggled, this, [](bool on) {
        magnet::setEnabledAtStart(on);
        magnet::setEnabled(on);
    });
    connect(reach, qOverload<int>(&QSpinBox::valueChanged), this, [](int px) { magnet::setDistance(px); });
    auto *follow = new FlagBox(QStringLiteral("Fields follow the values during snapshot fades"));
    follow->setChecked(followFades());
    lookForm->addRow(follow);
    lookForm->addRow(note(QStringLiteral("On: the inspector's sliders and fields (opacity, viewports, color, "
                                         "position, volume, speed, ISF parameters…) move with the values while a snapshot "
                                         "fades them. Off: they stay still and show the snapshot's values once the fade is "
                                         "over. A field being edited is never moved.")));
    connect(follow, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(kFollowKey, on);
        s_followFades = on ? 1 : 0;
    });
    connect(swatch, &QPushButton::clicked, this, [this, paintSwatch] {
        const QColor c = ColorPicker::getColor(theme::accent(), this, QStringLiteral("Accent color"),
                                               QStringLiteral("ui/colorPickerMode/settings/accent"), false);
        if (!c.isValid()) return;
        theme::setAccent(c);
        paintSwatch();
    });
    interfacePage->addWidget(look);

    { // At launch: an empty project, or a project of one's own used as a template
        auto *box = new QGroupBox(QStringLiteral("Startup"));
        auto *sf = new QFormLayout(box);
        auto *mode = new QComboBox;
        mode->addItem(QStringLiteral("Empty project"), false);
        mode->addItem(QStringLiteral("Startup project"), true);
        auto *path = new QLabel;
        path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        path->setWordWrap(true);
        auto *choose = new QPushButton(QStringLiteral("Choose…"));
        auto *useCurrent = new QPushButton(QStringLiteral("Use Current Project"));
        auto *buttons = new QHBoxLayout;
        buttons->addWidget(choose);
        buttons->addWidget(useCurrent);
        buttons->addStretch(1);
        const auto showPath = [path, mode, choose, useCurrent] {
            const QString p = QSettings().value(kStartupPathKey).toString();
            const bool on = mode->currentData().toBool();
            path->setText(p.isEmpty() ? QStringLiteral("(none chosen)")
                                      : QFileInfo::exists(p) ? p : p + QStringLiteral("  — not found"));
            path->setEnabled(on);
            choose->setEnabled(on);
            useCurrent->setEnabled(on);
        };
        {
            const QSignalBlocker b(mode);
            mode->setCurrentIndex(QSettings().value(kStartupUseKey, false).toBool() ? 1 : 0);
        }
        sf->addRow(new ResetLabel(QStringLiteral("At launch"), [mode] { mode->setCurrentIndex(0); }), mode);
        sf->addRow(QStringLiteral("Project"), path);
        sf->addRow(QString(), buttons);
        sf->addRow(note(QStringLiteral("Opened when NyxMap starts, instead of an empty project. It is used as a "
                                       "template: it opens untitled, \"Save\" asks where to save, and the startup "
                                       "file itself is never written over. Its startup sequence (Composition), if it has one, plays. A project given to open (double-click, "
                                       "command line) or a session to recover comes first.")));
        connect(mode, &QComboBox::currentIndexChanged, this, [mode, showPath] {
            QSettings().setValue(kStartupUseKey, mode->currentData().toBool());
            showPath();
        });
        connect(choose, &QPushButton::clicked, this, [this, showPath] {
            QSettings s;
            const QString cur = s.value(kStartupPathKey).toString();
            const QString f = QFileDialog::getOpenFileName(
                this, QStringLiteral("Startup Project"),
                cur.isEmpty() ? s.value("dirs/project").toString() : QFileInfo(cur).absolutePath(),
                QStringLiteral("NyxMap Projects (*.nyxmap *.json)"));
            if (f.isEmpty()) return;
            s.setValue(kStartupPathKey, QFileInfo(f).absoluteFilePath());
            showPath();
        });
        connect(useCurrent, &QPushButton::clicked, this, [this, showPath, path] {
            const QString p = m_engine->projectPath();
            if (p.isEmpty()) { // untitled: nothing on disk to open at launch yet
                path->setText(QStringLiteral("The current project is not saved yet: save it first."));
                return;
            }
            QSettings().setValue(kStartupPathKey, QFileInfo(p).absoluteFilePath());
            showPath();
        });
        showPath();
        interfacePage->addWidget(box);
    }

    auto *color = new QGroupBox(QStringLiteral("Color"));
    auto *cf = new QFormLayout(color);
    m_colorModels = new QComboBox;
    m_colorModels->setObjectName(QStringLiteral("defaultColorPickerView"));
    m_colorModels->addItem(QStringLiteral("HSL"), int(ColorPicker::HSL));
    m_colorModels->addItem(QStringLiteral("RGB"), int(ColorPicker::RGB));
    m_colorModels->addItem(QStringLiteral("HSV"), int(ColorPicker::HSV));
    m_colorModels->addItem(QStringLiteral("Wheel"), int(ColorPicker::Wheel));
    m_colorModels->addItem(QStringLiteral("Palette"), int(ColorPicker::Palette));
    m_colorModels->setCurrentIndex(m_colorModels->findData(int(ColorPicker::defaultView())));
    cf->addRow(new ResetLabel(QStringLiteral("Color widgets"), [this] {
        m_colorModels->setCurrentIndex(m_colorModels->findData(int(ColorPicker::HSL)));
    }), m_colorModels);
    cf->addRow(note(QStringLiteral("Initial view for every color picker. Each location remembers its own view "
                                   "once you choose one there. Locations without a choice follow this setting.")));
    interfacePage->addWidget(color);
    connect(m_colorModels, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        ColorPicker::setDefaultView(ColorPicker::View(m_colorModels->currentData().toInt()));
    });

    auto *osc = new QGroupBox(QStringLiteral("OSC"));
    auto *of = new QFormLayout(osc);
    m_osc = new FlagBox(QStringLiteral("Control by OSC and publish the namespace (OSCQuery)"));
    m_osc->setChecked(oscEnabled());
    m_oscPort = new IntBox;
    m_queryPort = new IntBox;
    for (QSpinBox *sb : {m_oscPort, m_queryPort}) {
        sb->setRange(1024, 65535);
        sb->setKeyboardTracking(false);
    }
    m_oscPort->setValue(oscPort());
    m_queryPort->setValue(oscQueryPort());
    of->addRow(m_osc);
    of->addRow(new ResetLabel(QStringLiteral("OSC port (UDP)"), [this] { m_oscPort->setValue(kDefaultOscPort); }), m_oscPort);
    of->addRow(new ResetLabel(QStringLiteral("OSCQuery port"), [this] { m_queryPort->setValue(kDefaultQueryPort); }),
               m_queryPort);
    // Where OSC and OSCQuery listen: every network interface, this computer only, or one interface (a show network)
    m_oscListen = new QComboBox;
    m_oscListen->addItem(QStringLiteral("All network interfaces"), QString());
    m_oscListen->addItem(QStringLiteral("This computer only"), QStringLiteral("127.0.0.1"));
    for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
        if (!(nif.flags() & QNetworkInterface::IsUp) || (nif.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry &e : nif.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                m_oscListen->addItem(QStringLiteral("%1 · %2").arg(nif.humanReadableName(), e.ip().toString()), e.ip().toString());
    }
    {
        const QString saved = oscListen();
        int i = m_oscListen->findData(saved);
        if (i < 0) { // an interface that is not up now: kept as chosen
            m_oscListen->addItem(QStringLiteral("%1 (not connected)").arg(saved), saved);
            i = m_oscListen->count() - 1;
        }
        m_oscListen->setCurrentIndex(i);
    }
    of->addRow(new ResetLabel(QStringLiteral("Listen on"), [this] { m_oscListen->setCurrentIndex(0); }), m_oscListen);
    m_oscStatus = note(QString());
    of->addRow(m_oscStatus);
    of->addRow(note(QStringLiteral(
        "Announced by zeroconf (_oscjson._tcp, _osc._udp): OSCQuery clients (score, Chataigne, Vezér…) find it "
        "by themselves. Addresses: /composition/…, /layer/&lt;name&gt;/… "
        "(groups: /layer/&lt;group&gt;/layer/&lt;name&gt;/…). The whole tree: http://&lt;this machine&gt;:&lt;OSCQuery port&gt;/<br>"
        "No password: any machine on a network listened on can control the show. On a shared network, listen on the "
        "show's interface only, or on this computer only.")));
    oscPage->addWidget(osc);
    // The phone remote: the controls (Controls tab) as a page for a phone or a tablet on the same network
    {
        auto *box = new QGroupBox(QStringLiteral("Phone remote"));
        auto *rf = new QFormLayout(box);
        m_remote = new FlagBox(QStringLiteral("Serve the Controls as a web page for a phone or a tablet"));
        m_remote->setObjectName(QStringLiteral("remoteEnabled"));
        m_remote->setChecked(remoteEnabled());
        m_remotePort = new IntBox;
        m_remotePort->setRange(1024, 65535);
        m_remotePort->setKeyboardTracking(false);
        m_remotePort->setValue(remotePort());
        m_remotePinUsed = new FlagBox(QStringLiteral("Ask a code (PIN) on the phone"));
        m_remotePinUsed->setObjectName(QStringLiteral("remotePinRequired"));
        m_remotePinUsed->setChecked(remotePinRequired());
        {
            QSettings s;
            QString saved = s.value(kRemotePinKey).toString();
            if (!RemoteServer::pinValid(saved)) {
                saved = RemoteServer::makePin(6);
                s.setValue(kRemotePinKey, saved);
            }
            m_remotePin = new QLineEdit(saved);
        }
        m_remotePin->setObjectName(QStringLiteral("remotePin"));
        m_remotePin->setMaxLength(32);
        m_remotePin->setPlaceholderText(QStringLiteral("4 to 32 letters or digits"));
        m_remotePin->setToolTip(QStringLiteral("The code you type on the phone. Choose your own, or use Random."));
        auto *newPin = new QPushButton(QStringLiteral("Random"));
        auto *pinRow = new QWidget;
        auto *pinLayout = new QHBoxLayout(pinRow);
        pinLayout->setContentsMargins(0, 0, 0, 0);
        pinLayout->addWidget(m_remotePin, 1);
        pinLayout->addWidget(newPin);
        rf->addRow(m_remote);
        rf->addRow(new ResetLabel(QStringLiteral("Port"), [this] { m_remotePort->setValue(kDefaultRemotePort); }), m_remotePort);
        rf->addRow(m_remotePinUsed);
        rf->addRow(QStringLiteral("Code"), pinRow);
        m_remoteStatus = note(QString());
        m_remoteStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rf->addRow(m_remoteStatus);
        rf->addRow(note(QStringLiteral(
            "Open the address on the phone, enter the PIN: sliders, switches, selectors, hold buttons and color pickers for the "
            "controls made in the Controls tab, moving live both ways. It reaches the controls only, not the layers or the project. "
            "Eight wrong codes lock an address out for a minute. Without a code, anyone on the network can move the controls. "
            "No encryption: use it on the show's network, not a shared one. It listens where OSC does (Listen on, above).")));
        oscPage->addWidget(box);
        auto apply = [this] {
            QSettings s;
            s.setValue(kRemoteKey, m_remote->isChecked());
            s.setValue(kRemotePortKey, m_remotePort->value());
            emit remoteChanged();
        };
        connect(m_remote, &QCheckBox::toggled, this, apply);
        connect(m_remotePort, qOverload<int>(&QSpinBox::valueChanged), this, apply);
        // The code typed: kept when it is valid (4 to 32 letters or digits), the phones connected are dropped (the server starts again)
        auto commitPin = [this] {
            const QString typed = m_remotePin->text().trimmed();
            const bool ok = RemoteServer::pinValid(typed);
            m_remotePin->setStyleSheet(ok ? QString() : QStringLiteral("QLineEdit { border:1px solid #e5564a; }"));
            if (!ok) return;
            QSettings().setValue(kRemotePinKey, typed);
            emit remoteChanged();
        };
        connect(m_remotePin, &QLineEdit::editingFinished, this, commitPin);
        connect(newPin, &QPushButton::clicked, this, [this, commitPin] {
            m_remotePin->setText(RemoteServer::makePin(6));
            commitPin();
        });
        connect(m_remotePinUsed, &QCheckBox::toggled, this, [this](bool on) {
            QSettings().setValue(kRemotePinUsedKey, on);
            m_remotePin->setEnabled(on);
            emit remoteChanged();
        });
        m_remotePin->setEnabled(m_remotePinUsed->isChecked());
    }
    oscPage->addWidget(buildFeedback());
    audioPage->addWidget(buildAudio());
    audioPage->addWidget(buildAudioInput());
    isfPage->addWidget(buildIsf());
    for (QVBoxLayout *l : {playbackPage, renderPage, audioPage, interfacePage, oscPage, isfPage}) l->addStretch();
    auto apply = [this] {
        QSettings s;
        s.setValue(kOscKey, m_osc->isChecked());
        s.setValue(kOscPortKey, m_oscPort->value());
        s.setValue(kQueryPortKey, m_queryPort->value());
        s.setValue(kOscListenKey, m_oscListen->currentData().toString());
        emit oscChanged();
    };
    connect(m_osc, &QCheckBox::toggled, this, apply);
    connect(m_oscPort, qOverload<int>(&QSpinBox::valueChanged), this, apply);
    connect(m_queryPort, qOverload<int>(&QSpinBox::valueChanged), this, apply);
    connect(m_oscListen, qOverload<int>(&QComboBox::currentIndexChanged), this, apply);
}

// Sequence feedback: the state of the sequence sent by OSC to the addresses listed, for a remote interface
QWidget *SettingsPanel::buildFeedback()
{
    auto *box = new QGroupBox(QStringLiteral("Sequence feedback"));
    auto *v = new QVBoxLayout(box);
    m_feedback = new FlagBox(QStringLiteral("Send the sequence's state by OSC at each cue (remote interface)"));
    m_feedback->setChecked(feedbackEnabled());
    v->addWidget(m_feedback);
    m_feedbackTargets = new QTableWidget(0, 2);
    m_feedbackTargets->setObjectName(QStringLiteral("feedbackTargets"));
    m_feedbackTargets->setHorizontalHeaderLabels({QStringLiteral("Address (IP or name)"), QStringLiteral("Port")});
    m_feedbackTargets->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_feedbackTargets->setColumnWidth(1, 80);
    m_feedbackTargets->verticalHeader()->setVisible(false);
    m_feedbackTargets->setMaximumHeight(130);
    m_feedbackTargets->setToolTip(QStringLiteral("Double-click to edit. Each address receives every message."));
    for (const QString &t : feedbackTargets()) {
        QString host;
        quint16 port = 0;
        if (!SequenceFeedback::parseTarget(t, &host, &port)) continue;
        const int r = m_feedbackTargets->rowCount();
        m_feedbackTargets->insertRow(r);
        m_feedbackTargets->setItem(r, 0, new QTableWidgetItem(host));
        m_feedbackTargets->setItem(r, 1, new QTableWidgetItem(QString::number(port)));
    }
    v->addWidget(m_feedbackTargets);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("+ Address"));
    auto *remove = new QPushButton(QStringLiteral("− Remove"));
    auto *reset = new QPushButton(QStringLiteral("Default"));
    reset->setToolTip(QStringLiteral("This computer, port 11111"));
    for (QPushButton *b : {add, remove, reset}) buttons->addWidget(b);
    buttons->addStretch();
    v->addLayout(buttons);
    v->addWidget(note(QStringLiteral(
        "For Bitfocus Companion, TouchOSC or an iPhone OSC app. Sent: /sequence/name, /sequence/running, "
        "/sequence/go_enabled… and for /sequence/cue/previous, /sequence/cue/current and /sequence/cue/next: number, "
        "name, label, text, description, prewait, action, postwait, continue, phase, elapsed, remaining (and their "
        "_text), progress, percent, phase_progress, phase_percent. At each cue, ten times a second while a time goes "
        "by (only what changed), and everything every 2 s.<br>"
        "Commands, to the OSC port above: /sequence/go, /sequence/back, /sequence/stop, /sequence/stop_all, "
        "/sequence/step &lt;n&gt;.")));

    auto apply = [this] {
        QStringList targets;
        for (int r = 0; r < m_feedbackTargets->rowCount(); ++r) {
            const QString host = m_feedbackTargets->item(r, 0) ? m_feedbackTargets->item(r, 0)->text().trimmed() : QString();
            const QString port = m_feedbackTargets->item(r, 1) ? m_feedbackTargets->item(r, 1)->text().trimmed() : QString();
            const QString t = (host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(host) : host) + QLatin1Char(':') + port;
            if (SequenceFeedback::parseTarget(t, nullptr, nullptr)) targets << t;
        }
        QSettings s;
        s.setValue(kFeedbackKey, m_feedback->isChecked());
        s.setValue(kFeedbackTargetsKey, targets);
        emit feedbackChanged();
    };
    connect(m_feedback, &QCheckBox::toggled, this, apply);
    connect(m_feedbackTargets, &QTableWidget::itemChanged, this, apply);
    connect(add, &QPushButton::clicked, this, [this, apply] {
        const int r = m_feedbackTargets->rowCount();
        {
            QSignalBlocker b(m_feedbackTargets);
            m_feedbackTargets->insertRow(r);
            m_feedbackTargets->setItem(r, 0, new QTableWidgetItem(QStringLiteral("127.0.0.1")));
            m_feedbackTargets->setItem(r, 1, new QTableWidgetItem(QStringLiteral("11111")));
        }
        apply();
        m_feedbackTargets->editItem(m_feedbackTargets->item(r, 0));
    });
    connect(remove, &QPushButton::clicked, this, [this, apply] {
        const int r = m_feedbackTargets->currentRow();
        if (r < 0) return;
        m_feedbackTargets->removeRow(r);
        apply();
    });
    connect(reset, &QPushButton::clicked, this, [this, apply] {
        {
            QSignalBlocker b(m_feedbackTargets);
            m_feedbackTargets->setRowCount(0);
            m_feedbackTargets->insertRow(0);
            m_feedbackTargets->setItem(0, 0, new QTableWidgetItem(QStringLiteral("127.0.0.1")));
            m_feedbackTargets->setItem(0, 1, new QTableWidgetItem(QStringLiteral("11111")));
        }
        apply();
    });
    return box;
}

// Audio: the sound card of this machine (the volume is the composition's)
QWidget *SettingsPanel::buildAudio()
{
    auto *g = new QGroupBox(QStringLiteral("Audio Output"));
    auto *v = new QVBoxLayout(g);
    auto *devRow = new QHBoxLayout;
    m_audioDevice = new QComboBox;
    m_audioDevice->setToolTip(QStringLiteral("Sound card or audio interface used for the sound of every layer"));
    m_audioDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_audioDevice->setMinimumContentsLength(8);
    auto *rescan = new QToolButton;
    rescan->setText(QStringLiteral("⟳"));
    rescan->setToolTip(QStringLiteral("Refresh the list of audio devices"));
    devRow->addWidget(new ResetLabel(QStringLiteral("Device"), [this] {
        m_audioDevice->setCurrentIndex(0); // the system's default
        emit m_audioDevice->activated(0);
    }));
    devRow->addWidget(m_audioDevice, 1);
    devRow->addWidget(rescan);
    v->addLayout(devRow);
    m_audioState = note(QString());
    v->addWidget(m_audioState);
    v->addWidget(note(QStringLiteral("Saved on this machine. The volume and the blackout are in the Composition tab.")));
    connect(rescan, &QToolButton::clicked, this, [this] { fillAudioDevices(); });
    connect(m_audioDevice, qOverload<int>(&QComboBox::activated), this, [this](int) {
        const QString name = m_audioDevice->currentData().toString();
        QSettings().setValue("audio/device", name);
        openAudioDevice(name);
    });
    return g;
}

void SettingsPanel::fillAudioDevices()
{
    const QString saved = QSettings().value("audio/device").toString();
    QSignalBlocker b(m_audioDevice);
    m_audioDevice->clear();
    m_audioDevice->addItem(QStringLiteral("System default"), QString());
    for (const QString &n : AudioOutput::deviceNames()) m_audioDevice->addItem(n, n);
    int idx = m_audioDevice->findData(saved);
    if (idx < 0 && !saved.isEmpty()) { // saved device currently unplugged: keep it visible
        m_audioDevice->addItem(saved + QStringLiteral(" (not connected)"), saved);
        idx = m_audioDevice->count() - 1;
    }
    m_audioDevice->setCurrentIndex(qMax(0, idx));
}

void SettingsPanel::openAudioDevice(const QString &name)
{
    QString err;
    if (m_engine->startAudio(name, &err)) {
        m_audioState->setText(QStringLiteral("Playing on \"%1\" · %2 kHz · latency %3 ms")
                                  .arg(m_engine->audioOutput().deviceName().toHtmlEscaped())
                                  .arg(AudioOutput::kSampleRate / 1000)
                                  .arg(int(std::lround(m_engine->audioOutput().latency() * 1000))));
        m_audioState->setStyleSheet("color:#888; font-size:11px;");
    } else {
        m_audioState->setText(err.toHtmlEscaped());
        m_audioState->setStyleSheet("color:#ff6b5b; font-size:11px;");
    }
}

void SettingsPanel::startAudio()
{
    fillAudioDevices();
    openAudioDevice(QSettings().value("audio/device").toString());
    fillAudioInputs();
    openAudioInput();
}

// The live input the Audio interactions can listen to (Interactions › Audio › Audio input: Live input)
QWidget *SettingsPanel::buildAudioInput()
{
    auto *g = new QGroupBox(QStringLiteral("Audio Input"));
    auto *v = new QVBoxLayout(g);
    auto *devRow = new QHBoxLayout;
    m_inputDevice = new QComboBox;
    m_inputDevice->setObjectName("audioInputDevice");
    m_inputDevice->setToolTip(QStringLiteral("Sound card input or microphone analysed for the Audio interactions set to Live input"));
    m_inputDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_inputDevice->setMinimumContentsLength(8);
    auto *rescan = new QToolButton;
    rescan->setText(QStringLiteral("⟳"));
    rescan->setToolTip(QStringLiteral("Refresh the list of audio inputs"));
    devRow->addWidget(new ResetLabel(QStringLiteral("Device"), [this] {
        m_inputDevice->setCurrentIndex(0); // off
        emit m_inputDevice->activated(0);
    }));
    devRow->addWidget(m_inputDevice, 1);
    devRow->addWidget(rescan);
    v->addLayout(devRow);
    m_inputLevel = new QProgressBar;
    m_inputLevel->setObjectName("audioInputLevel");
    m_inputLevel->setRange(0, 1000);
    m_inputLevel->setTextVisible(false);
    m_inputLevel->setFixedHeight(6);
    m_inputLevel->setToolTip(QStringLiteral("Level of the live input"));
    v->addWidget(m_inputLevel);
    m_inputState = note(QString());
    v->addWidget(m_inputState);
    v->addWidget(note(QStringLiteral("Its first channel, analysed like the program mix. Saved on this machine.")));
    connect(rescan, &QToolButton::clicked, this, [this] { fillAudioInputs(); });
    connect(m_inputDevice, qOverload<int>(&QComboBox::activated), this, [this](int) {
        const QVariant d = m_inputDevice->currentData();
        QSettings s;
        s.setValue("audio/input_on", d.isValid());
        if (d.isValid()) s.setValue("audio/input_device", d.toString());
        openAudioInput();
    });
    auto *timer = new QTimer(g);
    timer->setInterval(40);
    connect(timer, &QTimer::timeout, this, [this] {
        if (!m_inputLevel->isVisible()) return;
        // The peak in dB over 60 dB, so that a quiet microphone still shows
        const float peak = m_engine->audioInput().peak();
        const double db = 20.0 * std::log10(std::max(peak, 1e-6f));
        m_inputLevel->setValue(int(std::lround(std::clamp((db + 60.0) / 60.0, 0.0, 1.0) * 1000)));
    });
    timer->start();
    return g;
}

void SettingsPanel::fillAudioInputs()
{
    QSettings s;
    const bool on = s.value("audio/input_on", false).toBool();
    const QString saved = s.value("audio/input_device").toString();
    QSignalBlocker b(m_inputDevice);
    m_inputDevice->clear();
    m_inputDevice->addItem(QStringLiteral("Off")); // no data
    m_inputDevice->addItem(QStringLiteral("System default"), QString());
    for (const QString &n : AudioOutput::captureDeviceNames()) m_inputDevice->addItem(n, n);
    int idx = 0;
    if (on) {
        idx = m_inputDevice->findData(saved);
        if (idx < 0 && !saved.isEmpty()) { // saved input currently unplugged: keep it visible
            m_inputDevice->addItem(saved + QStringLiteral(" (not connected)"), saved);
            idx = m_inputDevice->count() - 1;
        }
    }
    m_inputDevice->setCurrentIndex(qMax(0, idx));
}

void SettingsPanel::openAudioInput()
{
    QSettings s;
    if (!s.value("audio/input_on", false).toBool()) {
        m_engine->stopAudioInput();
        m_inputState->setText(QStringLiteral("Off: the Audio interactions set to Live input measure silence"));
        m_inputState->setStyleSheet("color:#888; font-size:11px;");
        return;
    }
    QString err;
    if (m_engine->startAudioInput(s.value("audio/input_device").toString(), &err)) {
        m_inputState->setText(QStringLiteral("Listening to \"%1\" · %2 kHz")
                                  .arg(m_engine->audioInput().deviceName().toHtmlEscaped())
                                  .arg(AudioOutput::kSampleRate / 1000));
        m_inputState->setStyleSheet("color:#888; font-size:11px;");
    } else {
        m_inputState->setText(err.toHtmlEscaped());
        m_inputState->setStyleSheet("color:#ff6b5b; font-size:11px;");
    }
}

void SettingsPanel::setOscStatus(const QString &s) { m_oscStatus->setText(s.toHtmlEscaped()); }
void SettingsPanel::setRemoteStatus(const QString &s) { m_remoteStatus->setText(s.toHtmlEscaped()); }

// ISF: the folders the library scans (bundled with the application, the system's, the ones added), and which of the
// system's shaders it shows
QWidget *SettingsPanel::buildIsf()
{
    auto *root = new QWidget;
    auto *v = new QVBoxLayout(root);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    auto *folders = new QGroupBox(QStringLiteral("Shader Folders"));
    auto *fv = new QVBoxLayout(folders);
    m_isfFolders = new QListWidget;
    m_isfFolders->setObjectName(QStringLiteral("isfFolders"));
    m_isfFolders->setSelectionMode(QAbstractItemView::NoSelection);
    m_isfFolders->setMaximumHeight(140);
    m_isfFolders->setToolTip(QStringLiteral("The folders scanned for ISF generators, FX and transitions"));
    fv->addWidget(m_isfFolders);
    auto *row = new QHBoxLayout;
    auto button = [&](const QString &text, const char *name, const QString &tip) {
        auto *b = new QPushButton(text);
        b->setObjectName(QString::fromLatin1(name));
        b->setToolTip(tip);
        row->addWidget(b);
        return b;
    };
    auto *add = button(QStringLiteral("Add Folder…"), "addIsfFolder", QStringLiteral("A folder of your own shaders, scanned with the others"));
    auto *forget = button(QStringLiteral("Forget Added Folders"), "forgetIsfFolders", QStringLiteral("Only the bundled and the system's folders"));
    auto *rescan = button(QStringLiteral("Rescan"), "rescanIsf", QStringLiteral("Read the folders again (shaders added in the Finder)"));
    auto *bundled = button(QStringLiteral("Open Bundled Folder"), "openBundledIsf", QStringLiteral("The shaders that come with NyxMap"));
    row->addStretch();
    fv->addLayout(row);
    v->addWidget(folders);
    auto setFolders = [this](const QStringList &f) {
        m_engine->library().setUserFolders(f);
        QSettings().setValue(QStringLiteral("isf/folders"), f);
        emit isfLibraryChanged(true);
    };
    connect(add, &QPushButton::clicked, this, [this, setFolders] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("ISF Shader Folder"));
        if (d.isEmpty()) return;
        QStringList f = m_engine->library().userFolders();
        if (!f.contains(d)) f << d;
        setFolders(f);
    });
    connect(forget, &QPushButton::clicked, this, [setFolders] { setFolders({}); });
    connect(rescan, &QPushButton::clicked, this, [this] { emit isfLibraryChanged(true); });
    connect(bundled, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(IsfLibrary::bundledFolder())); });

    auto *system = new QGroupBox(QStringLiteral("System Shaders"));
    auto *sv = new QVBoxLayout(system);
    struct SystemOption {
        const char *label, *key, *objectName;
        bool IsfLibrary::SystemVisibility::*field;
    };
    for (const SystemOption &option : {
             SystemOption{"Show Generators", "isf/showSystemGenerators", "showSystemIsfGenerators", &IsfLibrary::SystemVisibility::generators},
             SystemOption{"Show FX", "isf/showSystemFX", "showSystemIsfFX", &IsfLibrary::SystemVisibility::filters},
             SystemOption{"Show Transitions", "isf/showSystemTransitions", "showSystemIsfTransitions", &IsfLibrary::SystemVisibility::transitions}}) {
        auto *box = new FlagBox(QString::fromLatin1(option.label));
        box->setObjectName(QString::fromLatin1(option.objectName));
        box->setChecked(m_engine->library().systemVisibility().*option.field);
        box->setToolTip(QStringLiteral("Include shaders from the standard ISF folders on this computer. "
                                       "Shaders already used by the project remain available."));
        sv->addWidget(box);
        connect(box, &QCheckBox::toggled, this, [this, option](bool on) {
            auto visibility = m_engine->library().systemVisibility();
            visibility.*option.field = on;
            m_engine->library().setSystemVisibility(visibility);
            QSettings().setValue(QString::fromLatin1(option.key), on);
            emit isfLibraryChanged(false);
        });
    }
    v->addWidget(system);
    refreshIsfFolders();
    return root;
}

void SettingsPanel::refreshIsfFolders()
{
    if (!m_isfFolders) return;
    m_isfFolders->clear();
    for (const QString &f : m_engine->library().allFolders()) m_isfFolders->addItem(QDir::toNativeSeparators(f));
}
