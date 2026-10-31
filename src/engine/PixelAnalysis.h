#pragma once
#include <QJsonObject>
#include <QRectF>
#include <QStringList>
#include <array>
#include <vector>

namespace PixelAnalysis {
enum class Channel { Red, Green, Blue, Alpha, Luminance, Chrominance };
enum class Feature {
    Mean,
    Energy,
    ActiveArea,
    Peak,
    CentroidX,
    CentroidY,
    Spread,
    FrameChange,
    VelocityX,
    VelocityY,
    MeanDerivative
};
QStringList channelNames();
QStringList featureNames();
struct Settings {
    quint64 layer = 0; // 0: no layer selected
    int tap = 2;       // 1: pre-FX, 2: post-FX
    Channel channel = Channel::Luminance;
    double threshold = .05;
    QRectF roi{0, 0, 1, 1}; // normalized, origin top left
    QJsonObject toJson() const;
    static Settings fromJson(const QJsonObject &json);
    bool operator==(const Settings &other) const;
};
using Pixel = std::array<float, 4>;
struct Values {
    std::array<double, 11> data{};
    double value(Feature feature) const;
    bool valid = false;
};
class Tracker {
public:
    Values update(const std::vector<Pixel> &rgba, int width, int height, const Settings &settings, qint64 timeNs);
    void clear();

private:
    Settings m_settings;
    int m_width = 0, m_height = 0;
    qint64 m_time = 0;
    Values m_previous;
    std::vector<double> m_pixels;
};
} // namespace PixelAnalysis
