// Engine: the media used by the project — the file types read, what each layer uses, the Media Bin,
// and relinking a file that moved.
#include "EngineInternal.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <limits>
#include <map>

QStringList Engine::videoExtensions()
{
    static const QStringList e = {"mov", "mp4", "m4v", "avi", "mkv", "webm", "mxf", "mpg", "mpeg", "wmv", "flv", "ts", "hap", "mts", "m2ts"};
    return e;
}

QStringList Engine::imageExtensions()
{
    static const QStringList e = {"png", "jpg", "jpeg", "tif", "tiff", "bmp", "gif", "webp", "tga", "exr", "psd", "psb"
#ifdef NYXMAP_HAS_SVG
                                  , "svg", "svgz"
#endif
    };
    return e;
}

QStringList Engine::sequenceExtensions()
{
    static const QStringList e = {"tga", "tif", "tiff", "dpx", "exr", "png", "jpg", "jpeg"};
    return e;
}

// The numbered pictures of a folder grouped by name and extension (name0001.exr: "name", 1, 4 digits); the largest
// group is the sequence, when it holds most of the folder's pictures. Numbers of one width make a padded pattern
// (%04d); numbers without leading zeros of several widths an unpadded one (%d).
bool Engine::detectImageSequence(const QString &folder, SequenceInfo *out)
{
    const QFileInfo fi(folder);
    if (!fi.isDir()) return false;
    static const QRegularExpression numbered(QStringLiteral("^(.*?)(\\d+)\\.([A-Za-z0-9]+)$"));
    struct Group {
        std::map<qint64, QString> numbers; // number -> its digits
        QString prefix, ext;
    };
    QHash<QString, Group> groups;
    int pictures = 0;
    for (const QString &name : QDir(folder).entryList(QDir::Files | QDir::Hidden, QDir::Name)) {
        const QRegularExpressionMatch m = numbered.match(name);
        const QString ext = QFileInfo(name).suffix();
        if (!sequenceExtensions().contains(ext.toLower())) continue;
        ++pictures;
        if (!m.hasMatch() || m.captured(2).size() > 9) continue;
        Group &g = groups[m.captured(1) + QChar(0) + m.captured(3)];
        g.prefix = m.captured(1);
        g.ext = m.captured(3);
        g.numbers.emplace(m.captured(2).toLongLong(), m.captured(2));
    }
    const Group *best = nullptr;
    for (const Group &g : groups)
        if (!best || g.numbers.size() > best->numbers.size()) best = &g;
    if (!best || best->numbers.size() < 2 || int(best->numbers.size()) * 2 < pictures) return false;
    // Its widths: one (padded, or all numbers as wide), else the widest run of numbers without leading zeros
    std::map<int, int> widths;
    bool leadingZero = false;
    for (const auto &[n, digits] : best->numbers) {
        ++widths[int(digits.size())];
        leadingZero = leadingZero || (digits.size() > 1 && digits.startsWith(QLatin1Char('0')));
    }
    int width = 0; // 0: %d
    if (widths.size() == 1) width = widths.begin()->first > 1 ? widths.begin()->first : 0;
    else if (leadingZero) { // mixed widths with padding: the most common width
        width = std::max_element(widths.begin(), widths.end(), [](auto a, auto b) { return a.second < b.second; })->first;
    }
    std::vector<qint64> numbers;
    for (const auto &[n, digits] : best->numbers)
        if (width == 0 ? !(digits.size() > 1 && digits.startsWith(QLatin1Char('0'))) : int(digits.size()) == width) numbers.push_back(n);
    if (numbers.size() < 2 || numbers.front() > std::numeric_limits<int>::max()) return false;
    int frames = 1; // from the first number, until the first gap (FFmpeg's image2 stops there)
    while (size_t(frames) < numbers.size() && numbers[size_t(frames)] == numbers.front() + frames) ++frames;
    if (out) {
        QString prefix = best->prefix;
        prefix.replace(QLatin1Char('%'), QStringLiteral("%%"));
        // QString::arg also recognizes %0; build FFmpeg's literal %0Nd without
        // letting Qt substitute that part of the pattern.
        out->pattern = prefix + (width ? QStringLiteral("%0") + QString::number(width) + QLatin1Char('d')
                                      : QStringLiteral("%d")) + QLatin1Char('.') + best->ext;
        out->start = int(numbers.front());
        out->frames = frames;
    }
    return true;
}

