// Engine: one frame — the sources are updated, every layer is rendered in dependency order, the
// composition is assembled, then read back for the interface preview and for publishing.
#include <QJsonArray>
#include "EngineInternal.h"
#include "TimecodeClock.h"
#include "ColorInfo.h"
#include "SourceColor.h"

#include <map>
#include <set>
#include <memory>
#include <vector>

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QOffscreenSurface>
#include <QPainter>
#include <QTextLayout>
#include <QWindow>
#include <cmath>
#include <cstring>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif

// The Text generator's picture: the text shown now (typed so far, during a typewriter), wrapped to the layer's
// width, with its alignment (left, center, right, justified; top, middle, bottom), line spacing and letter spacing.
// Filled with `fill` (the text's color, or a countdown's overtime color). Flipped like the images (the sources are
// stored bottom-up).
static QImage renderTextLayer(const Layer &l, const QString &text, const QColor &fill)
{
    const int w = std::max(1, l.text.width), h = std::max(1, l.text.height);
    QImage img(w, h, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    if (text.isEmpty()) return img.convertToFormat(QImage::Format_RGBA8888);

    QFont font(l.text.font);
    font.setPixelSize(std::max(1, l.text.size));
    font.setLetterSpacing(QFont::AbsoluteSpacing, l.text.letterSpacing);
    font.setBold(l.text.bold);
    font.setItalic(l.text.italic);
    font.setUnderline(l.text.underline);
    font.setStrikeOut(l.text.strike);

    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    option.setAlignment(l.text.align & Qt::AlignHorizontal_Mask ? (l.text.align & Qt::AlignHorizontal_Mask) : Qt::AlignLeft);

    // One layout per paragraph (a line break starts another)
    const QFontMetricsF fm(font);
    const double step = fm.lineSpacing() * std::max(0.1f, l.text.lineHeight);
    struct Line { QTextLayout *layout; QTextLine line; double y; };
    std::vector<std::unique_ptr<QTextLayout>> layouts;
    std::vector<Line> lines;
    double y = 0;
    for (const QString &para : text.split(QChar('\n'))) {
        auto layout = std::make_unique<QTextLayout>(para.isEmpty() ? QStringLiteral(" ") : para, font);
        layout->setTextOption(option);
        layout->beginLayout();
        for (;;) {
            QTextLine line = layout->createLine();
            if (!line.isValid()) break;
            line.setLineWidth(w);
            line.setPosition(QPointF(0, y));
            lines.push_back({layout.get(), line, y});
            y += step;
        }
        layout->endLayout();
        layouts.push_back(std::move(layout));
    }
    const double total = y;
    double top = 0;
    if (l.text.align & Qt::AlignBottom) top = h - total;
    else if (l.text.align & Qt::AlignVCenter) top = (h - total) / 2;

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font);
    // The lines drawn in one color at an offset; the layout's alignment applies inside the width
    auto draw = [&](const QColor &c, double dx, double dy) {
        painter.setPen(c);
        for (const Line &ln : lines) ln.line.draw(&painter, QPointF(dx, top + dy));
    };
    // The outline: the text repeated around its place (a ring of copies), under the fill
    auto drawOutlined = [&](const QColor &outline, const QColor &fill, double dx, double dy, bool withFill) {
        if (l.text.outline > 0.01f) {
            const int n = std::clamp(int(l.text.outline * 6), 16, 48);
            for (int k = 0; k < n; ++k) {
                const double a = 2 * M_PI * k / n;
                draw(outline, dx + std::cos(a) * l.text.outline, dy + std::sin(a) * l.text.outline);
            }
        }
        if (withFill) draw(fill, dx, dy);
    };
    if (l.text.shadow) { // the shadow has the shape of the text and its outline, in its own color
        QColor sc = l.text.shadowColor;
        if (l.text.outline > 0.01f) drawOutlined(sc, sc, l.text.shadowX, l.text.shadowY, true);
        else draw(sc, l.text.shadowX, l.text.shadowY);
    }
    drawOutlined(l.text.outlineColor, fill, 0, 0, true);
    painter.end();
    return flippedVertically(img.convertToFormat(QImage::Format_RGBA8888));
}

