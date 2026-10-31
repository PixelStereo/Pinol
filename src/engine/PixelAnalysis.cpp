#include "PixelAnalysis.h"
#include <algorithm>
#include <cmath>

QStringList PixelAnalysis::channelNames()
{
    return {"Red", "Green", "Blue", "Alpha", "Luminance", "Chrominance magnitude"};
}
QStringList PixelAnalysis::featureNames()
{
    return {"Mean / amount",
            "Energy (mean square)",
            "Active area",
            "Peak",
            "Centroid X",
            "Centroid Y",
            "Spread",
            "Frame-to-frame change",
            "Centroid velocity X / s",
            "Centroid velocity Y / s",
            "Mean derivative / s"};
}
QJsonObject PixelAnalysis::Settings::toJson() const
{
    return {{"layer", QString::number(layer)},
            {"tap", tap},
            {"channel", int(channel)},
            {"threshold", threshold},
            {"x", roi.x()},
            {"y", roi.y()},
            {"width", roi.width()},
            {"height", roi.height()}};
}
PixelAnalysis::Settings PixelAnalysis::Settings::fromJson(const QJsonObject &json)
{
    Settings s;
    s.layer = json.value("layer").toString().toULongLong();
    s.tap = std::clamp(json.value("tap").toInt(2), 1, 2);
    s.channel = Channel(std::clamp(json.value("channel").toInt(4), 0, 5));
    auto unit = [&](const char *key, double fallback) {
        double n = json.value(QLatin1String(key)).toDouble(fallback);
        return std::isfinite(n) ? std::clamp(n, 0., 1.) : fallback;
    };
    s.threshold = unit("threshold", .05);
    s.roi = QRectF(unit("x", 0), unit("y", 0), unit("width", 1), unit("height", 1)).intersected(QRectF(0, 0, 1, 1));
    return s;
}
bool PixelAnalysis::Settings::operator==(const Settings &s) const
{
    return layer == s.layer && tap == s.tap && channel == s.channel && threshold == s.threshold && roi == s.roi;
}
double PixelAnalysis::Values::value(Feature feature) const
{
    const int i = int(feature);
    return valid && i >= 0 && i < int(data.size()) ? data[size_t(i)] : 0;
}
void PixelAnalysis::Tracker::clear()
{
    m_time = 0;
    m_width = m_height = 0;
    m_previous = {};
    m_pixels.clear();
}
PixelAnalysis::Values PixelAnalysis::Tracker::update(const std::vector<Pixel> &rgba, int width, int height,
                                                     const Settings &settings, qint64 timeNs)
{
    Values out;
    if (width <= 0 || height <= 0 || size_t(width) * size_t(height) != rgba.size() || rgba.size() > 320 * 320)
        return out;
    const QRectF roi = settings.roi.intersected(QRectF(0, 0, 1, 1));
    if (roi.isEmpty())
        return out;
    const bool previous =
        m_time > 0 && timeNs > m_time && m_width == width && m_height == height && m_settings == settings;
    const double dt = previous ? (timeNs - m_time) / 1e9 : 0;
    std::vector<double> samples(rgba.size(), 0);
    double sum = 0, square = 0, peak = 0, active = 0, mass = 0, xmass = 0, ymass = 0, spread = 0, change = 0, count = 0;
    const double threshold = std::clamp(settings.threshold, 0., 1.);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const double nx = (x + .5) / width, ny = (y + .5) / height;
            if (!roi.contains(QPointF(nx, ny)))
                continue;
            const size_t i = size_t(y) * width + x;
            std::array<double, 4> p{};
            for (int c = 0; c < 4; ++c)
                p[c] = std::isfinite(rgba[i][c]) ? std::clamp(double(rgba[i][c]), 0., 1.) : 0;
            const double l = .2126 * p[0] + .7152 * p[1] + .0722 * p[2];
            double value = l;
            const int channel = int(settings.channel);
            if (channel >= 0 && channel < 4)
                value = p[size_t(channel)];
            else if (settings.channel == Channel::Chrominance)
                value = std::clamp(std::hypot((p[2] - l) * .5389, (p[0] - l) * .6350) * 1.41421356, 0., 1.);
            samples[i] = value;
            sum += value;
            square += value * value;
            peak = std::max(peak, value);
            ++count;
            if (previous)
                change += std::abs(value - m_pixels[i]);
            if (value > threshold) {
                ++active;
                mass += value;
                xmass += nx * value;
                ymass += ny * value;
                spread += (nx * nx + ny * ny) * value;
            }
        }
    if (count <= 0)
        return out;
    const double cx = mass > 0 ? xmass / mass : .5, cy = mass > 0 ? ymass / mass : .5;
    out.valid = true;
    out.data = {sum / count,
                square / count,
                active / count,
                peak,
                cx,
                cy,
                mass > 0 ? std::sqrt(std::max(0., spread / mass - cx * cx - cy * cy)) : 0,
                previous ? change / count : 0,
                0,
                0,
                0};
    if (previous && dt > 1e-6 && dt < 1) {
        out.data[10] = (out.data[0] - m_previous.data[0]) / dt;
        if (active > 0 && m_previous.data[2] > 0) {
            out.data[8] = (cx - m_previous.data[4]) / dt;
            out.data[9] = (cy - m_previous.data[5]) / dt;
        }
    }
    m_settings = settings;
    m_width = width;
    m_height = height;
    m_time = timeNs;
    m_previous = out;
    m_pixels = std::move(samples);
    return out;
}
