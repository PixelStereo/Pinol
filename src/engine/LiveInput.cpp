// Live inputs (LiveInput.h): cameras, screens and windows through Qt Multimedia, NDI and OMT through their libraries
// loaded at run time, DeckLink through its driver (DeckLink.cpp), Syphon in LiveInputSyphon.mm, Spout in
// LiveInputSpout.cpp.
#include "LiveInput.h"
#include "DeckLink.h"
#include "NetVideo.h"
#include "Permissions.h"

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QMetaObject>
#include <QMutex>
#include "AnnotatedMutex.h"
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <thread>

#ifdef NYXMAP_HAS_CAMERA
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#include <QScreen>
#include <QScreenCapture>
#define NYXMAP_HAS_SCREEN_CAPTURE // Qt 6.5: the screens
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
#include <QCapturableWindow>
#include <QWindowCapture>
#define NYXMAP_HAS_WINDOW_CAPTURE // Qt 6.6: the other applications' windows
#endif
#endif

#ifdef NYXMAP_HAS_SPOUT
#include "SpoutInput.h"
#endif

#ifdef NYXMAP_HAS_SYPHON
std::vector<LiveInputInfo> listSyphonInputs();
std::unique_ptr<LiveInput> createSyphonInput(const QString &name, QString *err);
#endif