void Engine::drawQuad()
{
    auto f = gl();
    f->glBindVertexArray(m_quadVao);
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// The layers the image inputs of a layer's shaders (its generator, its enabled effects) show
void Engine::forEachImageLayer(const Layer &l, const std::function<void(quint64)> &fn) const
{
    if (l.type == SourceType::Tox)
        for (quint64 input : l.tox.inputs) if (input) fn(input);
    auto each = [&](const IsfInstance *inst) {
        for (const IsfInput &in : inst->inputs())
            if (in.imageLayer) fn(in.imageLayer);
    };
    if (l.type == SourceType::Isf && l.generator) each(l.generator.get());
    if (l.effectsEnabled)
        for (const auto &fx : l.effects)
            if (fx->enabled) each(fx.get());
}

void Engine::bindLayerImages(IsfInstance &inst)
{
    for (IsfInput &in : inst.inputs()) {
        if (!in.imageLayer) continue;
        const Layer *src = layer(indexOfId(in.imageLayer));
        // Not drawn this frame (gone, in error): its image file, or black
        in.extTex = src && src->finalTex ? src->finalTex : 0;
        in.extW = src ? src->finalW : 0;
        in.extH = src ? src->finalH : 0;
    }
}

// ISF "audio" and "audioFFT" inputs: the live input's sound or the program's, now, a row per channel, as
// 32-bit float RGBA (grey)
void Engine::feedAudio(IsfInstance &inst)
{
    std::vector<float> values, rgba;
    for (IsfInput &in : inst.inputs()) {
        if (in.type != IsfInput::Audio && in.type != IsfInput::AudioFFT) continue;
        const bool program = in.audioSource == IsfInput::Program;
        int w;
        if (in.type == IsfInput::Audio) {
            w = AudioAnalysis::waveWidth(in.audioMax);
            values.resize(size_t(2 * w));
            AudioAnalysis::waveColumns(program ? m_audio->wave() : m_audioInput->wave(), in.audioMax, values.data());
        } else {
            w = AudioAnalysis::spectrumWidth(in.audioMax);
            values.resize(size_t(2 * w));
            AudioAnalysis::spectrumColumns(program ? m_audio->spectrum() : m_audioInput->spectrum(), in.audioMax, values.data());
        }
        rgba.resize(values.size() * 4);
        for (size_t k = 0; k < values.size(); ++k) {
            rgba[4 * k] = rgba[4 * k + 1] = rgba[4 * k + 2] = values[k];
            rgba[4 * k + 3] = 1.f;
        }
        in.imageTex.uploadFloat(rgba.data(), w, 2);
    }
}

// Squares rather than GL_POINTS: no limit on their size, and no driver's wide-point path (Mesa 23.3's llvmpipe
// crashed in it with a colour passed on and gl_PointCoord)
void Engine::drawPoints(int count)
{
    auto f = gl();
    f->glBindVertexArray(m_pointsVao);
    f->glDrawArrays(GL_TRIANGLES, 0, count * 6);
}

void Engine::blit(GLuint tex, const RenderTarget &target)
{
    auto f = gl();
    target.bind();
    f->glDisable(GL_BLEND);
    f->glUseProgram(m_blitProgram);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glUniform1i(m_blitTexLoc, 0);
    drawQuad();
}

void Engine::updateSource(Layer &l, double dt)
{
    // An image has no time: uploaded paused or not (its previous texture is already gone)
    if (!l.pendingImage.isNull()) {
        l.sourceTex.uploadImage(l.pendingImage);
        l.pendingImage = QImage();
        l.renderPaused = true; // seen at once, even while paused
        if (!l.pendingAutoImage.isNull()) {
            l.imageAutoTex.uploadImage(l.pendingAutoImage);
            l.pendingAutoImage = QImage();
        }
    }
    if (l.toxPlayer && l.tox.playing) l.toxPlayer->advance(m_realDt * timeScale());
    (void)dt;
    if ((l.type != SourceType::Video && l.type != SourceType::Audio) || !l.hasTransport()) return;
    if (l.playing) {
        // Real time (not the clamped animation step): after a render stall, picture and sound stay together.
        l.clock += m_realDt * std::abs(l.speed) * timeScale();
        if (l.atEnd()) { // One-shot / Stop: end of the media (the end, or the start when playing backwards)
            l.clock = l.timeline().firstLeg().clockEnd();
            l.playing = false;
            l.ended = l.mode == PlayMode::Stop; // Stop: black (and silent); One-shot: last frame
        }
    }
    if (l.audio)
        l.audio->setTransport(l.clock, l.playing, std::abs(l.speed) * timeScale(), l.timeline(), l.timelineId, l.audioGain(), m_frameStampNs);
    if (!l.video || timeScale() <= 0) return;
    if (!l.videoTex) l.videoTex = std::make_unique<VideoTexture>();
    l.videoTex->feed(*l.video); // upload buffers the decoder writes its next frames into
    if (l.video->fetch(l.clock, l.frame)) l.frameUploaded = false;
    // Only a picture that is drawn goes to the GPU; a hidden layer's last frame waits to be shown
    if (!l.frameUploaded && l.needed) {
        l.videoTex->upload(l.frame, *l.video, m_videoConv);
        l.frameUploaded = true;
    }
}

// Solo: the soloed layers are kept, with everything inside a soloed group and the groups holding them; the others
// are left out. Nothing soloed: none left out. Viewports are never left out.
std::vector<bool> Engine::soloLeftOut() const
{
    std::vector<bool> out(m_layers.size(), false);
    std::map<quint64, const Layer *> byId;
    bool anySolo = false;
    for (const auto &l : m_layers) {
        byId[l->id] = l.get();
        anySolo = anySolo || (l->solo && !l->isViewport);
    }
    if (!anySolo) return out;
    auto up = [&](quint64 id) -> const Layer * {
        const auto it = byId.find(id);
        return it == byId.end() ? nullptr : it->second;
    };
    std::set<quint64> kept;
    for (const auto &l : m_layers) {
        bool in = false;
        for (const Layer *p = l.get(); p && !in; p = up(p->parent)) in = p->solo;
        if (!in) continue;
        kept.insert(l->id);
        for (const Layer *g = up(l->parent); g; g = up(g->parent)) kept.insert(g->id);
    }
    for (size_t i = 0; i < m_layers.size(); ++i) out[i] = !m_layers[i]->isViewport && !kept.count(m_layers[i]->id);
    return out;
}

// Which layers are drawn this frame: shown (in a shown group), used as a source or as a mask by another,
// previewed in the inspector or a preview view while switched on, viewports. The others are neither uploaded nor
// rendered (their sound plays on). A layer switched off (or in a group switched off) is not computed for a preview:
// only another layer that uses it keeps it rendered.
void Engine::markNeeded()
{
    for (auto &l : m_layers) l->referenced = false;
    auto refer = [this](const Layer &l) { // the layers its picture is made from
        if (l.type == SourceType::Layer && l.sourceLayer) {
            const int si = indexOfId(l.sourceLayer);
            if (si >= 0) m_layers[size_t(si)]->referenced = true;
        }
        for (const auto &fx : l.effects)
            if (fx->enabled && fx->maskLayer) {
                const int mi = indexOfId(fx->maskLayer);
                if (mi >= 0) m_layers[size_t(mi)]->referenced = true;
            }
        if (l.color.enabled && l.color.maskLayer) {
            const int mi = indexOfId(l.color.maskLayer);
            if (mi >= 0) m_layers[size_t(mi)]->referenced = true;
        }
        if (l.matte.layer) {
            const int mi = indexOfId(l.matte.layer);
            if (mi >= 0) m_layers[size_t(mi)]->referenced = true;
        }
        forEachImageLayer(l, [&](quint64 id) {
            const int si = indexOfId(id);
            if (si >= 0) m_layers[size_t(si)]->referenced = true;
        });
    };
    auto visionReference = [this](const Interaction &input) {
        if (!input.enabled || (input.source != InteractionSource::Pixels && input.source != InteractionSource::Hands))
            return;
        const int index = indexOfId(input.pixels.layer);
        if (index >= 0)
            m_layers[size_t(index)]->referenced = true;
    };
    for (const auto &input : m_interactions)
        if (input.enabled) {
            visionReference(input);
            for (const auto &axis : input.xyInputs)
                visionReference(axis);
        }
    for (auto &l : m_layers) refer(*l);
    for (auto &[id, t] : m_transitions) refer(*t->from); // the outgoing source is drawn too, with what it uses
    const std::vector<bool> leftOut = soloLeftOut();
    for (size_t i = 0; i < m_layers.size(); ++i) m_layers[i]->soloHidden = leftOut[i];
    // A group comes before its members: its verdict is known when they are reached
    std::map<quint64, bool> groupNeeded;
    for (auto &lp : m_layers) {
        Layer &l = *lp;
        bool inShown = true;
        if (l.parent) {
            const auto g = groupNeeded.find(l.parent);
            inShown = g != groupNeeded.end() && g->second;
        }
        const bool previewed =
            l.id == m_previewId ||
            std::any_of(m_previewSlots.begin(), m_previewSlots.end(), [&](const auto &p) { return p.id == l.id; });
        l.needed = l.isViewport || l.referenced || (previewed && l.enabled && l.parentEnabled) ||
                   (l.enabled && inShown && !l.soloHidden);
        if (l.isGroup) groupNeeded[l.id] = l.needed;
    }
    for (auto &[id, t] : m_transitions) t->from->needed = true;
}

// One frame of every layer, in dependency order: a layer whose source is another layer is rendered after it,
// and a group after its members. A layer used as a source is rendered even when it is hidden.
// A reference cycle (A on B, B on A) is not broken by an error: the layer reached a second time keeps the
// texture of the previous frame, which gives a one-frame feedback loop.
void Engine::renderPass(const IsfRenderContext &rc, const std::set<quint64> *only)
{
    const size_t n = m_layers.size();
    enum { Todo = 0, Doing = 1, Done = 2 };
    m_renderMark.assign(n, Todo);
    std::function<void(size_t)> render = [&](size_t i) {
        if (m_renderMark[i] != Todo) return; // done, or being rendered (cycle)
        if (only && !only->count(m_layers[i]->id)) { // held: its picture stays as it is
            m_renderMark[i] = Done;
            return;
        }
        m_renderMark[i] = Doing;
        Layer &l = *m_layers[i];
        auto dependencies = [&](const Layer &d) { // the layers its picture is made from, before it
            if (d.type == SourceType::Layer && d.sourceLayer) {
                const int si = indexOfId(d.sourceLayer);
                if (si >= 0) render(size_t(si));
            }
            for (const auto &fx : d.effects) // the masks of its effects
                if (fx->enabled && fx->maskLayer) {
                    const int mi = indexOfId(fx->maskLayer);
                    if (mi >= 0 && !m_layers[size_t(mi)]->isViewport) render(size_t(mi));
                }
            if (d.color.enabled && d.color.maskLayer) { // the mask of its color
                const int mi = indexOfId(d.color.maskLayer);
                if (mi >= 0 && !m_layers[size_t(mi)]->isViewport) render(size_t(mi));
            }
            if (d.matte.layer) { // its matte
                const int mi = indexOfId(d.matte.layer);
                if (mi >= 0 && !m_layers[size_t(mi)]->isViewport) render(size_t(mi));
            }
            forEachImageLayer(d, [&](quint64 id) { // the layers its shaders show as images
                const int si = indexOfId(id);
                if (si >= 0 && !m_layers[size_t(si)]->isViewport) render(size_t(si));
            });
        };
        dependencies(l);
        const auto transition = l.isGroup || l.isViewport ? m_transitions.end() : m_transitions.find(l.id);
        if (transition != m_transitions.end()) dependencies(*transition->second->from); // the outgoing source's too
        if (l.isViewport) {
            // What it sees: the items at the top of the list it shows, all rendered by now
            std::vector<Layer *> shown;
            for (size_t k = 0; k < n; ++k) {
                Layer &o = *m_layers[k];
                if (o.isViewport || o.parent || !o.shownIn(l.id)) continue;
                render(k);
                shown.push_back(&o);
            }
            renderViewport(l, shown, rc);
        } else if (l.isGroup) {
            // Its direct contents, in list order (groups inside are rendered first, by the recursion)
            std::vector<Layer *> members;
            for (size_t k = i + 1; k < n; ++k)
                if (m_layers[k]->parent == l.id) {
                    render(k);
                    members.push_back(m_layers[k].get());
                }
            if (l.needed) renderGroup(l, members, rc);
            else l.finalTex = l.rawTex = l.preFxTex = 0;
        } else {
            renderLayer(l, rc);
            if (transition != m_transitions.end()) renderTransition(l, *transition->second, rc);
        }
        if (!l.isViewport) prepareMatte(l); // before it is composited (a viewport or a group draws it later)
        m_renderMark[i] = Done;
    };
    for (size_t i = 0; i < n; ++i) render(i);
}

// Paused, nothing is rendered (feedback buffers and FRAMEINDEX hold too), except a picture loaded meanwhile: that layer,
// the groups it is in and the viewports that show it are drawn once, the others keep their frame
void Engine::renderPausedSources(const IsfRenderContext &rc)
{
    std::set<quint64> only;
    for (auto &lp : m_layers) {
        if (!lp->renderPaused) continue;
        lp->renderPaused = false;
        for (const Layer *x = lp.get(); x && only.insert(x->id).second; x = x->parent ? layer(indexOfId(x->parent)) : nullptr) {}
    }
    if (only.empty()) return;
    for (auto &vp : m_layers) {
        if (!vp->isViewport) continue;
        for (auto &o : m_layers)
            if (!o->isViewport && !o->parent && only.count(o->id) && o->shownIn(vp->id)) {
                only.insert(vp->id);
                break;
            }
    }
    renderPass(rc, &only);
}

void Engine::renderLayer(Layer &l, const IsfRenderContext &rc)
{
    l.finalTex = 0;
    l.rawTex = 0;
    l.preFxTex = 0;
    if (l.isGroup) return;  // rendered from its members (renderGroup)
    if (l.ended) return; // Stop mode, after the end: nothing
    if (!l.needed) return; // hidden, and used by no other layer
    GLuint tex = 0;
    int w = 0, h = 0;
    switch (l.type) {
    case SourceType::Layer:
        // Picture of another layer, already rendered this frame (the render pass follows the references).
        if (const Layer *s = l.sourceLayer ? layer(indexOfId(l.sourceLayer)) : nullptr) {
            switch (l.sourceTap) {
            case LayerTap::Source: // its source's picture, as it is (a group has none of its own: its composite)
                if (s->isGroup) tex = s->finalTex, w = s->finalW, h = s->finalH;
                else tex = s->rawTex, w = s->rawW, h = s->rawH;
                break;
            case LayerTap::PreFx: tex = s->preFxTex, w = s->preFxW, h = s->preFxH; break;
            case LayerTap::PostFx: tex = s->finalTex, w = s->finalW, h = s->finalH; break;
            }
            l.srcWidth = w; // what the inspector and the ROI editor show
            l.srcHeight = h;
        }
        break;
    case SourceType::Video:
        if (l.videoTex) {
            tex = l.videoTex->texture();
            w = l.videoTex->width();
            h = l.videoTex->height();
            // A live stream's size is known at its first picture (and may change when it connects again)
            if (l.videoSource.live && w > 0 && h > 0 && (w != l.srcWidth || h != l.srcHeight)) {
                const bool first = !l.srcWidth;
                l.srcWidth = w;
                l.srcHeight = h;
                if (first && l.mapping.isIdentity())
                    l.mapping.fitAspect(double(w) / h, double(m_compSize.width()) / m_compSize.height());
            }
        }
        break;
    case SourceType::Image:
        tex = l.sourceColorSpace == QLatin1String("auto") && l.imageAutoTex.tex ? l.imageAutoTex.tex : l.sourceTex.tex;
        w = l.sourceTex.w;
        h = l.sourceTex.h;
        break;
    case SourceType::Input: // the newest frame of the camera, screen, window, DeckLink, NDI, OMT, Syphon or Spout source
        if (l.input && l.input->update(l.sourceTex)) {
            const bool first = !l.srcWidth;
            l.srcWidth = l.sourceTex.w;
            l.srcHeight = l.sourceTex.h;
            if (first && l.mapping.isIdentity() && l.srcHeight > 0)
                l.mapping.fitAspect(double(l.srcWidth) / l.srcHeight, double(m_compSize.width()) / m_compSize.height());
        }
        tex = l.sourceTex.tex;
        w = l.sourceTex.w;
        h = l.sourceTex.h;
        break;
    case SourceType::Tox:
        if (l.toxPlayer) {
            for (const auto &top : l.toxPlayer->inputs()) {
                const Layer *source = layer(indexOfId(l.tox.inputs.value(top.first)));
                l.toxPlayer->setInputTexture(top.first, source ? source->finalTex : 0,
                                            source ? source->finalW : 0, source ? source->finalH : 0);
            }
            if (l.toxPlayer->update(l.sourceTex)) {
                const bool first = !l.srcWidth;
                l.srcWidth = l.sourceTex.w; l.srcHeight = l.sourceTex.h;
                if (first && l.mapping.isIdentity() && l.srcHeight > 0)
                    l.mapping.fitAspect(double(l.srcWidth) / l.srcHeight, double(m_compSize.width()) / m_compSize.height());
            }
            l.error = l.toxPlayer->error();
        }
        tex = l.sourceTex.tex; w = l.sourceTex.w; h = l.sourceTex.h;
        break;
    case SourceType::Web: // the newest picture taken of the page (its size: the page's, set in its settings)
        if (l.webPage && l.webPage->update(l.sourceTex)) {
            l.srcWidth = l.sourceTex.w;
            l.srcHeight = l.sourceTex.h;
        }
        tex = l.sourceTex.tex;
        w = l.sourceTex.w;
        h = l.sourceTex.h;
        break;
    case SourceType::Isf:
        if (l.generator) {
            bindLayerImages(*l.generator);
            feedAudio(*l.generator);
            if (isLineGeneratorPath(l.sourcePath)) { // its lines, as segments, uploaded again when they change
                if (l.linesTexRev != l.linesRevision || l.linesTexSize != QSize(l.genWidth, l.genHeight)) {
                    int count = 0;
                    const std::vector<float> data = lineSegmentData(l.lines, l.genWidth, l.genHeight, &count);
                    l.linesTex.uploadFloat(data.data(), count, 2);
                    l.linesTexRev = l.linesRevision;
                    l.linesTexSize = QSize(l.genWidth, l.genHeight);
                }
                l.generator->setExtraTexture(QByteArrayLiteral("nyxmapSegments"), l.linesTex.tex);
            }
            l.generator->render(rc, 0, 0, 0, l.generatorTarget, l.genWidth, l.genHeight);
            if (l.generatorTarget.ready(l.genWidth, l.genHeight)) {
                tex = l.generatorTarget.tex;
                w = l.generatorTarget.w;
                h = l.generatorTarget.h;
            }
        }
        break;
    case SourceType::Text: {
        // Redrawn only when what is shown changes (typed characters, edits, style)
        // A Timecode generator: its clock; a Countdown, Stopwatch or Clock: its time (an overtime in a color of its own)
        const QString shown = l.tcClock ? l.tcClock->display() : l.clockGen ? l.clockGen->display() : l.text.shown();
        const QColor fill = l.clockGen ? l.clockGen->color(l.text.color) : l.text.color;
        const QString key = QStringLiteral("%1\n%2|%3|%4|%5|%6|%7|%8x%9|%10").arg(shown, l.text.font).arg(l.text.size)
                                .arg(fill.name(QColor::HexArgb)).arg(int(l.text.align))
                                .arg(l.text.lineHeight).arg(l.text.letterSpacing).arg(l.text.width).arg(l.text.height)
                                .arg(QStringLiteral("%1%2%3%4|%5%6|%7%8|%9").arg(l.text.bold).arg(l.text.italic).arg(l.text.underline)
                                         .arg(l.text.strike).arg(l.text.outline).arg(l.text.outlineColor.name(QColor::HexArgb))
                                         .arg(l.text.shadow).arg(l.text.shadowColor.name(QColor::HexArgb))
                                         .arg(QStringLiteral("%1,%2").arg(l.text.shadowX).arg(l.text.shadowY)));
        if (!l.textTex.tex || key != l.textKey) {
            const QImage img = renderTextLayer(l, shown, fill);
            l.textTex.upload(img.constBits(), img.width(), img.height());
            l.textKey = key;
        }
        tex = l.textTex.tex;
        w = l.textTex.w;
        h = l.textTex.h;
        break;
    }
    default: break;
    }
    if (!tex) return;
    const QString colorKey = sourceColorKey(l);
    if (colorKey != QLatin1String("srgb")) {
        const bool changed = l.sourceColorTarget.ensure(w, h);
        if (!l.sourceColorTarget.ready(w, h)) return; // fail closed, never display incorrectly interpreted pixels
        if (l.type != SourceType::Image || changed || l.sourceColorCachedKey != colorKey) {
            auto f = gl();
            l.sourceColorTarget.bind();
            f->glDisable(GL_BLEND);
            f->glUseProgram(m_outputColorProgram);
            f->glActiveTexture(GL_TEXTURE0);
            f->glBindTexture(GL_TEXTURE_2D, tex);
            f->glUniform1i(m_outputTexLoc, 0);
            f->glUniform1i(m_outputSourceLoc, 1);
            f->glUniform1i(m_outputDecodeLoc, sourceColorSpaceIndex(colorKey));
            f->glUniform1i(m_outputEncodeLoc, 0);
            f->glUniform1f(m_outputGainLoc, 1);
            f->glUniform1f(m_outputOpacityLoc, 1);
            drawQuad();
            l.sourceColorCachedKey = colorKey;
        }
        tex = l.sourceColorTarget.tex;
    } else if (l.sourceColorTarget.tex) {
        l.sourceColorTarget.destroy();
        l.sourceColorCachedKey.clear();
    }
    processLayer(l, tex, w, h, false, rc);
}

// The outgoing source's picture (its ROI, color and effects) and the incoming one, mixed by the transition into the
// layer's picture; the layer's mapping, opacity and blend then apply to the mix.
void Engine::renderTransition(Layer &l, SourceTransition &t, const IsfRenderContext &rc)
{
    renderLayer(*t.from, rc);
    const GLuint a = t.from->finalTex, b = l.finalTex;
    t.incomingTex = b;
    t.incomingW = l.finalW;
    t.incomingH = l.finalH;
    const int w = b ? l.finalW : t.from->finalW, h = b ? l.finalH : t.from->finalH;
    if (w <= 0 || h <= 0) return;
    if (!t.loaded) {
        t.loaded = true;
        if (!t.shaderPath.isEmpty()) {
            auto s = std::make_unique<IsfInstance>();
            if (s->load(t.shaderPath) && s->input(QStringLiteral("startImage")) && s->input(QStringLiteral("endImage"))) {
                t.shader = std::move(s);
            } else {
                qWarning("Transition %s: %s", qPrintable(t.shaderPath), qPrintable(s->error()));
                s->releaseGl();
            }
        }
    }
    const double p = std::clamp(t.elapsed / t.duration, 0.0, 1.0);
    t.target.ensure(w, h);
    if (!t.target.ready(w, h)) return; // keep the last valid incoming layer output
    if (t.shader) {
        t.shader->setImageTexture(QStringLiteral("startImage"), a, t.from->finalW, t.from->finalH);
        t.shader->setImageTexture(QStringLiteral("endImage"), b, l.finalW, l.finalH);
        if (IsfInput *in = t.shader->input(QStringLiteral("progress"))) in->fValue = p * p * (3 - 2 * p);
        t.shader->render(rc, 0, 0, 0, t.target, w, h);
    } else {
        // A crossfade: the outgoing picture to the incoming one at the progress
        auto f = gl();
        t.target.bind();
        f->glDisable(GL_BLEND);
        f->glUseProgram(m_fadeProgram);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, a);
        f->glUniform1i(m_fadeALoc, 0);
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, b);
        f->glUniform1i(m_fadeBLoc, 1);
        f->glUniform3f(m_fadeKLoc, float(p * p * (3 - 2 * p)), a ? 1.0f : 0.0f, b ? 1.0f : 0.0f);
        drawQuad();
        f->glActiveTexture(GL_TEXTURE0);
    }
    l.finalTex = t.target.tex;
    l.finalW = w;
    l.finalH = h;
}

