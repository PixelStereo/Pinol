#include "AudioResources.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;
    const auto contents = directory.filePath("NyxMap.app/Contents");
    const auto executable = contents + "/MacOS";
    const auto resources = contents + "/Resources/audio";
    QDir().mkpath(resources + "/generators");
    // Even an old incremental build's legacy code-directory copy must not win.
    QDir().mkpath(executable + "/audio/generators");
    if (AudioResources::bundledFolder(executable) != resources) {
        std::cerr << "macOS audio resources not found\n";
        return 1;
    }
    const auto portable = directory.filePath("portable");
    QDir().mkpath(portable + "/audio/generators");
    if (AudioResources::bundledFolder(portable) != portable + "/audio") {
        std::cerr << "portable audio resources not found\n";
        return 1;
    }
    if (!AudioResources::bundledFolder(directory.filePath("missing/deeper/bin")).isEmpty())
        return 1;
    std::cout << "macOS Resources/audio and portable executable/audio lookup passed\n";
    return 0;
}