bool Engine::isStreamUrl(const QString &s)
{
    static const QStringList schemes = {"rtsp", "rtsps", "rtmp", "rtmps", "rtmpt", "srt", "udp", "rtp", "tcp", "http", "https"};
    const int colon = s.indexOf(QLatin1String("://"));
    return colon > 0 && schemes.contains(s.left(colon).trimmed().toLower());
}

static const QString kStreamsFolder = QStringLiteral("/NyxMap/Streams/");

QString Engine::streamPath(const QString &url) { return kStreamsFolder + QString::fromLatin1(QUrl::toPercentEncoding(url)); }

QString Engine::streamUrlOfPath(const QString &path)
{
    if (isStreamUrl(path)) return path.trimmed();
    const QString p = QString(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int at = p.indexOf(kStreamsFolder);
    if (at < 0) return {};
    const QString url = QUrl::fromPercentEncoding(p.mid(at + kStreamsFolder.size()).toLatin1());
    return isStreamUrl(url) ? url : QString();
}

QString Engine::streamName(const QString &url)
{
    const QUrl u(url.trimmed());
    if (!u.isValid() || u.host().isEmpty()) return url.trimmed();
    QString name = u.host() + (u.port() > 0 ? QStringLiteral(":%1").arg(u.port()) : QString()) + u.path();
    while (name.endsWith(QLatin1Char('/'))) name.chop(1);
    return name;
}

std::vector<LiveInputInfo> Engine::audioInputs()
{
    std::vector<LiveInputInfo> found;
    for (const QString &device : AudioCapture::devices())
        found.push_back({audioInputUri(device), device, QStringLiteral("Audio")});
    return found;
}

QStringList Engine::audioExtensions()
{
    static const QStringList e = {"dsp", "nyxaudio", "wav", "aif", "aiff", "aifc", "mp3", "m4a", "aac", "flac", "ogg", "oga", "opus", "wma", "caf", "w64"};
    return e;
}

bool Engine::isAudioFile(const QString &path) { return audioExtensions().contains(QFileInfo(path).suffix().toLower()); }

bool Engine::isVideoFile(const QString &path) { return videoExtensions().contains(QFileInfo(path).suffix().toLower()); }

bool Engine::isToxFile(const QString &path) { return QFileInfo(path).suffix().compare("tox", Qt::CaseInsensitive) == 0; }

bool Engine::isImageFile(const QString &path) { return imageExtensions().contains(QFileInfo(path).suffix().toLower()); }

bool Engine::isIsfFile(const QString &path)
{
    const QString e = QFileInfo(path).suffix().toLower();
    return e == "fs" || e == "frag";
}

std::vector<Engine::MediaRef> Engine::mediaUsage() const
{
    std::vector<MediaRef> out;
    enum Kind { ImageKind, VideoKind, AudioKind };
    auto kindOf = [](const QString &p) {
        return isVideoFile(p) || isToxFile(p) || isStreamUrl(p) || QFileInfo(p).isDir() ? VideoKind : isAudioFile(p) ? AudioKind : ImageKind;
    };
    auto add = [&](const QString &path, Kind kind, const QString &user, bool imported) {
        if (path.isEmpty()) return;
        for (MediaRef &r : out) {
            if (r.path == path) {
                if (!user.isEmpty() && !r.users.contains(user)) r.users << user;
                r.imported |= imported;
                return;
            }
        }
        MediaRef r;
        r.path = path;
        r.video = kind == VideoKind;
        r.audio = kind == AudioKind;
        r.imported = imported;
        if (!user.isEmpty()) r.users << user;
        out.push_back(r);
    };
    {
        Lock lk(&m_mutex);
        for (const auto &l : m_layers) {
            const SourceType t = l->type != SourceType::None ? l->type : l->missingType;
            const bool listed = t != SourceType::Audio || (!isAudioInputUri(l->sourcePath) && !AudioGenerator::isRecipe(l->sourcePath));
            if (listed && (t == SourceType::Video || t == SourceType::Image || t == SourceType::Audio || t == SourceType::Tox))
                add(l->sourcePath, (t == SourceType::Video || t == SourceType::Tox) ? VideoKind : t == SourceType::Audio ? AudioKind : ImageKind,
                    l->name, false);
            auto scan = [&](const IsfInstance *inst, const QString &user) {
                if (!inst) return;
                for (const IsfInput &in : inst->inputs())
                    if (in.type == IsfInput::Image && !in.isInputImage && !in.imagePath.isEmpty())
                        add(in.imagePath, isVideoFile(in.imagePath) ? VideoKind : ImageKind, user, false);
            };
            scan(l->generator.get(), l->name);
            for (const auto &fx : l->effects) scan(fx.get(), l->name + QStringLiteral(" › ") + fx->name());
        }
        for (const QString &p : m_binItems)
            if (!isIsfFile(p)) add(p, kindOf(p), QString(), true); // shaders imported to the bin are in ISF > Generators
    }
    for (MediaRef &r : out) r.missing = !isStreamUrl(r.path) && !QFileInfo::exists(r.path); // a stream: never missing
    return out;
}

QStringList Engine::binItems() const
{
    Lock lk(&m_mutex);
    return m_binItems;
}

void Engine::addBinItems(const QStringList &paths)
{
    Lock lk(&m_mutex);
    for (const QString &p : paths) {
        if (p.isEmpty()) continue;
        const QString abs = isStreamUrl(p) ? p.trimmed() // an URL as it is (cleanPath would fold its "//")
                            : QFileInfo(p).isAbsolute() ? QDir::cleanPath(p) : QFileInfo(p).absoluteFilePath();
        if (!m_binItems.contains(abs)) m_binItems << abs;
    }
}

void Engine::removeBinItem(const QString &path)
{
    Lock lk(&m_mutex);
    m_binItems.removeAll(path);
    m_binStreamsUdp.remove(path);
}

void Engine::addBinStream(const QString &url, bool rtspTcp)
{
    const QString u = url.trimmed();
    if (!isStreamUrl(u)) return;
    addBinItems({u});
    Lock lk(&m_mutex);
    if (rtspTcp) m_binStreamsUdp.remove(u);
    else m_binStreamsUdp.insert(u);
}

bool Engine::binStreamTcp(const QString &url) const
{
    Lock lk(&m_mutex);
    return !m_binStreamsUdp.contains(url.trimmed());
}

void Engine::relinkBinItem(const QString &from, const QString &to)
{
    Lock lk(&m_mutex);
    const int i = m_binItems.indexOf(from);
    if (i >= 0) {
        if (m_binItems.contains(to)) m_binItems.removeAt(i);
        else m_binItems[i] = to;
    }
}

static bool isfUsesImage(const IsfInstance *inst, const QString &path)
{
    if (!inst) return false;
    for (const IsfInput &in : inst->inputs())
        if (in.type == IsfInput::Image && !in.isInputImage && in.imagePath == path) return true;
    return false;
}

QList<int> Engine::layersUsingMedia(const QString &path) const
{
    QList<int> out;
    Lock lk(&m_mutex);
    for (int i = 0; i < int(m_layers.size()); ++i) {
        const Layer &l = *m_layers[size_t(i)];
        bool uses = l.sourcePath == path && (l.type == SourceType::Video || l.type == SourceType::Image ||
                                             l.type == SourceType::Audio || l.type == SourceType::Tox || l.missingType != SourceType::None);
        uses |= l.type == SourceType::Isf && !l.sourcePath.isEmpty() && QDir::cleanPath(l.sourcePath) == QDir::cleanPath(path);
        uses |= isfUsesImage(l.generator.get(), path);
        for (const auto &fx : l.effects) uses |= isfUsesImage(fx.get(), path);
        if (uses) out << i;
    }
    return out;
}

bool Engine::relinkLayerMedia(int i, const QString &from, const QString &to, QString *err)
{
    SourceType kind = SourceType::None;
    Mapping mapping;
    PlayMode mode = PlayMode::Loop;
    double in = 0, out = -1;
    QJsonObject kept; // the values it kept while its file was missing (its speed, its range…)
    int psdLayer = -1; // the layer of a Photoshop file it showed (found again by its name in the new file)
    QString psdLayerName;
    ToxSettings tox;
    QJsonObject values; // an ISF generator's: its inputs' values, kept by the shader replacing it where the names match
    std::vector<std::pair<IsfInstance *, int>> inputs;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return false;
        const SourceType t = l->type != SourceType::None ? l->type : l->missingType;
        if (l->sourcePath == from && (t == SourceType::Video || t == SourceType::Image || t == SourceType::Audio || t == SourceType::Tox)) kind = t;
        if (t == SourceType::Isf && !l->sourcePath.isEmpty() && QDir::cleanPath(l->sourcePath) == QDir::cleanPath(from)) {
            kind = SourceType::Isf;
            if (l->generator) values = l->generator->save(QString(), true);
        }
        tox = l->tox;
        mapping = l->mapping;
        mode = l->mode;
        in = l->inPoint;
        out = l->outPoint;
        kept = l->extraParams;
        psdLayer = l->psdLayer;
        psdLayerName = l->psdLayerName;
        auto collect = [&](IsfInstance *inst) {
            if (!inst) return;
            for (int k = 0; k < int(inst->inputs().size()); ++k) {
                const IsfInput &in = inst->inputs()[size_t(k)];
                if (in.type == IsfInput::Image && !in.isInputImage && in.imagePath == from) inputs.emplace_back(inst, k);
            }
        };
        collect(l->generator.get());
        for (auto &fx : l->effects) collect(fx.get());
    }
    bool changed = false, ok = true;
    if (kind != SourceType::None) {
        // The new file may be of another type (video replaced by an image, an audio file…)
        SourceType target = kind;
        if (isToxFile(to)) target = SourceType::Tox;
        else if (isImageFile(to)) target = SourceType::Image;
        else if (isVideoFile(to)) target = SourceType::Video;
        else if (isAudioFile(to)) target = SourceType::Audio;
        else if (isIsfFile(to)) target = SourceType::Isf;
        if (target == SourceType::Isf) {
            const IsfInstance::Header h = IsfInstance::readHeader(to);
            if (!h.ok || h.isFilter || h.isTransition) {
                if (err) *err = QFileInfo(to).fileName() + QStringLiteral(": not an ISF generator");
                return false;
            }
        }
        ok = target == SourceType::Tox ? setLayerTox(i, to, tox, err)
             : target == SourceType::Video   ? setLayerVideo(i, to, err)
             : target == SourceType::Audio ? setLayerAudio(i, to, err)
             : target == SourceType::Isf   ? setLayerIsf(i, to, err)
                                           : setLayerImage(i, to, err, psdLayer, psdLayerName);
        if (ok && target == SourceType::Isf && kind == SourceType::Isf) {
            IsfInstance *gen;
            {
                Lock lk(&m_mutex);
                gen = layer(i) ? layer(i)->generator.get() : nullptr;
            }
            if (gen && !values.isEmpty()) runGl([gen, values] { gen->restore(values, QString()); });
        }
        if (ok) {
            setLayerPlayMode(i, mode); // the layer keeps its play mode and its in / out points
            setLayerInOut(i, in, out);
            Lock lk(&m_mutex);
            if (Layer *l = layer(i)) {
                const unsigned rev = l->mapping.revision;
                const double aspect = l->mapping.aspect;
                l->mapping = mapping; // the aligned mapping stays intact
                l->mapping.aspect = aspect;
                l->mapping.revision = rev + 1;
                parametersFromJson(l->parameters(), kept, &l->extraParams);
            }
            changed = true;
        }
    }
    for (auto &[inst, k] : inputs) {
        QString e;
        if (setIsfImageInput(inst, k, to, &e)) changed = true;
        else if (err && err->isEmpty()) *err = e;
    }
    return changed && ok;
}