// Source picture -> roi and color (when needed) -> effect chain -> l.finalTex
void Engine::processLayer(Layer &l, GLuint tex, int w, int h, bool premultiplied, const IsfRenderContext &rc)
{
    l.rawTex = tex;
    l.rawW = w;
    l.rawH = h;
    QRectF c = l.roi.normalized() & Layer::fullRoi();
    if (c.width() < 1e-4 || c.height() < 1e-4) c = Layer::fullRoi();
    const bool restricted = c != Layer::fullRoi();
    const ColorAdjust col = l.color.effective(); // switches honoured once, here
    if (restricted || !col.isIdentity() || premultiplied) {
        auto f = gl();
        const int cw = std::max(1, int(std::lround(w * c.width()))), ch = std::max(1, int(std::lround(h * c.height())));
        l.prepTarget.ensure(cw, ch);
        if (!l.prepTarget.ready(cw, ch)) {
            // Fail closed: never expose unprocessed media when ROI/color
            // preparation cannot allocate a compatible output texture.
            // The pre-effects tap too: a layer or mask reading it must not get the previous frame.
            l.preFxTex = 0;
            l.finalTex = 0;
            return;
        }
        l.prepTarget.bind();
        f->glDisable(GL_BLEND);
        f->glUseProgram(m_prepProgram);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, tex);
        f->glUniform1i(m_prepTexLoc, 0);
        // ROI: top-left origin in the UI, textures in OpenGL convention (origin bottom left)
        f->glUniform4f(m_prepRoiLoc, float(c.left()), float(1.0 - c.bottom()), float(c.right()), float(1.0 - c.top()));
        f->glUniform3f(m_prepAddLoc, col.add[0], col.add[1], col.add[2]);
        f->glUniform3f(m_prepRemoveLoc, col.remove[0], col.remove[1], col.remove[2]);
        float gains[3];
        col.balanceGains(gains);
        f->glUniform3f(m_prepBalanceLoc, gains[0], gains[1], gains[2]);
        f->glUniform1i(m_prepUnpremulLoc, premultiplied ? 1 : 0);
        // The color through its mask (another layer's picture, stretched over this one)
        const Layer *mask = col.maskLayer && !col.isIdentity() ? layer(indexOfId(col.maskLayer)) : nullptr;
        if (mask && mask->finalTex && mask != &l) {
            f->glActiveTexture(GL_TEXTURE1);
            f->glBindTexture(GL_TEXTURE_2D, mask->finalTex);
            f->glUniform1i(m_prepMaskLoc, 1);
            f->glUniform1i(m_prepMaskModeLoc, col.maskInvert ? 2 : 1);
            f->glActiveTexture(GL_TEXTURE0);
        } else {
            f->glUniform1i(m_prepMaskModeLoc, 0);
        }
        drawQuad();
        tex = l.prepTarget.tex;
        w = cw;
        h = ch;
    } else if (l.prepTarget.fbo) {
        l.prepTarget.destroy();
    }

    l.preFxTex = tex; // tap of a layer used as a source: after the ROI and the color, before the effects
    l.preFxW = w;
    l.preFxH = h;

    if (l.effectsEnabled) {
        int ping = 0;
        for (auto &fx : l.effects) {
            if (!fx->enabled) continue;
            RenderTarget &dst = l.fxTarget[ping];
            bindLayerImages(*fx);
            feedAudio(*fx);
            fx->render(rc, tex, w, h, dst, w, h);
            if (!dst.ready(w, h)) {
                // Do not present a raw source or a stale-size effect output.
                l.finalTex = 0;
                return;
            }
            // Through its mask: the effect where the mask is white, the input where it is black
            const Layer *mask = fx->maskLayer ? layer(indexOfId(fx->maskLayer)) : nullptr;
            const GLuint maskTex = !mask ? 0 : fx->maskPreFx && mask->preFxTex ? mask->preFxTex : mask->finalTex;
            if (mask && maskTex && mask != &l) {
                RenderTarget &mixed = l.maskTarget[ping];
                mixed.ensure(w, h);
                if (!mixed.ready(w, h)) {
                    // Do not silently bypass an effect visibility mask.
                    l.finalTex = 0;
                    return;
                }
                mixed.bind();
                auto f = gl();
                f->glDisable(GL_BLEND);
                f->glUseProgram(m_maskProgram);
                const GLuint texs[3] = {tex, dst.tex, maskTex};
                const GLint locs[3] = {m_maskInLoc, m_maskFxLoc, m_maskMaskLoc};
                for (int k = 0; k < 3; ++k) {
                    f->glActiveTexture(GL_TEXTURE0 + k);
                    f->glBindTexture(GL_TEXTURE_2D, texs[k]);
                    f->glUniform1i(locs[k], k);
                }
                f->glUniform1i(m_maskInvertLoc, fx->maskInvert ? 1 : 0);
                drawQuad();
                f->glActiveTexture(GL_TEXTURE0);
                tex = mixed.tex;
            } else {
                tex = dst.tex;
            }
            ping = 1 - ping;
        }
    }
    applySurfaceMasks(l, tex, w, h);
    applyPictureMatte(l, tex, w, h);
    l.finalTex = tex;
    l.finalW = w;
    l.finalH = h;
}

