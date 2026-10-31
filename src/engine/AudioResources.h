#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace AudioResources {
// Data belongs in Contents/Resources in a signed macOS bundle. The executable
// directory remains the resource root for Windows/Linux portable builds.
inline QString bundledFolder(const QString &applicationDirectory = QCoreApplication::applicationDirPath())
{
    for (const auto &candidate : {applicationDirectory + "/../Resources/audio", applicationDirectory + "/audio",
                                  applicationDirectory + "/../share/nyxmap/audio", applicationDirectory + "/../audio"})
        if (QFileInfo(candidate).isDir())
            return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
    return {};
}
inline QString generatorsFolder()
{
    const auto root = bundledFolder();
    return root.isEmpty() ? QString() : QDir(root).filePath("generators");
}
} // namespace AudioResources