namespace {

QString part(const QString &uri, int k) { return uri.section(QLatin1Char(':'), k, k); }
QString rest(const QString &uri) { return uri.section(QLatin1Char(':'), 2); } // after "input:<kind>:"

// The newest frame, from a thread of its own, taken by the render thread
class LatestFrame
{
public:
    // `rows` of `stride` bytes, from the top row down; bgra: blue first (swapped to RGBA here). Stored bottom row first,
    // as the still images and the other textures are (Texture2D expects it), so a live picture is not upside down.
    void put(const uint8_t *data, int w, int h, int stride, bool bgra)
    {
        if (!data || w <= 0 || h <= 0) return;
        QMutexLocker lk(&m_mutex);
        m_rgba.resize(qsizetype(w) * h * 4);
        uchar *dst = reinterpret_cast<uchar *>(m_rgba.data());
        for (int y = 0; y < h; ++y) {
            const uint8_t *s = data + qsizetype(y) * stride;
            uchar *d = dst + qsizetype(h - 1 - y) * w * 4;
            if (!bgra) {
                std::memcpy(d, s, size_t(w) * 4);
            } else {
                for (int x = 0; x < w; ++x, s += 4, d += 4) {
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    d[3] = s[3];
                }
            }
        }
        m_w = w;
        m_h = h;
        m_fresh = true;
    }
    bool take(Texture2D &tex)
    {
        QMutexLocker lk(&m_mutex);
        if (!m_fresh) return false;
        m_fresh = false;
        tex.upload(m_rgba.constData(), m_w, m_h);
        return true;
    }

private:
    Annotated<QMutex> m_mutex; // the camera thread writes, the render thread reads
    QByteArray m_rgba;
    int m_w = 0, m_h = 0;
    bool m_fresh = false;
};

// --- A solid color (tests)
class TestInput : public LiveInput
{
public:
    explicit TestInput(const QColor &c)
    {
        QImage img(64, 36, QImage::Format_RGBA8888);
        img.fill(c);
        m_frame.put(img.constBits(), img.width(), img.height(), int(img.bytesPerLine()), false);
    }
    bool update(Texture2D &tex) override { return m_frame.take(tex); }

private:
    LatestFrame m_frame;
};

// --- NDI
class NdiInput : public LiveInput
{
public:
    NdiInput(const ndi::Lib *lib, const QString &name) : m_lib(lib), m_name(name.toUtf8())
    {
        m_thread = std::thread([this] { run(); });
    }
    ~NdiInput() override
    {
        m_quit = true;
        if (m_thread.joinable()) m_thread.join();
    }
    bool update(Texture2D &tex) override { return m_frame.take(tex); }
    QString status() const override { return m_connected ? QString() : QStringLiteral("Waiting for the NDI source"); }

private:
    void run()
    {
        ndi::RecvCreateV3 c{};
        c.source_to_connect_to.p_ndi_name = m_name.constData();
        c.color_format = ndi::kRecvRGBX_RGBA;
        c.bandwidth = ndi::kRecvBandwidthHighest;
        c.allow_video_fields = false;
        c.p_ndi_recv_name = "NyxMap";
        void *recv = m_lib->recv_create_v3(&c);
        if (!recv) return;
        while (!m_quit) {
            ndi::VideoFrameV2 v{};
            if (m_lib->recv_capture_v2(recv, &v, nullptr, nullptr, 100) != ndi::kFrameVideo) continue;
            m_connected = true;
            const bool bgra = v.FourCC == ndi::kBGRA || v.FourCC == ndi::fourcc('B', 'G', 'R', 'X');
            if (v.FourCC == ndi::kRGBA || v.FourCC == ndi::kRGBX || bgra)
                m_frame.put(v.p_data, v.xres, v.yres, v.line_stride_in_bytes, bgra);
            m_lib->recv_free_video_v2(recv, &v);
        }
        m_lib->recv_destroy(recv);
    }
    const ndi::Lib *m_lib;
    QByteArray m_name;
    LatestFrame m_frame;
    std::atomic<bool> m_quit{false}, m_connected{false};
    std::thread m_thread;
};

// The NDI sources on the network: one finder for the program's life (it keeps listening)
std::vector<LiveInputInfo> ndiSources(const QString &folder)
{
    std::vector<LiveInputInfo> out;
    const ndi::Lib &lib = ndi::lib(folder);
    if (!lib.canReceive()) return out;
    static void *finder = nullptr;
    static QMutex mutex;
    QMutexLocker lk(&mutex);
    if (!finder) {
        const ndi::FindCreate f{true, nullptr, nullptr};
        finder = lib.find_create_v2(&f);
        if (!finder) return out;
    }
    uint32_t n = 0;
    const ndi::Source *s = lib.find_get_current_sources(finder, &n);
    for (uint32_t k = 0; s && k < n; ++k) {
        const QString name = QString::fromUtf8(s[k].p_ndi_name);
        if (!name.isEmpty()) out.push_back({QStringLiteral("input:ndi:") + name, name, QStringLiteral("NDI")});
    }
    return out;
}

// --- OMT
class OmtInput : public LiveInput
{
public:
    OmtInput(const omt::Lib *lib, const QString &address) : m_lib(lib), m_address(address.toUtf8())
    {
        m_thread = std::thread([this] { run(); });
    }
    ~OmtInput() override
    {
        m_quit = true;
        if (m_thread.joinable()) m_thread.join();
    }
    bool update(Texture2D &tex) override { return m_frame.take(tex); }
    QString status() const override { return m_connected ? QString() : QStringLiteral("Waiting for the OMT source"); }

private:
    void run()
    {
        void *recv = m_lib->receive_create(m_address.constData(), omt::kVideo, omt::kPreferBGRA, omt::kReceiveDefault);
        if (!recv) return;
        while (!m_quit) {
            const omt::MediaFrame *f = m_lib->receive(recv, omt::kVideo, 100);
            if (!f || f->Type != omt::kVideo || f->Codec != omt::kBGRA) continue;
            m_connected = true;
            m_frame.put(static_cast<const uint8_t *>(f->Data), f->Width, f->Height, f->Stride, true);
        }
        m_lib->receive_destroy(recv);
    }
    const omt::Lib *m_lib;
    QByteArray m_address;
    LatestFrame m_frame;
    std::atomic<bool> m_quit{false}, m_connected{false};
    std::thread m_thread;
};

std::vector<LiveInputInfo> omtSources(const QString &folder)
{
    std::vector<LiveInputInfo> out;
#if defined(Q_OS_LINUX) // libomt's discovery needs Avahi, and aborts without it
    if (!QFile::exists(QStringLiteral("/run/avahi-daemon/socket")) && !QFile::exists(QStringLiteral("/var/run/avahi-daemon/socket")))
        return out;
#endif
    const omt::Lib &lib = omt::lib(folder);
    if (!lib.canReceive()) return out;
    int n = 0;
    char **a = lib.discovery_getaddresses(&n);
    for (int k = 0; a && k < n; ++k) {
        if (!a[k]) continue;
        const QString address = QString::fromUtf8(a[k]);
        out.push_back({QStringLiteral("input:omt:") + address, address, QStringLiteral("OMT")});
    }
    return out;
}

#ifdef NYXMAP_HAS_CAMERA
// The format a camera is opened in. Left to itself, the system may pick a square one (the cameras with Center
// Stage, the iPhone's Continuity Camera offer them): the whole picture is wanted, so a wide format (not square),
// at least 24 fps, as large as possible up to 4K, then the highest rate up to 60.
static QCameraFormat bestCameraFormat(const QCameraDevice &device)
{
    QCameraFormat best;
    double bestScore = -1;
    for (const QCameraFormat &f : device.videoFormats()) {
        const QSize s = f.resolution();
        if (s.isEmpty()) continue;
        const bool wide = double(s.width()) / double(s.height()) > 1.2;
        const double fps = std::min(double(f.maxFrameRate()), 60.0);
        const double area = std::min(double(s.width()) * double(s.height()), 3840.0 * 2160.0);
        const double score = (wide ? 1e15 : 0) + (fps >= 24 ? 1e14 : 0) + area * 100 + fps;
        if (score > bestScore) {
            bestScore = score;
            best = f;
        }
    }
    return best;
}

// --- A camera: Qt Multimedia, in the application's thread; each frame converted to RGBA there
class CameraInput : public LiveInput
{
public:
    explicit CameraInput(const QCameraDevice &device)
    {
        m_holder = new QObject;
        QObject *holder = m_holder;
        auto frame = m_frame;
        auto status = m_status;
        auto start = [holder, device, frame, status] {
            auto *camera = new QCamera(device, holder);
            const QCameraFormat format = bestCameraFormat(device);
            if (!format.isNull()) {
                camera->setCameraFormat(format);
                qInfo("Camera %s: %dx%d, %.0f fps", qPrintable(device.description()), format.resolution().width(),
                      format.resolution().height(), double(format.maxFrameRate()));
            }
            auto *session = new QMediaCaptureSession(holder);
            auto *sink = new QVideoSink(holder);
            session->setCamera(camera);
            session->setVideoSink(sink);
            QObject::connect(sink, &QVideoSink::videoFrameChanged, holder, [frame](const QVideoFrame &f) {
                const QImage img = f.toImage().convertToFormat(QImage::Format_RGBA8888);
                if (!img.isNull()) frame->put(img.constBits(), img.width(), img.height(), int(img.bytesPerLine()), false);
            });
            QObject::connect(camera, &QCamera::errorOccurred, holder, [status](QCamera::Error, const QString &e) {
                QMutexLocker lk(&status->mutex);
                status->text = e;
            });
            camera->start();
        };
        // Its objects live in the application's thread (the one with an event loop)
        QObject *app = QCoreApplication::instance();
        if (app) holder->moveToThread(app->thread());
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (app) {
            QMetaObject::invokeMethod(app, [holder, start, status] {
                QCameraPermission permission;
                switch (qApp->checkPermission(permission)) {
                case Qt::PermissionStatus::Granted: start(); break;
                case Qt::PermissionStatus::Undetermined:
                    qApp->requestPermission(permission, holder, [start, status](const QPermission &p) {
                        if (p.status() == Qt::PermissionStatus::Granted) start();
                        else {
                            QMutexLocker lk(&status->mutex);
                            status->text = QStringLiteral("Camera access refused (System Settings › Privacy)");
                        }
                    });
                    break;
                case Qt::PermissionStatus::Denied: {
                    QMutexLocker lk(&status->mutex);
                    status->text = QStringLiteral("Camera access refused (System Settings › Privacy)");
                    break;
                }
                }
            });
        }
#else
        if (app) QMetaObject::invokeMethod(app, start);
#endif
    }
    ~CameraInput() override { m_holder->deleteLater(); } // stopped and released in its own thread
    bool update(Texture2D &tex) override { return m_frame->take(tex); }
    QString status() const override
    {
        QMutexLocker lk(&m_status->mutex);
        return m_status->text;
    }

private:
    struct Status {
        QMutex mutex;
        QString text;
    };
    QObject *m_holder;
    std::shared_ptr<LatestFrame> m_frame = std::make_shared<LatestFrame>();
    std::shared_ptr<Status> m_status = std::make_shared<Status>();
};
#endif

// --- DeckLink: the card's pictures, in whatever mode comes in; 4:2:2 made RGBA in the card's thread
class DeckLinkInput : public LiveInput
{
public:
    bool open(const QString &port, QString *err)
    {
        return m_in.open(port, [this](const decklink::InputFrame &f) { receive(f); }, err);
    }
    bool update(Texture2D &tex) override { return m_frame.take(tex); }
    QString status() const override
    {
        if (m_in.hasSignal()) return {};
        const QString s = m_in.status();
        return s.endsWith(QLatin1String("no signal")) ? s : s + QStringLiteral(" — waiting for a signal");
    }

private:
    void receive(const decklink::InputFrame &f) // the card's thread
    {
        if (!f.data || f.width <= 0 || f.height <= 0) return; // no signal: the last picture stays
        if (f.format == decklink::InputFrame::Bgra) {
            m_frame.put(f.data, f.width, f.height, f.rowBytes, true);
            return;
        }
        m_rgba.resize(size_t(f.width) * size_t(f.height) * 4);
        if (f.format == decklink::InputFrame::Uyvy) decklink::uyvyToRgba(f.data, f.rowBytes, f.width, f.height, m_rgba.data(), f.width * 4);
        else decklink::v210ToRgba(f.data, f.rowBytes, f.width, f.height, m_rgba.data(), f.width * 4);
        m_frame.put(m_rgba.data(), f.width, f.height, f.width * 4, false);
    }
    LatestFrame m_frame;
    std::vector<uint8_t> m_rgba; // the card's thread only
    decklink::VideoInput m_in;   // last: it goes first, and no frame comes after
};

#ifdef NYXMAP_HAS_SPOUT
// --- Spout: the sender's texture copied on the GPU into the layer's, in the render thread
class SpoutInput : public LiveInput
{
public:
    explicit SpoutInput(const QString &name) : m_name(name), m_rx(createSpoutReceive(name)) {}
    bool update(Texture2D &tex) override
    {
        SpoutReceive::Result r = m_rx->receive(tex.tex, tex.w, tex.h);
        if (r == SpoutReceive::Resize) { // a new sender, or another size: the texture made at its size, then the frame
            if (!tex.tex || tex.w != m_rx->width() || tex.h != m_rx->height()) tex.upload(nullptr, m_rx->width(), m_rx->height());
            r = m_rx->receive(tex.tex, tex.w, tex.h);
        }
        m_found = r != SpoutReceive::Gone;
        if (r == SpoutReceive::Received) m_seen = true;
        return r == SpoutReceive::Received;
    }
    QString status() const override
    {
        if (!m_seen) return QStringLiteral("Waiting for the Spout sender \"%1\"").arg(m_name);
        return m_found ? QString() : QStringLiteral("Spout sender \"%1\" not found — its last picture kept").arg(m_name);
    }

private:
    QString m_name;
    std::unique_ptr<SpoutReceive> m_rx;
    std::atomic<bool> m_found{false}, m_seen{false};
};
#endif

#ifdef NYXMAP_HAS_SCREEN_CAPTURE
const QString kScreenPermission = QStringLiteral("Screen Recording permission needed: System Settings › Privacy & Security");

// The screens, by name (else by place, from 1)
std::vector<std::pair<QString, QScreen *>> screensById()
{
    std::vector<std::pair<QString, QScreen *>> out;
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) return out;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (int k = 0; k < screens.size(); ++k)
        out.push_back({screens[k]->name().isEmpty() ? QString::number(k + 1) : screens[k]->name(), screens[k]});
    return out;
}
#endif

