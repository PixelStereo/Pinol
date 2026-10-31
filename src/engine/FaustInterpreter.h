#pragma once
#include <QString>
#include <memory>
class dsp;
// Compile outside the device callback. The implementation loads libfaust locally
// through its C ABI so compiler LLVM symbols cannot interpose on graphics drivers.
std::unique_ptr<dsp> compileFaustProgram(const QString &source, const QString &directory, QString *error);