// Masks are drawn at most 2048 pixels on their long side (their soft edges and the GPU's filtering hide it)
static QSize maskRasterSize(int w, int h)
{
    const int longSide = std::max(w, h);
    if (longSide <= 2048) return QSize(std::max(1, w), std::max(1, h));
    const double s = 2048.0 / longSide;
    return QSize(std::max(1, int(std::lround(w * s))), std::max(1, int(std::lround(h * s))));
}

// Its surface masks: the picture through their alpha (a viewport's color too, as its screen shows it opaque)
void Engine::applySurfaceMasks(Layer &l, GLuint &tex, int w, int h)
{
    if (w <= 0 || h <= 0 || !masks::any(l.masks, Mask::Attach::Surface)) {
        if (l.shapeMaskTarget.fbo) l.shapeMaskTarget.destroy();
        return;
    }
    const QSize size = maskRasterSize(w, h);
    if (l.shapeMaskSurfaceRev != l.masksRevision || l.shapeMaskSurfaceSize != size) {
        const QImage img = masks::raster(l.masks, Mask::Attach::Surface, size).convertToFormat(QImage::Format_RGBA8888);
        l.shapeMaskSurface.upload(img.constBits(), img.width(), img.height());
        l.shapeMaskSurfaceRev = l.masksRevision;
        l.shapeMaskSurfaceSize = size;
    }
    auto f = gl();
    l.shapeMaskTarget.ensure(w, h);
    if (!l.shapeMaskTarget.ready(w, h)) {
        // Surface masks must fail closed, not reveal unmasked content.
        tex = 0;
        return;
    }
    l.shapeMaskTarget.bind();
    f->glDisable(GL_BLEND);
    f->glUseProgram(m_shapeMaskProgram);
    f->glActiveTexture(GL_TEXTURE1);
    f->glBindTexture(GL_TEXTURE_2D, l.shapeMaskSurface.tex);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glUniform1i(m_shapeMaskTexLoc, 0);
    f->glUniform1i(m_shapeMaskMaskLoc, 1);
    f->glUniform1i(m_shapeMaskRgbLoc, l.isViewport ? 1 : 0);
    drawQuad();
    tex = l.shapeMaskTarget.tex;
}

// A matte in Composition space: the matte layer drawn alone, where it is in the composition (its mapping, its
// opacity, its own composition masks), on a transparent canvas the size of the composition. Drawn even when that
// layer is switched off (a matte is usually hidden). A matte layer with no picture: nothing shows (or all, inverted).
void Engine::prepareMatte(Layer &l)
{
    if (!l.matte.layer || l.matte.space != LayerMatte::Space::Composition) {
        if (l.matteTarget.fbo) l.matteTarget.destroy();
        return;
    }
    if (!l.needed) return; // not drawn this frame
    l.matteTarget.ensure(m_compSize.width(), m_compSize.height());
    if (!l.matteTarget.ready(m_compSize.width(), m_compSize.height())) {
        l.matteTarget.destroy(); // no matte target: drawn without it rather than from a stale one
        return;
    }
    l.matteTarget.clear();
    Layer *m = layer(indexOfId(l.matte.layer));
    if (!m || !m->finalTex) return;
    auto f = gl();
    l.matteTarget.bind();
    f->glEnable(GL_BLEND);
    f->glBlendEquation(GL_FUNC_ADD);
    f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    f->glUseProgram(m_compProgram);
    f->glUniform1i(m_compTexLoc, 0);
    f->glUniform1i(m_compMaskLoc, 2);
    f->glUniform1i(m_compMatteLoc, 3);
    f->glUniform1i(m_compMatteModeLoc, 0);
    f->glUniform4f(m_compViewLoc, 1.0f, 1.0f, 0.0f, 0.0f); // the whole composition
    f->glUniform4f(m_compRotLoc, 1.0f, 0.0f, 0.0f, 0.0f);
    f->glUniform1f(m_compAspLoc, float(double(m_compSize.width()) / std::max(1, m_compSize.height())));
    f->glUniform1f(m_compOpacityLoc, m->opacity);
    f->glBindVertexArray(m_meshVao);
    bindMesh(*m);
    f->glUniform1i(m_compMaskOnLoc, bindCompositionMasks(*m) ? 1 : 0);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, m->finalTex);
    f->glDrawElements(GL_TRIANGLES, m_meshIndexCount, GL_UNSIGNED_INT, nullptr);
    f->glDisable(GL_BLEND);
    f->glBindVertexArray(0);
}

// A matte in Picture space: the matte layer's picture stretched over this one's (after its effects and its surface
// masks). A matte layer with no picture: nothing shows (or all, inverted).
void Engine::applyPictureMatte(Layer &l, GLuint &tex, int w, int h)
{
    if (!l.matte.layer || l.matte.space != LayerMatte::Space::Picture || !tex || w <= 0 || h <= 0) {
        if (l.mattePicture.fbo) l.mattePicture.destroy();
        return;
    }
    const Layer *m = layer(indexOfId(l.matte.layer));
    l.mattePicture.ensure(w, h);
    if (!l.mattePicture.ready(w, h)) {
        tex = 0; // fail closed: never the picture without its matte
        return;
    }
    auto f = gl();
    l.mattePicture.bind();
    f->glDisable(GL_BLEND);
    f->glUseProgram(m_matteProgram);
    f->glActiveTexture(GL_TEXTURE1);
    f->glBindTexture(GL_TEXTURE_2D, m && m->finalTex && m != &l ? m->finalTex : m_blackTex);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glUniform1i(m_matteTexLoc, 0);
    f->glUniform1i(m_matteMatteLoc, 1);
    f->glUniform1i(m_matteModeLoc, matteMode(l.matte));
    drawQuad();
    tex = l.mattePicture.tex;
}