#ifdef NYXMAP_HAS_WINDOW_CAPTURE
// NyxMap's own windows are not offered (a layer showing its own output window: a picture fed back into itself).
// Qt does not say whose a window is: by its title, NyxMap's (in the application's thread, its windows' titles too)
bool isOwnWindow(const QString &title)
{
    if (title.contains(QLatin1String("NyxMap"), Qt::CaseInsensitive)) return true;
    QCoreApplication *app = QCoreApplication::instance();
    if (qobject_cast<QGuiApplication *>(app) && QThread::currentThread() == app->thread())
        for (const QWindow *w : QGuiApplication::topLevelWindows())
            if (w->title() == title) return true;
    return false;
}

// The other applications' windows, by title: the same after a restart (a window's handle is not). Two of the same
// title: the second "<title> #2", in the system's order
std::vector<std::pair<QString, QCapturableWindow>> windowsById()
{
    std::vector<std::pair<QString, QCapturableWindow>> out;
    QHash<QString, int> seen;
    for (const QCapturableWindow &w : QWindowCapture::capturableWindows()) {
        const QString title = w.description().trimmed();
        if (title.isEmpty() || isOwnWindow(title)) continue;
        const int n = ++seen[title];
        out.push_back({n == 1 ? title : QStringLiteral("%1 #%2").arg(title).arg(n), w});
    }
    return out;
}
#endif

