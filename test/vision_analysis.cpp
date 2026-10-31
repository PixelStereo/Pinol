#include "HandTracking.h"
#include "Interactions.h"
#include "PixelAnalysis.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-5; }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        using namespace PixelAnalysis;
        Settings settings;
        settings.channel = Channel::Red;
        Tracker tracker;
        std::vector<Pixel> frame(8, {0, 0, 0, 1});
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                frame[size_t(y) * 4 + x] = {1, 1, 1, 1};
        auto v = tracker.update(frame, 4, 2, settings, 1000000000);
        require(v.valid && near(v.data[0], .5) && near(v.data[1], .5) && near(v.data[2], .5) && near(v.data[3], 1),
                "mean, energy, active area and peak");
        require(near(v.data[4], .25) && near(v.data[5], .5), "weighted centroid in normalized image coordinates");
        for (auto &p : frame) {
            p[0] = p[1] = p[2] = 1 - p[0];
        }
        v = tracker.update(frame, 4, 2, settings, 1100000000);
        require(near(v.data[7], 1) && near(v.data[8], 5) && near(v.data[9], 0) && near(v.data[10], 0),
                "frame change and time-based centroid velocity");
        settings.roi = QRectF(.5, 0, .5, 1);
        v = tracker.update(frame, 4, 2, settings, 1200000000);
        require(near(v.data[0], 1) && near(v.data[4], .75) && v.data[7] == 0,
                "ROI normalization and temporal reset on configuration change");
        settings.roi = QRectF(0, 0, 1, 1);
        frame.assign(8, {.3f, .6f, .9f, .25f});
        for (int c = 0; c < 4; ++c) {
            settings.channel = Channel(c);
            v = tracker.update(frame, 4, 2, settings, 1300000000 + c);
            require(near(v.data[0], frame[0][c]), "RGBA channels");
        }
        settings.channel = Channel::Luminance;
        v = tracker.update(frame, 4, 2, settings, 1400000000);
        require(near(v.data[0], .2126 * .3 + .7152 * .6 + .0722 * .9), "luminance weighting");
        settings.channel = Channel::Chrominance;
        frame.assign(8, {.5, .5, .5, 1});
        v = tracker.update(frame, 4, 2, settings, 1500000000);
        require(v.data[0] < 1e-6, "neutral chrominance is zero");
        frame.assign(8, {1, 0, 0, 1});
        v = tracker.update(frame, 4, 2, settings, 1600000000);
        require(v.data[0] > .3, "color chrominance detected");
        frame[0][0] = std::numeric_limits<float>::quiet_NaN();
        v = tracker.update(frame, 4, 2, settings, 1700000000);
        for (double n : v.data)
            require(std::isfinite(n), "non-finite pixels are sanitized");
        require(!tracker.update(frame, 1, 1, settings, 1800000000).valid, "invalid frame dimensions refused");
        const auto normalized = Settings::fromJson(
            {{"layer", "9007199254740993"}, {"threshold", 2}, {"tap", 9}, {"channel", 99}, {"x", -.5}});
        require(normalized.layer == 9007199254740993ULL && normalized.threshold == 1 && normalized.tap == 2 &&
                    int(normalized.channel) == 5,
                "safe settings parsing and exact layer IDs");
        require(Settings::fromJson({{"tap", 0}}).tap == 1, "legacy raw-source tap migrates to pre-FX");
        for (auto source : {InteractionSource::Pixels, InteractionSource::Hands}) {
            auto input = Interaction::made(source);
            input.pixels = settings;
            input.pixels.layer = 42;
            input.pixelFeature = Feature::Energy;
            input.handIndex = 1;
            input.handFeature = HandTracking::Feature::Pinch;
            input.handConfidence = .9;
            auto copy = Interaction::fromJson(input.toJson());
            require(copy.source == source && copy.pixels == input.pixels && copy.pixelFeature == input.pixelFeature &&
                        copy.handIndex == 1 && copy.handConfidence == .9,
                    "vision source project roundtrip");
            require(!input.matches(InteractionEvent{}), "vision sources are polled, not external events");
        }
        HandTracking::Hand hand;
        hand.valid = true;
        hand.confidence = .9f;
        hand.points[0] = {.5, .9, 0};
        hand.points[9] = {.5, .6, 0};
        hand.points[4] = {.4, .3, 0};
        hand.points[8] = {.401, .3, 0};
        require(HandTracking::measure(hand, HandTracking::Feature::Pinch) > .99, "scale-normalized finger pinch");
        hand.points[4][0] = .1;
        require(HandTracking::measure(hand, HandTracking::Feature::Pinch) == 0, "open pinch");
        for (int base : {5, 9, 13, 17}) {
            hand.points[base] = {.5, .6, 0};
            hand.points[base + 3] = {.5, .1, 0};
        }
        require(HandTracking::measure(hand, HandTracking::Feature::Openness) > .9, "open fingers");
        for (int base : {5, 9, 13, 17})
            hand.points[base + 3] = hand.points[base];
        require(HandTracking::measure(hand, HandTracking::Feature::Openness) == 0, "folded fingers");
        hand.valid = false;
        require(HandTracking::measure(hand, HandTracking::Feature::Presence) == 0, "missing hand is not present");
        QTemporaryDir dir;
        QString error;
        HandTracking::Detector detector;
        require(!detector.load(dir.path(), &error) && !error.isEmpty(), "missing models report a clear error");
        if (HandTracking::Detector::available()) {
            QFile fake(dir.filePath("palm_detection_mediapipe_2023feb.onnx"));
            fake.open(QIODevice::WriteOnly);
            fake.write("invalid");
            fake.close();
            error.clear();
            require(!detector.load(dir.path(), &error) && error.contains("checksum"),
                    "invalid model rejected before inference");
        }
        HandTracking::Worker worker(dir.path());
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::white);
        worker.submit("camera", image, 1000000000);
        HandTracking::Frame result;
        for (int i = 0; i < 100; ++i) {
            result = worker.latest("camera");
            if (result.timeNs)
                break;
            QThread::msleep(5);
        }
        require(result.timeNs && !result.error.isEmpty() && !result.hands[0].valid,
                "async model failure never generates gestures");
        worker.setDirectory(dir.path());
        require(worker.latest("camera").timeNs == 0, "model changes clear stale detections");
        std::cout << "Vision statistics, temporal measurements, settings, hand metrics and unavailable-model handling "
                     "passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