// Its composition masks, on texture unit 2 for the compositing (false: none)
bool Engine::bindCompositionMasks(Layer &l)
{
    if (l.isViewport || !masks::any(l.masks, Mask::Attach::Composition)) return false;
    const QSize size = maskRasterSize(m_compSize.width(), m_compSize.height());
    if (l.shapeMaskCompRev != l.masksRevision || l.shapeMaskCompSize != size) {
        const QImage img = masks::raster(l.masks, Mask::Attach::Composition, size).convertToFormat(QImage::Format_RGBA8888);
        l.shapeMaskComp.upload(img.constBits(), img.width(), img.height());
        l.shapeMaskCompRev = l.masksRevision;
        l.shapeMaskCompSize = size;
    }
    auto f = gl();
    f->glActiveTexture(GL_TEXTURE2);
    f->glBindTexture(GL_TEXTURE_2D, l.shapeMaskComp.tex);
    f->glActiveTexture(GL_TEXTURE0);
    return true;
}

// A group's picture: its members composited on a transparent canvas the size of the composition
// (premultiplied), then restricted, colored and processed by its effects like any layer.
void Engine::renderGroup(Layer &g, const std::vector<Layer *> &members, const IsfRenderContext &rc)
{
    g.finalTex = 0;
    g.rawTex = 0;
    g.groupTarget.ensure(m_compSize.width(), m_compSize.height());
    if (!g.groupTarget.ready(m_compSize.width(), m_compSize.height())) return;
    compositeLayers(g.groupTarget, members);
    processLayer(g, g.groupTarget.tex, g.groupTarget.w, g.groupTarget.h, true, rc);
}

// A viewport's picture: the part of the composition its Spatial places it on, drawn at its own size, then
// with its own ROI, color and effects like a group.
void Engine::renderViewport(Layer &v, const std::vector<Layer *> &shown, const IsfRenderContext &rc)
{
    v.finalTex = v.rawTex = v.preFxTex = 0;
    if (!v.enabled) return;
    const QSize size = v.viewportSize();
    // Its test card: what goes to its screen, as it is (no ROI, color or effect)
    if (v.testCard) {
        m_testCard.render(v.name, size, v.groupTarget, m_quadVao);
        v.finalTex = v.rawTex = v.preFxTex = v.groupTarget.tex;
        v.finalW = v.rawW = v.preFxW = v.groupTarget.w;
        v.finalH = v.rawH = v.preFxH = v.groupTarget.h;
        return;
    }
    v.groupTarget.ensure(size.width(), size.height());
    if (!v.groupTarget.ready(size.width(), size.height())) return;
    // Its region: a rectangle of the composition, upright or turned
    const Mapping::Rect r = v.mapping.rect(m_compSize);
    const double cw = std::max(1, m_compSize.width()), ch = std::max(1, m_compSize.height());
    QRectF view(0, 0, r.w / cw, r.h / ch);
    view.moveCenter(QPointF(r.center.x() / cw, r.center.y() / ch));
    compositeLayers(v.groupTarget, shown, view, QColor(0, 0, 0, 0), v.id, r.angle);
    processLayer(v, v.groupTarget.tex, v.groupTarget.w, v.groupTarget.h, true, rc);
}

// Draws the layers into the target; `view` is the part of the composition the target shows
// (normalized, origin top left: the whole composition by default)
void Engine::compositeLayers(const RenderTarget &target, const std::vector<Layer *> &layers, const QRectF &view,
                             QColor clear, quint64 viewport, double angle)
{
    // A failed allocation must not accidentally draw onto framebuffer 0
    // (the output window) or overwrite an unrelated GPU target.
    if (!target.fbo) return;
    auto f = gl();
    const RenderSettings rs = effectiveRender();
    // Antialiasing: drawn into a multisampled buffer of the same size, resolved into the target at the end
    if (!m_maxSamples) f->glGetIntegerv(GL_MAX_SAMPLES, &m_maxSamples);
    const int samples = std::min(rs.samples, std::max(1, m_maxSamples));
    MsaaBuffer *ms = nullptr;
    if (samples > 1) {
        MsaaBuffer &candidate = m_msaa[{target.w, target.h}];
        candidate.ensure(target.w, target.h, samples);
        // If MSAA couldn't allocate its replacement, draw into the valid
        // ordinary target, rather than use a stale-sized or incomplete FBO.
        if (candidate.fbo && candidate.w == target.w && candidate.h == target.h &&
            candidate.samples == samples && candidate.bits == renderBits()) {
            ms = &candidate;
            ms->used = true;
        }
    }
    if (ms) {
        f->glBindFramebuffer(GL_FRAMEBUFFER, ms->fbo);
        f->glViewport(0, 0, target.w, target.h);
    } else {
        target.bind();
    }
    f->glClearColor(float(clear.redF()), float(clear.greenF()), float(clear.blueF()), float(clear.alphaF()));
    f->glClear(GL_COLOR_BUFFER_BIT);
    f->glEnable(GL_BLEND);
    f->glUseProgram(m_compProgram);
    f->glUniform1i(m_compTexLoc, 0);
    f->glUniform1i(m_compMaskLoc, 2); // composition masks (Masks.h)
    // Vertices are in the composition's clip space: scale and offset bring the view to the whole target
    const double rw = std::max(1e-6, view.width()), rh = std::max(1e-6, view.height());
    f->glUniform4f(m_compViewLoc, float(1.0 / rw), float(1.0 / rh), float((1.0 - 2.0 * view.left()) / rw - 1.0),
                   float(1.0 - (1.0 - 2.0 * view.top()) / rh));
    // The view turned about its center (the viewport's region): the composition's vertices turn the other way
    const double ang = angle * 3.14159265358979323846 / 180;
    const float rotCos = float(std::cos(ang)), rotSin = float(std::sin(ang));
    const float rotCx = float(view.center().x() * 2.0 - 1.0), rotCy = float(1.0 - view.center().y() * 2.0);
    const float asp = float(double(m_compSize.width()) / std::max(1, m_compSize.height()));
    f->glUniform4f(m_compRotLoc, rotCos, rotSin, rotCx, rotCy);
    f->glUniform1f(m_compAspLoc, asp);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindVertexArray(m_meshVao);

    // The first layer is on top: draw from last to first.
    bool inDiff = false;
    for (int i = int(layers.size()) - 1; i >= 0; --i) {
        Layer &l = *layers[size_t(i)];
        const float opacity = viewport ? l.opacity * l.opacityIn(viewport) : l.opacity;
        if (!l.enabled || l.soloHidden || !l.finalTex || opacity <= 0.0f) continue;
        bool diff = l.blend == BlendMode::Difference;
        if (diff) {
            // Difference samples the picture already on the destination. It
            // must never copy into FBO 0 (the app window), or sample a stale
            // buffer whose size/precision belongs to an earlier frame.
            const bool forcedUnavailable = s_failDifferenceCopyForTests.exchange(false);
            if (!forcedUnavailable) m_dstCopy.ensure(target.w, target.h, target.isFloat);
            const bool copyReady = !forcedUnavailable && m_dstCopy.fbo && m_dstCopy.tex &&
                                   m_dstCopy.w == target.w && m_dstCopy.h == target.h &&
                                   m_dstCopy.bits == target.bits && m_dstCopy.isFloat == target.isFloat;
            if (!copyReady) diff = false; // degraded but safe: ordinary source-over blending
        }
        if (diff) {
            // Copy what is drawn so far (resolved if antialiased), then draw the layer against the copy
            const GLuint drawFbo = ms ? ms->fbo : target.fbo;
            f->glBindFramebuffer(GL_READ_FRAMEBUFFER, drawFbo);
            f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_dstCopy.fbo);
            f->glBlitFramebuffer(0, 0, target.w, target.h, 0, 0, target.w, target.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            f->glBindFramebuffer(GL_FRAMEBUFFER, drawFbo);
            f->glDisable(GL_BLEND);
            f->glUseProgram(m_diffProgram);
            f->glUniform1i(m_diffTexLoc, 0);
            f->glUniform1i(m_diffMaskLoc, 2);
            f->glUniform1i(m_diffDstLoc, 1);
            f->glUniform4f(m_diffViewLoc, float(1.0 / rw), float(1.0 / rh), float((1.0 - 2.0 * view.left()) / rw - 1.0),
                           float(1.0 - (1.0 - 2.0 * view.top()) / rh));
            f->glUniform4f(m_diffRotLoc, rotCos, rotSin, rotCx, rotCy);
            f->glUniform1f(m_diffAspLoc, asp);
            f->glUniform1f(m_diffOpacityLoc, opacity);
            f->glActiveTexture(GL_TEXTURE1);
            f->glBindTexture(GL_TEXTURE_2D, m_dstCopy.tex);
            f->glActiveTexture(GL_TEXTURE0);
            inDiff = true;
        } else {
            if (inDiff) {
                f->glUseProgram(m_compProgram);
                f->glEnable(GL_BLEND);
                inDiff = false;
            }
            f->glBlendEquation(GL_FUNC_ADD);
            switch (l.blend) {
            case BlendMode::Add: f->glBlendFunc(GL_ONE, GL_ONE); break;
            case BlendMode::Screen: f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
            case BlendMode::Multiply: f->glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
            case BlendMode::Subtract: // dst - src (the layer's premultiplied color), alpha kept
                f->glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
                f->glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
                break;
            default: f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
            }
            f->glUniform1f(m_compOpacityLoc, opacity);
        }
        bindMesh(l);
        const bool compMasks = bindCompositionMasks(l);
        f->glUniform1i(inDiff ? m_diffMaskOnLoc : m_compMaskOnLoc, compMasks ? 1 : 0);
        // Its matte in Composition space, on texture unit 3 (prepared this frame by prepareMatte)
        const bool matte = l.matte.layer && l.matte.space == LayerMatte::Space::Composition && l.matteTarget.fbo;
        f->glUniform1i(inDiff ? m_diffMatteLoc : m_compMatteLoc, 3);
        f->glUniform1i(inDiff ? m_diffMatteModeLoc : m_compMatteModeLoc, matte ? matteMode(l.matte) : 0);
        if (matte) {
            f->glActiveTexture(GL_TEXTURE3);
            f->glBindTexture(GL_TEXTURE_2D, l.matteTarget.tex);
            f->glActiveTexture(GL_TEXTURE0);
        }
        f->glBindTexture(GL_TEXTURE_2D, l.finalTex);
        // Drawn smaller than it is: sampled from mipmaps (no shimmer, no moiré), made for this draw only
        bool mip = false;
        if (rs.mipmaps > 0 && l.finalW > 0 && l.finalH > 0) {
            const QRectF b = l.mapping.bounds();
            const double drawnW = b.width() / std::max(1e-6, view.width()) * target.w;
            const double drawnH = b.height() / std::max(1e-6, view.height()) * target.h;
            mip = drawnW < l.finalW * 0.9 || drawnH < l.finalH * 0.9;
            if (mip) {
                // Made again only when the picture can have changed (audit P2):
                //  - a plain video (no ROI, color or effect) changes only with a new frame of the file: once per frame
                //    of the video, not once per frame of the render;
                //  - any other picture is made again each frame: once per frame of the render, whatever the number of
                //    viewports drawing it (before: once per viewport)
                const bool plainVideo = l.type == SourceType::Video && l.videoTex && l.finalTex == l.videoTex->texture();
                const uint64_t serial = plainVideo ? l.videoTex->pictureSerial() : m_frameCount.load(std::memory_order_relaxed);
                const int kind = plainVideo ? 1 : 2;
                if (l.mipTex != l.finalTex || l.mipFrame != serial || l.mipKind != kind) {
                    f->glGenerateMipmap(GL_TEXTURE_2D);
                    ++m_mipmapsMade;
                    l.mipTex = l.finalTex;
                    l.mipFrame = serial;
                    l.mipKind = kind;
                }
                f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            }
        }
        f->glDrawElements(GL_TRIANGLES, m_meshIndexCount, GL_UNSIGNED_INT, nullptr);
        if (mip) f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // others sample it plainly
    }
    f->glBlendEquation(GL_FUNC_ADD);
    f->glDisable(GL_BLEND);
    // A matte is per layer: never left set for the next use of the programs
    f->glUseProgram(m_diffProgram);
    f->glUniform1i(m_diffMatteModeLoc, 0);
    f->glUseProgram(m_compProgram);
    f->glUniform1i(m_compMatteModeLoc, 0);
    f->glBindVertexArray(0);
    if (ms) ms->resolveInto(target);
}