#ifdef NYXMAP_HAS_SCREEN_CAPTURE
// --- A screen or a window: Qt Multimedia, in the application's thread, as a camera. Looked for again every few
// seconds while it is not there (a window closed, a screen unplugged), the last picture kept meanwhile
class ScreenInput : public LiveInput
{
public:
    ScreenInput(bool window, const QString &id)
    {
        m_holder = new QObject;
        QObject *holder = m_holder;
        auto frame = m_frame;
        auto status = m_status;
        auto setStatus = [status](const QString &t) {
            QMutexLocker lk(&status->mutex);
            status->text = t;
        };
        auto start = [holder, window, id, frame, setStatus] {
            auto *session = new QMediaCaptureSession(holder);
            auto *sink = new QVideoSink(holder);
            session->setVideoSink(sink);
            QObject::connect(sink, &QVideoSink::videoFrameChanged, holder, [frame, setStatus](const QVideoFrame &f) {
                const QImage img = f.toImage().convertToFormat(QImage::Format_RGBA8888);
                if (img.isNull()) return;
                frame->put(img.constBits(), img.width(), img.height(), int(img.bytesPerLine()), false);
                setStatus(QString());
            });
            // An error: the permission (macOS) when it is missing, else what Qt says
            auto failed = [setStatus](const QString &e) { setStatus(screenCaptureAllowed(false) ? e : kScreenPermission); };
            std::function<bool()> attach; // found (again) and started; false: not there now
            if (!window) {
                auto *capture = new QScreenCapture(holder);
                session->setScreenCapture(capture);
                QObject::connect(capture, &QScreenCapture::errorOccurred, holder,
                                 [failed](QScreenCapture::Error, const QString &e) { failed(e); });
                attach = [capture, id, setStatus] {
                    if (capture->isActive()) return true;
                    for (const auto &s : screensById()) {
                        if (s.first != id) continue;
                        capture->setScreen(s.second);
                        capture->start();
                        return true;
                    }
                    setStatus(QStringLiteral("Screen \"%1\" not found").arg(id));
                    return false;
                };
            } else {
#ifdef NYXMAP_HAS_WINDOW_CAPTURE
                auto *capture = new QWindowCapture(holder);
                session->setWindowCapture(capture);
                QObject::connect(capture, &QWindowCapture::errorOccurred, holder,
                                 [failed](QWindowCapture::Error, const QString &e) { failed(e); });
                attach = [capture, id, setStatus] {
                    if (capture->isActive() && capture->window().isValid()) return true;
                    for (const auto &w : windowsById()) {
                        if (w.first != id) continue;
                        capture->setWindow(w.second);
                        capture->start();
                        return true;
                    }
                    setStatus(QStringLiteral("Window \"%1\" not found").arg(id));
                    return false;
                };
#endif
            }
            if (!attach) return;
            attach();
            auto *again = new QTimer(holder);
            again->setInterval(3000);
            QObject::connect(again, &QTimer::timeout, holder, attach);
            again->start();
        };
        QObject *app = QCoreApplication::instance();
        if (!app) return;
        holder->moveToThread(app->thread());
        QMetaObject::invokeMethod(app, [start, setStatus] {
            // macOS: asked once (the system's prompt); granted, it applies when NyxMap is opened again
            if (!screenCaptureAllowed(true)) setStatus(kScreenPermission);
            else start();
        });
    }
    ~ScreenInput() override { m_holder->deleteLater(); } // stopped and released in its own thread
    bool update(Texture2D &tex) override { return m_frame->take(tex); }
    QString status() const override
    {
        QMutexLocker lk(&m_status->mutex);
        return m_status->text;
    }

private:
    struct Status {
        QMutex mutex;
        QString text;
    };
    QObject *m_holder;
    std::shared_ptr<LatestFrame> m_frame = std::make_shared<LatestFrame>();
    std::shared_ptr<Status> m_status = std::make_shared<Status>();
};
#endif

} // namespace

