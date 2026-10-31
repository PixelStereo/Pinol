#include "EngineInternal.h"
#include "VisionRuntime.h"
#include <QSettings>
#include <QStandardPaths>
#include <cmath>

void Engine::stepVision()
{
    if (m_visionReset) {
        releaseVision();
        m_visionReset = false;
    }
    std::vector<VisionRuntime::Request> requests;
    auto add = [&](const Interaction &input) {
        if (input.enabled && input.pixels.layer &&
            (input.source == InteractionSource::Pixels || input.source == InteractionSource::Hands))
            requests.push_back({input.pixels, input.source == InteractionSource::Hands});
    };
    for (const auto &input : m_interactions)
        if (input.enabled) {
            add(input);
            for (const auto &axis : input.xyInputs)
                add(axis);
        }
    if (!m_vision && !requests.empty())
        m_vision = std::make_unique<VisionRuntime>([this] { drawQuad(); }, handModelsDirectory());
    if (!m_vision)
        return;
    m_vision->step(
        requests,
        [this](const PixelAnalysis::Settings &settings) -> VisionRuntime::Texture {
            if (!settings.layer)
                return {};
            const auto *source = layer(indexOfId(settings.layer));
            if (!source)
                return {};
            if (settings.tap <= 1)
                return {source->preFxTex, source->preFxW, source->preFxH};
            return {source->finalTex, source->finalW, source->finalH};
        },
        AudioStream::clockNs());
}
double Engine::visionValue(const Interaction &input)
{
    if (!m_vision || m_visionReset || !input.pixels.layer)
        return 0;
    const auto now = AudioStream::clockNs();
    if (input.source == InteractionSource::Pixels)
        return m_vision->pixelValue(input.pixels, input.pixelFeature, now);
    const auto frame = m_vision->hands(input.pixels);
    if (!frame.error.isEmpty() || !frame.timeNs || now - frame.timeNs > 500000000)
        return 0;
    if (input.handFeature == HandTracking::Feature::Count) {
        double count = 0;
        for (const auto &hand : frame.hands)
            if (hand.valid && hand.confidence >= input.handConfidence)
                ++count;
        return count;
    }
    const auto &hand = frame.hands[size_t(std::clamp(input.handIndex, 0, 1))];
    return hand.confidence >= input.handConfidence ? HandTracking::measure(hand, input.handFeature) : 0;
}
void Engine::releaseVision()
{
    if (m_vision) {
        m_vision->release();
        m_vision.reset();
    }
}
bool Engine::handTrackingAvailable() { return HandTracking::Detector::available(); }
QString Engine::handModelsDirectory() const
{
    return QSettings()
        .value(QStringLiteral("vision/handModels"),
               QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/models/hands"))
        .toString();
}
void Engine::setHandModelsDirectory(const QString &directory)
{
    QSettings().setValue(QStringLiteral("vision/handModels"), directory);
    Lock lock(&m_mutex);
    if (m_vision)
        m_vision->setModelsDirectory(directory);
}
QImage Engine::visionPreview(const PixelAnalysis::Settings &settings, bool hands) const
{
    Lock lock(&m_mutex);
    if (!m_vision || m_visionReset)
        return {};
    if (hands) {
        const auto frame = m_vision->hands(settings);
        if (!frame.preview.isNull())
            return frame.preview;
    }
    return m_vision->preview(settings);
}
QString Engine::visionStatus(const PixelAnalysis::Settings &settings, bool hands) const
{
    Lock lock(&m_mutex);
    if (!settings.layer)
        return QStringLiteral("Choose an image layer to analyze, before or after FX");
    if (hands && !handTrackingAvailable())
        return QStringLiteral("Hands require OpenCV DNN at build time");
    if (!m_vision || m_visionReset)
        return QStringLiteral("Waiting for camera / image frames");
    if (!hands)
        return m_vision->status(settings);
    const auto frame = m_vision->hands(settings);
    if (!frame.error.isEmpty())
        return frame.error;
    if (!frame.timeNs)
        return QStringLiteral("Waiting for hand detection");
    if (AudioStream::clockNs() - frame.timeNs > 500000000)
        return QStringLiteral("No recent camera frames");
    int count = 0;
    for (const auto &hand : frame.hands)
        if (hand.valid)
            ++count;
    return QStringLiteral("%1 hand(s) detected locally").arg(count);
}
