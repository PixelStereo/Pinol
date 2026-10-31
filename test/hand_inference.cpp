#include "HandTracking.h"
#include <QCoreApplication>
#include <QDir>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto directory = qEnvironmentVariable("NYXMAP_HAND_MODELS");
    if (!HandTracking::Detector::available() || directory.isEmpty()) {
        std::cout << "Set NYXMAP_HAND_MODELS to the verified ONNX model folder in an OpenCV build\n";
        return 77;
    }
    HandTracking::Detector detector;
    QString error;
    if (!detector.load(directory, &error)) {
        std::cerr << error.toStdString();
        return 1;
    }
    QImage blank(320, 240, QImage::Format_RGB32);
    blank.fill(Qt::black);
    const auto hands = detector.detect(blank, &error);
    if (!error.isEmpty() || !hands.empty()) {
        std::cerr << "Blank frame inference failed: " << error.toStdString();
        return 1;
    }
    const auto fixture = qEnvironmentVariable("NYXMAP_HAND_IMAGE");
    if (!fixture.isEmpty()) {
        QImage image(fixture);
        if (image.isNull()) {
            std::cerr << "Cannot decode hand fixture: " << fixture.toStdString();
            return 1;
        }
        const auto found = detector.detect(image, &error);
        if (!error.isEmpty() || found.empty()) {
            std::cerr << "No hand in fixture: " << error.toStdString();
            return 1;
        }
        std::cout << found.size() << " hand(s) detected in fixture\n";
    }
    std::cout << "Verified models loaded and CPU inference completed\n";
    return 0;
}
