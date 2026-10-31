#pragma once
#include "Engine.h"
#include "IsfLibrary.h"
#include "Layer.h"
#include <QWidget>

class QCheckBox;
class QTableWidget;

// Choices of the rendering settings (Settings ▸ Rendering)
namespace renderChoice {
QList<double> frameRates(); // 0: the screen's refresh rate
QString frameRateName(double fps);
QList<int> samples();       // 0: off
QString samplesName(int n);
QList<int> depths();        // bits per channel: 8, 10
QString depthName(int bits);
} // namespace renderChoice
class QComboBox;
class QLabel;
class QSpinBox;

// "Settings" tab, next to Layer and Composition: the preferences of this machine (not saved in the project), in
// sub-tabs (Playback, Rendering, Audio, Interface, OSC), applied as soon as they change.
class SettingsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPanel(Engine *engine, QWidget *parent = nullptr);
    void startAudio(); // opens the sound card saved on this machine (at launch)

    // Mode given to a video or a sound when it is loaded into a layer (Loop unless changed)
    static PlayMode defaultPlayMode();
    // Videos decoded by the graphics hardware when it can (on unless changed)
    static bool hardwareDecoding();
    static bool gpuShareOneFrameLate(); // Syphon / Spout: the frame before, without waiting for the GPU
    // OSC control and OSCQuery publication (defaults of libossia / score: OSC 1234, OSCQuery 5678)
    static bool oscEnabled();
    // The phone remote (RemoteServer.h): off until asked, a port, a PIN made on first use
    static bool remoteEnabled();
    static int remotePort();
    static bool remotePinRequired(); // false: no code is asked
    static QString remotePin();     // empty when none is asked
    static int oscPort();      // UDP, OSC messages
    static int oscQueryPort(); // TCP, OSCQuery (HTTP + WebSocket)
    static QString oscListen(); // the address OSC and OSCQuery listen on ("": every network interface)
    // The sequence's state sent by OSC to these "host:port" (on unless changed; 127.0.0.1:11111 by default)
    static bool feedbackEnabled();
    static QStringList feedbackTargets();

    // The inspector's fields move with the values while a snapshot fades them (on unless changed); off, they
    // stay still and show the snapshot's values once the fade is over
    static bool followFades();

    // The project opened at launch instead of an empty one, as a template (untitled): "" when none is chosen or
    // "Empty project" is selected
    static QString startupProject();

    void setOscStatus(const QString &s);
    void setRemoteStatus(const QString &s);
    // Transition used when a snapshot gives a layer another source, for the layers that do not choose one:
    // the path saved on this machine (Crossfade by default), found again by its file name in the library
    static QString defaultTransition(const IsfLibrary &library);
    void setTransitions(const QVector<IsfEntry> &transitions, const QString &current); // the library's
    void refreshIsfFolders(); // the ISF tab's list: the folders the library scans now
    // Frame rate, antialiasing, mipmaps and color depth, for every project
    static Engine::RenderSettings renderDefaults();

signals:
    void playModeChanged();
    void hardwareDecodingChanged();
    void oscChanged();
    void remoteChanged();
    void feedbackChanged();
    void transitionChanged(const QString &path);
    void renderDefaultsChanged();
    void isfLibraryChanged(bool rescan); // its folders (scanned again: rescan) or what it shows of the system's
private:
    QWidget *buildIsf();
    class QListWidget *m_isfFolders = nullptr;
    QWidget *buildAudio();
    QWidget *buildFeedback();
    void fillAudioDevices();
    void openAudioDevice(const QString &name);
    QWidget *buildAudioInput();
    void fillAudioInputs();
    void openAudioInput(); // the one saved (or none)

    Engine *m_engine;
    QComboBox *m_audioDevice = nullptr;
    QLabel *m_audioState = nullptr;
    QComboBox *m_inputDevice = nullptr; // Off, System default, the inputs
    QLabel *m_inputState = nullptr;
    class QProgressBar *m_inputLevel = nullptr;
    QComboBox *m_playMode = nullptr, *m_colorModels = nullptr, *m_transition = nullptr, *m_rate = nullptr,
              *m_samples = nullptr, *m_depth = nullptr;
    QCheckBox *m_mipmaps = nullptr, *m_hardware = nullptr;
    QCheckBox *m_osc = nullptr, *m_feedback = nullptr;
    QTableWidget *m_feedbackTargets = nullptr;
    QSpinBox *m_oscPort = nullptr, *m_queryPort = nullptr;
    QLabel *m_oscStatus = nullptr, *m_remoteStatus = nullptr;
    QCheckBox *m_remote = nullptr, *m_remotePinUsed = nullptr;
    QSpinBox *m_remotePort = nullptr;
    class QLineEdit *m_remotePin = nullptr;
    class QComboBox *m_oscListen = nullptr;
};