// Mesh vertices (position, uv) of the buffer bound, for the mesh vertex array (its index buffer stays)
void Engine::bindMeshBuffer(GLuint vbo)
{
    auto f = gl();
    f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
}

// A layer's mapped mesh lives in a vertex buffer of its own, rebuilt only when its mapping changes: no vertices
// computed or sent every frame, and no draw waits for a shared buffer to be rewritten.
void Engine::bindMesh(Layer &l)
{
    auto f = gl();
    const Mapping &m = l.mapping, &was = l.meshShape;
    bool same = l.meshVbo && was.cols == m.cols && was.rows == m.rows && was.offsets == m.offsets &&
                was.position == m.position && was.size == m.size && was.rotation == m.rotation && was.pivot == m.pivot &&
                was.aspect == m.aspect;
    for (int i = 0; same && i < 4; ++i) same = was.pins[i] == m.pins[i];
    if (!l.meshVbo) f->glGenBuffers(1, &l.meshVbo);
    if (!same) {
        m.buildVertices(kMeshSubdiv, m_meshScratch);
        f->glBindBuffer(GL_ARRAY_BUFFER, l.meshVbo);
        f->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m_meshScratch.size() * sizeof(float)), m_meshScratch.data(), GL_DYNAMIC_DRAW);
        l.meshShape = m;
    }
    bindMeshBuffer(l.meshVbo);
}

// The whole composition, for the interface preview, then each viewport's output. The composition opacity and the
// blackout are applied to both, into double buffers read by the interface, the windows and the publishers.
void Engine::composite()
{
    auto f = gl();
    const double comp = m_compositionOpacity.load() * m_blackLevel.load();
    auto applyOpacity = [&] {
        if (comp >= 0.999) return;
        f->glEnable(GL_BLEND);
        const float m = float(comp);
        f->glBlendColor(m, m, m, 1.0f);
        f->glBlendFunc(GL_ZERO, GL_CONSTANT_COLOR);
        f->glUseProgram(m_blitProgram);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, m_blackTex);
        f->glUniform1i(m_blitTexLoc, 0);
        drawQuad();
        f->glDisable(GL_BLEND);
    };

    RenderTarget &out = m_output[m_back];
    out.ensure(m_compSize.width(), m_compSize.height());
    if (!out.fbo) return; // no valid target: the last published frame remains available
    std::vector<Layer *> top;
    for (auto &l : m_layers)
        if (!l->parent && !l->isViewport) top.push_back(l.get());
    compositeLayers(out, top, QRectF(0, 0, 1, 1), QColor(0, 0, 0, 255));
    out.bind();
    applyOpacity();

    for (auto &lp : m_layers) {
        Layer &v = *lp;
        if (!v.isViewport) continue;
        RenderTarget &o = v.vpOut[v.vpBack];
        const QSize size = v.viewportSize();
        o.ensure(size.width(), size.height());
        if (!o.fbo) continue; // keep the previously published viewport frame
        o.clear(0, 0, 0, 1);
        if (v.finalTex && v.opacity > 0.0f) {
            const QString &targetSpace = v.outputSpace == QLatin1String("same") ? v.outputInputSpace : v.outputSpace;
            const bool transform = targetSpace != v.outputInputSpace || v.outputExposure != 0;
            if (transform) {
                // Black-matte the viewport first, then decode/expose/encode once.
                // All consumers (screen, preview and publishers) use this same target.
                o.bind();
                f->glDisable(GL_BLEND);
                f->glUseProgram(m_outputColorProgram);
                f->glActiveTexture(GL_TEXTURE0);
                f->glBindTexture(GL_TEXTURE_2D, v.finalTex);
                f->glUniform1i(m_outputTexLoc, 0);
                f->glUniform1f(m_outputOpacityLoc, v.opacity);
                f->glUniform1i(m_outputDecodeLoc, outputColorSpaceIndex(v.outputInputSpace));
                f->glUniform1i(m_outputSourceLoc, 0);
                f->glUniform1i(m_outputEncodeLoc, outputColorSpaceIndex(targetSpace));
                f->glUniform1f(m_outputGainLoc, float(std::exp2(v.outputExposure)));
                drawQuad();
            } else {
                // The viewport's own picture fills its output, at its opacity
                o.bind();
                f->glEnable(GL_BLEND);
                f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                f->glUseProgram(m_compProgram);
                f->glUniform1i(m_compTexLoc, 0);
                f->glUniform4f(m_compViewLoc, 1, 1, 0, 0);
                f->glUniform1f(m_compOpacityLoc, v.opacity);
                // Its picture as it is: none of what the last layer composited left set in the program (its composition
                // masks, its matte, a turned view)
                f->glUniform1i(m_compMaskOnLoc, 0);
                f->glUniform1i(m_compMatteModeLoc, 0);
                f->glUniform4f(m_compRotLoc, 1, 0, 0, 0);
                f->glActiveTexture(GL_TEXTURE0);
                f->glBindVertexArray(m_meshVao);
                bindMeshBuffer(m_meshVbo); // the whole frame, built once
                f->glBindTexture(GL_TEXTURE_2D, v.finalTex);
                f->glDrawElements(GL_TRIANGLES, m_meshIndexCount, GL_UNSIGNED_INT, nullptr);
                f->glDisable(GL_BLEND);
                f->glBindVertexArray(0);
            }
        }
        o.bind();
        applyOpacity();
    }
    f->glBindVertexArray(0);
}

double Engine::nextDt()
{
    const qint64 now = m_clock.nsecsElapsed();
    double dt = (now - m_lastNs) / 1e9;
    m_lastNs = now;
    m_frameStampNs = AudioStream::clockNs();
    const FrameStep step = FrameStep::fromElapsed(dt, m_threaded);
    m_realDt = step.elapsed;
    dt = step.shaderDelta;
    if (dt > 0) m_fps = m_fps.load() * 0.95 + (1.0 / dt) * 0.05;
    return dt;
}