static const QString kInputsFolder = QStringLiteral("/NyxMap/Inputs/");

QString liveInputPath(const QString &uri) { return kInputsFolder + QString::fromLatin1(QUrl::toPercentEncoding(uri)); }

QString liveInputUriOfPath(const QString &path)
{
    const QString p = QString(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int at = p.indexOf(kInputsFolder);
    if (at < 0) return {};
    const QString uri = QUrl::fromPercentEncoding(p.mid(at + kInputsFolder.size()).toLatin1());
    return isLiveInputUri(uri) ? uri : QString();
}

QString liveInputKind(const QString &uri)
{
    const QString k = part(uri, 1);
    if (k == QLatin1String("camera")) return QStringLiteral("Camera");
    if (k == QLatin1String("ndi")) return QStringLiteral("NDI");
    if (k == QLatin1String("omt")) return QStringLiteral("OMT");
    if (k == QLatin1String("syphon")) return QStringLiteral("Syphon");
    if (k == QLatin1String("spout")) return QStringLiteral("Spout");
    if (k == QLatin1String("decklink")) return QStringLiteral("DeckLink");
    if (k == QLatin1String("screen")) return QStringLiteral("Screen");
    if (k == QLatin1String("window")) return QStringLiteral("Window");
    if (k == QLatin1String("test")) return QStringLiteral("Test");
    if (k == QLatin1String("audio")) return QStringLiteral("Audio");
    return {};
}

QString liveInputName(const QString &uri)
{
#ifdef NYXMAP_HAS_CAMERA
    if (part(uri, 1) == QLatin1String("camera"))
        for (const QCameraDevice &d : QMediaDevices::videoInputs())
            if (QString::fromUtf8(d.id()) == rest(uri)) return d.description();
#endif
    if (part(uri, 1) == QLatin1String("decklink"))
        for (const decklink::Port &p : decklink::inputPorts())
            if (p.id == rest(uri)) return p.name;
    return rest(uri);
}

std::vector<LiveInputInfo> listLiveInputs(const QString &folder)
{
    std::vector<LiveInputInfo> out;
#ifdef NYXMAP_HAS_CAMERA
    for (const QCameraDevice &d : QMediaDevices::videoInputs())
        out.push_back({QStringLiteral("input:camera:") + QString::fromUtf8(d.id()), d.description(), QStringLiteral("Camera")});
#endif
    for (const LiveInputInfo &i : ndiSources(folder)) out.push_back(i);
    for (const LiveInputInfo &i : omtSources(folder)) out.push_back(i);
#ifdef NYXMAP_HAS_SYPHON
    for (const LiveInputInfo &i : listSyphonInputs()) out.push_back(i);
#endif
#ifdef NYXMAP_HAS_SPOUT
    for (const QString &name : spoutSenders()) out.push_back({QStringLiteral("input:spout:") + name, name, QStringLiteral("Spout")});
#endif
    // The ports that take a signal in (looked up once: decklink::rescan looks again); none without the driver
    for (const decklink::Port &p : decklink::inputPorts())
        out.push_back({QStringLiteral("input:decklink:") + p.id, p.name, QStringLiteral("DeckLink")});
#ifdef NYXMAP_HAS_SCREEN_CAPTURE
    for (const auto &s : screensById()) {
        const QSize size = s.second->size() * s.second->devicePixelRatio();
        out.push_back({QStringLiteral("input:screen:") + s.first, QStringLiteral("%1 (%2 × %3)").arg(s.first).arg(size.width()).arg(size.height()),
                       QStringLiteral("Screen")});
    }
#endif
#ifdef NYXMAP_HAS_WINDOW_CAPTURE
    for (const auto &w : windowsById()) out.push_back({QStringLiteral("input:window:") + w.first, w.first, QStringLiteral("Window")});
#endif
    return out;
}

std::unique_ptr<LiveInput> createLiveInput(const QString &uri, const QString &folder, QString *err)
{
    auto fail = [err](const QString &e) -> std::unique_ptr<LiveInput> {
        if (err) *err = e;
        return nullptr;
    };
    const QString kind = part(uri, 1), what = rest(uri);
    if (!isLiveInputUri(uri) || what.isEmpty()) return fail(QStringLiteral("Not an input: ") + uri);
    if (kind == QLatin1String("test")) {
        const QStringList c = what.split(QLatin1Char(','));
        return std::make_unique<TestInput>(QColor(c.value(0).toInt(), c.value(1).toInt(), c.value(2).toInt()));
    }
    if (kind == QLatin1String("ndi")) {
        const ndi::Lib &lib = ndi::lib(folder);
        if (!lib.ok) return fail(lib.error);
        if (!lib.canReceive()) return fail(QStringLiteral("This NDI library cannot receive: ") + lib.path);
        return std::make_unique<NdiInput>(&lib, what);
    }
    if (kind == QLatin1String("omt")) {
        const omt::Lib &lib = omt::lib(folder);
        if (!lib.ok) return fail(lib.error);
        if (!lib.canReceive()) return fail(QStringLiteral("This OMT library cannot receive: ") + lib.path);
        return std::make_unique<OmtInput>(&lib, what);
    }
    if (kind == QLatin1String("camera")) {
#ifdef NYXMAP_HAS_CAMERA
        for (const QCameraDevice &d : QMediaDevices::videoInputs())
            if (QString::fromUtf8(d.id()) == what) return std::make_unique<CameraInput>(d);
        return fail(QStringLiteral("Camera not found: ") + what);
#else
        return fail(QStringLiteral("Cameras: this build has no Qt Multimedia"));
#endif
    }
    if (kind == QLatin1String("syphon")) {
#ifdef NYXMAP_HAS_SYPHON
        return createSyphonInput(what, err);
#else
        return fail(QStringLiteral("Syphon is only available on macOS"));
#endif
    }
    if (kind == QLatin1String("spout")) {
#ifdef NYXMAP_HAS_SPOUT
        return std::make_unique<SpoutInput>(what);
#else
        return fail(QStringLiteral("Spout is only available on Windows"));
#endif
    }
    if (kind == QLatin1String("decklink")) {
        if (!decklink::compiledIn()) return fail(QStringLiteral("DeckLink: this build has no DeckLink SDK"));
        auto input = std::make_unique<DeckLinkInput>();
        QString e;
        if (!input->open(what, &e)) return fail(e);
        return input;
    }
    if (kind == QLatin1String("screen") || kind == QLatin1String("window")) {
#ifdef NYXMAP_HAS_WINDOW_CAPTURE
        return std::make_unique<ScreenInput>(kind == QLatin1String("window"), what);
#elif defined(NYXMAP_HAS_SCREEN_CAPTURE)
        if (kind == QLatin1String("screen")) return std::make_unique<ScreenInput>(false, what);
        return fail(QStringLiteral("Window capture needs Qt 6.6 or later"));
#else
        return fail(QStringLiteral("Screen and window capture need Qt Multimedia 6.5 or later"));
#endif
    }
    return fail(QStringLiteral("Unknown input: ") + uri);
}