void Engine::frame(double dt)
{
    const uint64_t lockRequested = RealtimeTiming::nowNs();
    // In threaded mode, keep serving OpenGL tasks while the UI holds the lock:
    // it may wait on a task while holding it, without deadlock.
    if (m_threaded) {
        while (!m_mutex.tryLock(2)) {
            runPendingTasks();
            if (m_quit) return;
        }
    } else {
        m_mutex.lock();
    }
    const uint64_t lockAcquired = RealtimeTiming::nowNs();
    m_renderLockWait.record(lockAcquired - lockRequested);
    setRenderBits(effectiveRender().depth);

    // Manual faders use command-relative wall time, including render-lock waits.
    const double faderDt = m_compositionFaderClock.step(lockAcquired, m_threaded);
    const double blackoutDt = m_blackoutClock.step(lockAcquired, m_threaded);
    // Composition opacity
    double lvl = m_compositionOpacity.load();
    if (m_compositionOpacitySpeed <= 0.0) {
        lvl = m_compositionOpacityTarget;
    } else {
        const double step = faderDt * m_compositionOpacitySpeed;
        lvl = lvl < m_compositionOpacityTarget ? std::min(m_compositionOpacityTarget, lvl + step) : std::max(m_compositionOpacityTarget, lvl - step);
    }
    m_compositionOpacity = lvl;
    double black = m_blackLevel.load();
    if (m_blackSpeed <= 0.0) {
        black = m_blackTarget;
    } else {
        const double step = blackoutDt * m_blackSpeed;
        black = black < m_blackTarget ? std::min(m_blackTarget, black + step) : std::max(m_blackTarget, black - step);
    }
    m_blackLevel = black;

    IsfRenderContext rc;
    const double scale = timeScale();
    rc.dt = dt * scale;
    rc.elapsed = m_realDt * scale;
    rc.blackTex = m_blackTex;
    rc.drawQuad = [this] { drawQuad(); };
    rc.drawPoints = [this](int count) { drawPoints(count); };
    rc.blit = [this](GLuint t, const RenderTarget &rt) { blit(t, rt); };

    // Members of a hidden group are hidden (and silent) too, at any depth. A group comes before its contents.
    {
        std::map<quint64, bool> shown; // group id → visible, groups above included
        for (auto &l : m_layers) {
            const auto up = l->parent ? shown.find(l->parent) : shown.end();
            l->parentEnabled = up == shown.end() || up->second;
            if (l->isGroup) shown[l->id] = l->parentEnabled && l->enabled;
        }
    }
    if (!m_fadesManual) {
        const double k = timeScale(); // paused or slowed: the fades, transitions and timelines move at that pace
        stepShowRecalls(m_realDt * k);
    }
    stepInteractions(m_realDt); // after the animations: a controller moved wins (its own time: it follows the hand)
    followSequenceClocks();     // the steps a clock (a soundtrack's playhead, a timecode) got to: their GO
    markNeeded();
    for (auto &l : m_layers) updateSource(*l, dt);
    for (auto &[id, t] : m_transitions) updateSource(*t->from, dt); // the outgoing sources play on
    releaseRetired(false); // transitions over whose decoders are closed by now

    // Retain the last source/effect outputs, including persistent ISF buffers and
    // live input textures. dt=0 alone would still execute feedback and FRAMEINDEX.
    if (scale > 0) {
        renderPass(rc);
        for (auto &l : m_layers) l->renderPaused = false;
    } else {
        renderPausedSources(rc);
    }
    composite();
    // Only a tiny visual tap crosses to the audio engine. All GL work stays on this thread; the device callback only reads
    // atomics and performs DSP. An instrument that listens to the picture listens to one viewport's output (its own
    // setting; the first viewport by default, the composition when the project has none): one read per viewport watched.
    {
        std::map<const RenderTarget *, std::array<float, 3>> seen;
        auto watched = [this](const Layer &l) -> const RenderTarget * {
            const Layer *viewport = nullptr;
            for (const auto &candidate : m_layers) {
                if (!candidate->isViewport) continue;
                if (!viewport) viewport = candidate.get(); // the first
                if (l.sonificationViewport && candidate->id == l.sonificationViewport) {
                    viewport = candidate.get();
                    break;
                }
            }
            return viewport ? &viewport->vpOut[viewport->vpBack] : &m_output[m_back];
        };
        for (const auto &layer : m_layers)
            if (layer->audio && layer->audio->generator() && layer->audio->generator()->sonification()) seen[watched(*layer)] = {};
        if (!seen.empty()) {
            auto *f = gl();
            GLint read = 0, draw = 0, pack = 0;
            f->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            f->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
            f->glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
            m_audioVisual.ensure(16, 16);
            for (auto &[picture, rgb] : seen) {
                if (!m_audioVisual.ready(16, 16) || !picture->fbo || picture->w <= 0 || picture->h <= 0) continue;
                f->glBindFramebuffer(GL_READ_FRAMEBUFFER, picture->fbo);
                f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_audioVisual.fbo);
                f->glBlitFramebuffer(0, 0, picture->w, picture->h, 0, 0, 16, 16, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_audioVisual.fbo);
                f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                std::array<unsigned char, 16 * 16 * 4> pixels{};
                f->glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                for (size_t i = 0; i < pixels.size(); i += 4)
                    for (int c = 0; c < 3; ++c)
                        rgb[size_t(c)] += pixels[i + size_t(c)] / (255.f * 16 * 16);
            }
            f->glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(pack));
            f->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read));
            f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(draw));
            for (const auto &layer : m_layers)
                if (layer->audio && layer->audio->generator() && layer->audio->generator()->sonification()) {
                    const auto &rgb = seen[watched(*layer)];
                    layer->audio->generator()->setColor(rgb[0], rgb[1], rgb[2]);
                }
        }
    }
    stepVision();
    stepPixelMapping(); // the fixtures read the composition as it goes out (opacity and blackout included)
    // Multisampled buffers of sizes no longer drawn (antialiasing off, a size changed): released
    for (auto it = m_msaa.begin(); it != m_msaa.end();) {
        if (!it->second.used) {
            it->second.destroy();
            it = m_msaa.erase(it);
        } else {
            it->second.used = false;
            ++it;
        }
    }
    // Copy only requested pictures into bounded, double-buffered preview textures.
    // No CPU readback, decoder duplication, blocking task dispatch or per-view engine render.
    for (auto &slot : m_previewSlots) {
        // The UI may still sample this buffer on the GPU: wait there, never on the CPU.
        if (slot.reader[m_back]) {
            gl()->glWaitSync(slot.reader[m_back], 0, GL_TIMEOUT_IGNORED);
            gl()->glDeleteSync(slot.reader[m_back]);
            slot.reader[m_back] = nullptr;
        }
        slot.renderedId[m_back] = 0;
        if (!slot.id) { slot.target[m_back].destroy(); continue; }
        const int index = indexOfId(slot.id);
        if (index < 0) { slot.target[m_back].destroy(); continue; }
        const Layer &layer = *m_layers[size_t(index)];
        const RenderTarget *vp = layer.isViewport ? &layer.vpOut[layer.vpBack] : nullptr;
        const GLuint texture = vp ? vp->tex : layer.finalTex;
        QSize size = vp ? QSize(vp->w, vp->h) : QSize(layer.finalW, layer.finalH);
        if (!texture || size.isEmpty()) continue;
        if (size.width() > 1024 || size.height() > 1024) size.scale(1024, 1024, Qt::KeepAspectRatio);
        auto &target = slot.target[m_back];
        target.ensure(std::max(1, size.width()), std::max(1, size.height()));
        if (!target.fbo) continue;
        blit(texture, target);
        slot.renderedId[m_back] = slot.id;
    }
    readSourcePreview();
    // Publishing follows the viewports, however they changed (added, removed, undone, loaded, edited)
    std::vector<std::pair<quint64, PublishSettings>> publish;
    std::vector<std::pair<quint64, const RenderTarget *>> outs, previous; // this frame's pictures, the last frame's
    std::vector<std::optional<Timecode>> atc; // the timecode each viewport's pictures carry (ATC), if one is patched to it
    for (auto &l : m_layers)
        if (l->isViewport) {
            publish.push_back({l->id, l->vpPublish});
            outs.push_back({l->id, &l->vpOut[l->vpBack]});
            previous.push_back({l->id, &l->vpOut[l->vpPublished.load()]});
            const auto tc = m_atc.find(l->id);
            atc.push_back(tc != m_atc.end() ? tc->second->now() : std::nullopt);
        }
    m_mutex.unlock();
    m_renderLockHold.record(RealtimeTiming::nowNs() - lockAcquired);

    m_publishing.follow(publish);
    // NDI, OMT: read back in this context, before the frame's end
    for (size_t k = 0; k < outs.size(); ++k)
        m_publishing.readback(outs[k].first, *outs[k].second, m_fps.load(), m_frameCount.load(), atc[k]);

    auto f = gl();
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // Other contexts read these pictures: the output windows and the interface wait for this fence on the GPU
    // (waitForFrame), the CPU goes on. A texture drawn in one context is only seen whole by another once the commands
    // that drew it are complete (OpenGL spec). Delayed sharing (the default) publishes
    // the previous completed picture via the SDK. The triple viewport rotation keeps
    // that picture out of the immediately following render target. The SDK's copy-on-
    // return contract must also be validated with real Syphon and Spout receivers.
    // Immediate sharing remains available, but glFinish can stall the render thread.
    const bool shared = m_publishing.sharing();
    const bool late = shared && m_shareOneFrameLate.load(std::memory_order_relaxed);
    GLsync last = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_fenceMutex);
        if (late) last = m_frameFence; // kept for the share below (no other context holds it: they wait under the lock)
        else if (m_frameFence) f->glDeleteSync(m_frameFence);
        m_frameFence = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    if (shared && !late) f->glFinish();
    else f->glFlush(); // the fence reaches the GPU before another context waits for it
    if (late) {
        bool ready = last != nullptr;
        if (last) {
            const GLenum r = f->glClientWaitSync(last, GL_SYNC_FLUSH_COMMANDS_BIT, 4000000);
            ready = r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED;
            f->glDeleteSync(last);
        }
        if (ready) {
            for (auto &[id, out] : previous)
                if (out->tex) m_publishing.share(id, *out);
        } else {
            ++m_sharesSkipped;
        }
    } else {
        for (auto &[id, out] : outs) m_publishing.share(id, *out);
    }
    m_published = m_back;
    m_back = 1 - m_back;
    {
        Lock lk(&m_mutex);
        for (auto &l : m_layers)
            if (l->isViewport) {
                const int oldFront = l->vpPublished.load();
                const int newFront = l->vpBack;
                l->vpPublished = newFront;
                l->vpBack = Layer::nextViewportBack(oldFront, newFront, late);
            }
    }
    ++m_frameCount;
}

// The source preview, a few times per second. Read back without waiting: the picture asked for at one tick is
// collected at the next one (a quarter of a second later at most), long finished by then.
void Engine::readSourcePreview()
{
    if (!m_previewId || (m_previewTick++ % 4) != 0) return; // a few times per second is enough
    auto f = gl();
    if (m_previewPboId) {
        QImage img(m_previewPboW, m_previewPboH, QImage::Format_RGBA8888);
        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_previewPbo);
        const GLsizeiptr bytes = GLsizeiptr(m_previewPboW) * m_previewPboH * 4;
        if (const void *p = f->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT)) {
            memcpy(img.bits(), p, size_t(bytes));
            f->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            std::lock_guard<std::mutex> lk(m_previewMutex);
            m_previewImage = flippedVertically(img);
            m_previewImageId = m_previewPboId;
        }
        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        m_previewPboId = 0;
    }
    const Layer *l = nullptr;
    for (auto &x : m_layers)
        if (x->id == m_previewId) l = x.get();
    if (!l || !l->rawTex || l->rawW <= 0 || l->rawH <= 0) {
        std::lock_guard<std::mutex> lk(m_previewMutex);
        m_previewImage = QImage();
        m_previewImageId = m_previewId;
        return;
    }
    const double k = std::min(1.0, double(m_previewSide) / std::max(l->rawW, l->rawH));
    const int w = std::max(1, int(std::lround(l->rawW * k))), h = std::max(1, int(std::lround(l->rawH * k)));
    m_previewTarget.ensure(w, h);
    // A failed allocation may leave an old FBO with another size; never
    // draw the preview into framebuffer 0 or read back stale dimensions.
    if (!m_previewTarget.fbo || !m_previewTarget.tex ||
        m_previewTarget.w != w || m_previewTarget.h != h) return;
    m_previewTarget.bind();
    f->glDisable(GL_BLEND);
    f->glUseProgram(m_prepProgram); // unpremultiplied (groups), no roi, no color
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, l->rawTex);
    f->glUniform1i(m_prepTexLoc, 0);
    f->glUniform4f(m_prepRoiLoc, 0, 0, 1, 1);
    f->glUniform3f(m_prepAddLoc, 0, 0, 0);
    f->glUniform3f(m_prepRemoveLoc, 0, 0, 0);
    f->glUniform3f(m_prepBalanceLoc, 1, 1, 1);
    f->glUniform1i(m_prepUnpremulLoc, l->isGroup || l->isViewport ? 1 : 0);
    f->glUniform1i(m_prepMaskModeLoc, 0);
    drawQuad();
    if (!m_previewPbo) f->glGenBuffers(1, &m_previewPbo);
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_previewPbo);
    if (w != m_previewPboW || h != m_previewPboH) {
        f->glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(w) * h * 4, nullptr, GL_STREAM_READ);
        m_previewPboW = w;
        m_previewPboH = h;
    }
    f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_previewPboId = l->id;
}

void Engine::renderFrame()
{
    if (!m_initialized || m_threaded) return;
    if (!m_context->makeCurrent(m_surface)) return;
    m_currentSurface = m_surface;
    const qint64 t0 = FrameTiming::nowNs();
    frame(nextDt());
    const qint64 t1 = FrameTiming::nowNs();
    const bool presented = presentViewports();
    const double rate = effectiveRender().frameRate;
    m_frameTiming.noteFrame(t0, t1, FrameTiming::nowNs(), rate > 0 ? qint64(1e9 / rate) : 0, presented);
    sampleResources();
    if (!m_framePending.exchange(true)) emit frameRendered();
}

QJsonObject Engine::frameTimingJson() const
{
    QJsonObject o = m_frameTiming.stats().json();
    auto realtime = [](const RealtimeTiming::Snapshot &s) {
        return QJsonObject{{"count", double(s.count)}, {"p99_ms", s.p99Ms()},
                           {"max_ms", double(s.maxNs) / 1e6},
                           {"over_quarter_budget", double(s.overQuarter)},
                           {"over_budget", double(s.overBudget)}};
    };
    o["audio_callback_lifetime"] = realtime(m_audio->callbackTiming());
    o["render_lock_wait_lifetime"] = realtime(m_renderLockWait.snapshot());
    o["render_lock_hold_lifetime"] = realtime(m_renderLockHold.snapshot());
    o["interaction_events_dropped_lifetime"] = double(m_interactionEventsDropped.load(std::memory_order_relaxed));
    // The pace the render keeps: the frame rate asked, else the output window's screen, else the main screen's
    const double rate = effectiveRender().frameRate;
    o["main_screen_hz"] = screenRefreshRate();
    // How late the cue clock fired the steps (after their pre-wait, or the GO of the step before)
    const Sequencer::CueTiming ct = m_sequencer->cueTiming();
    auto r2 = [](double ms) { return std::round(ms * 100.0) / 100.0; };
    o["cue_late"] = QJsonObject{{"count", double(ct.fired)}, {"last_ms", r2(ct.last * 1e3)}, {"max_ms", r2(ct.max * 1e3)},
                                {"mean_ms", r2(ct.fired ? ct.total / double(ct.fired) * 1e3 : 0)},
                                {"p99_ms", r2(ct.percentile(0.99) * 1e3)}};
    // Cue clock execution time (not timer scheduling lateness). These values
    // let operators distinguish a slow Undo capture from a slow source/fade
    // application, even when the clock itself fires exactly on time.
    const Sequencer::CueWorkTiming work = m_sequencer->cueWorkTiming();
    o["cue_capture"] = work.capture.json();
    o["cue_recall"] = work.recall.json();
    o["cue_execution"] = work.total.json();
    // Each output window: its screen's rate, the pictures shown, and those not shown by cause
    QJsonArray outs;
    double paceHz = rate > 0 ? rate : 0;
    {
        std::lock_guard<std::mutex> g(m_outputStatsMutex);
        for (const OutputStats &s : m_outputStats) {
            outs.append(QJsonObject{{"name", s.name},
                                    {"metal", s.metal},
                                    {"shown", s.exposed},
                                    {"screen_hz", s.hz},
                                    {"presented", double(s.presented)},
                                    {"no_drawable", double(s.noDrawable)},
                                    {"surfaces_busy", double(s.surfacesBusy)},
                                    {"failed", double(s.failed)},
                                    {"no_picture", double(s.noPicture)},
                                    {"not_render_thread", double(s.notRenderThread)}});
            if (rate <= 0 && s.exposed && s.hz > 1) paceHz = s.hz; // the last one shown sets the pace
        }
    }
    o["outputs"] = outs;
    o["pace_hz"] = paceHz > 0 ? paceHz : screenRefreshRate();
    o["outputs_dropped"] = double(m_outputsDropped.load());
    o["gpu_shares_skipped"] = double(m_sharesSkipped.load());          // Syphon / Spout, one frame later: not ready
    o["readbacks_skipped"] = double(m_publishing.readbacksSkipped()); // NDI / OMT: the GPU not done in time
    return o;
}

void Engine::resetFrameTiming()
{
    m_frameTiming.reset();
    m_sequencer->resetCueTiming(); // the OSC timing reset must reset every reported cue histogram too
    m_outputsDropped = 0;
    m_sharesSkipped = 0;
}

// Last finished picture of one viewport (what its window and its publishers get)
QImage Engine::grabViewport(quint64 viewport)
{
    QImage img;
    runGl([this, viewport, &img] {
        Lock lk(&m_mutex);
        for (auto &l : m_layers) {
            if (!l->isViewport || l->id != viewport) continue;
            const RenderTarget &o = l->vpOut[l->vpPublished.load()];
            if (!o.fbo) return;
            img = QImage(o.w, o.h, QImage::Format_RGBA8888);
            auto f = gl();
            f->glBindFramebuffer(GL_FRAMEBUFFER, o.fbo);
            f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            f->glReadPixels(0, 0, o.w, o.h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    });
    return flippedVertically(img);
}

QImage Engine::grabOutput()
{
    QImage img;
    runGl([this, &img] {
        const RenderTarget &o = m_output[m_published];
        if (!o.fbo) return;
        img = QImage(o.w, o.h, QImage::Format_RGBA8888);
        auto f = gl();
        f->glBindFramebuffer(GL_FRAMEBUFFER, o.fbo);
        f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        f->glReadPixels(0, 0, o.w, o.h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    });
    return flippedVertically(img);
}

QImage Engine::grabLayerSource(int index)
{
    QImage img;
    runGl([this, index, &img] {
        Lock lk(&m_mutex);
        const Layer *l = layer(index);
        if (!l || !l->rawTex || l->rawW <= 0 || l->rawH <= 0) return;
        RenderTarget t;
        t.ensure(l->rawW, l->rawH);
        if (!t.fbo || !t.tex || t.w != l->rawW || t.h != l->rawH)
            return; // no valid source target: return a null image, never the default framebuffer
        blit(l->rawTex, t);
        img = QImage(t.w, t.h, QImage::Format_RGBA8888);
        auto f = gl();
        f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        f->glReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        t.destroy();
    });
    return flippedVertically(img);
}

quint64 Engine::videoFramesShown(int index) const
{
    Lock lk(&m_mutex);
    const Layer *l = index >= 0 && index < int(m_layers.size()) ? m_layers[size_t(index)].get() : nullptr;
    return l && l->videoTex ? l->videoTex->uploads() : 0;
}

// ---------------------------------------------------------------------------
// Publishing (NDI, OMT, Syphon, Spout)
// ---------------------------------------------------------------------------
void Engine::setPublishSettings(quint64 viewport, const PublishSettings &s)
{
    Lock lk(&m_mutex);
    for (auto &l : m_layers)
        if (l->isViewport && l->id == viewport) l->vpPublish = s;
}

PublishSettings Engine::publishSettings(quint64 viewport) const
{
    Lock lk(&m_mutex);
    for (const auto &l : m_layers)
        if (l->isViewport && l->id == viewport) return l->vpPublish;
    return {};
}

void Engine::setTestTap(std::function<void(const CpuFrame &)> fn) { m_publishing.setTap(std::move(fn)); }
void Engine::setGpuPublisherFactoryForTests(std::function<std::unique_ptr<GpuPublisher>(PublishKind)> f)
{
    m_publishing.setGpuPublisherFactoryForTests(std::move(f));
}

PublishState Engine::publishState(quint64 viewport, PublishKind k) const { return m_publishing.state(viewport, k); }
