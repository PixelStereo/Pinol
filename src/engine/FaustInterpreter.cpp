#include "FaustInterpreter.h"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#ifdef NYXMAP_FAUST_COMPILER
#include <QCoreApplication>
#include <QLibrary>
#include <mutex>
#pragma push_macro("signals")
#undef signals
#include "faust/dsp/interpreter-dsp-c.h"
#include "faust/gui/CGlue.h"
#pragma pop_macro("signals")

namespace {
std::mutex factoryMutex;
struct Runtime {
    QLibrary library;
    QString error;
#define API(name) decltype(&name) name##Ptr = nullptr
    API(createCInterpreterDSPFactoryFromString);
    API(deleteCInterpreterDSPFactory);
    API(createCInterpreterDSPInstance);
    API(deleteCInterpreterDSPInstance);
    API(getNumInputsCInterpreterDSPInstance);
    API(getNumOutputsCInterpreterDSPInstance);
    API(initCInterpreterDSPInstance);
    API(instanceInitCInterpreterDSPInstance);
    API(instanceConstantsCInterpreterDSPInstance);
    API(instanceResetUserInterfaceCInterpreterDSPInstance);
    API(instanceClearCInterpreterDSPInstance);
    API(getSampleRateCInterpreterDSPInstance);
    API(metadataCInterpreterDSPInstance);
    API(buildUserInterfaceCInterpreterDSPInstance);
    API(computeCInterpreterDSPInstance);
#undef API
    Runtime()
    {
        // Do not export symbols: Debian libfaust embeds LLVM, which otherwise
        // interposes on Mesa's separate LLVM and can hang shader compilation.
        library.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint | QLibrary::DeepBindHint);
        for (const QString &path : {QCoreApplication::applicationDirPath() + "/faust",
                                    QStringLiteral(NYXMAP_FAUST_LIBRARY_FILE), QStringLiteral("faust")}) {
            library.setFileName(path);
            if (library.load())
                break;
        }
        if (!library.isLoaded()) {
            error = library.errorString();
            return;
        }
#define RESOLVE(name)                                                                                                  \
    name##Ptr = reinterpret_cast<decltype(name##Ptr)>(library.resolve(#name));                                         \
    if (!name##Ptr) {                                                                                                  \
        error = QStringLiteral("Missing libfaust API: ") + QStringLiteral(#name);                                      \
        return;                                                                                                        \
    }
        RESOLVE(createCInterpreterDSPFactoryFromString);
        RESOLVE(deleteCInterpreterDSPFactory);
        RESOLVE(createCInterpreterDSPInstance);
        RESOLVE(deleteCInterpreterDSPInstance);
        RESOLVE(getNumInputsCInterpreterDSPInstance);
        RESOLVE(getNumOutputsCInterpreterDSPInstance);
        RESOLVE(initCInterpreterDSPInstance);
        RESOLVE(instanceInitCInterpreterDSPInstance);
        RESOLVE(instanceConstantsCInterpreterDSPInstance);
        RESOLVE(instanceResetUserInterfaceCInterpreterDSPInstance);
        RESOLVE(instanceClearCInterpreterDSPInstance);
        RESOLVE(getSampleRateCInterpreterDSPInstance);
        RESOLVE(metadataCInterpreterDSPInstance);
        RESOLVE(buildUserInterfaceCInterpreterDSPInstance);
        RESOLVE(computeCInterpreterDSPInstance);
#undef RESOLVE
    }
};
Runtime &runtime()
{
    static Runtime value;
    return value;
}
class Program final : public dsp {
    Runtime &api;
    interpreter_dsp_factory *factory;
    interpreter_dsp *instance;

public:
    Program(Runtime &r, interpreter_dsp_factory *f, interpreter_dsp *i) : api(r), factory(f), instance(i) {}
    ~Program() override
    {
        std::lock_guard<std::mutex> lock(factoryMutex);
        api.deleteCInterpreterDSPInstancePtr(instance);
        api.deleteCInterpreterDSPFactoryPtr(factory);
    }
    int getNumInputs() override { return api.getNumInputsCInterpreterDSPInstancePtr(instance); }
    int getNumOutputs() override { return api.getNumOutputsCInterpreterDSPInstancePtr(instance); }
    int getSampleRate() override { return api.getSampleRateCInterpreterDSPInstancePtr(instance); }
    void init(int rate) override { api.initCInterpreterDSPInstancePtr(instance, rate); }
    void instanceInit(int rate) override { api.instanceInitCInterpreterDSPInstancePtr(instance, rate); }
    void instanceConstants(int rate) override { api.instanceConstantsCInterpreterDSPInstancePtr(instance, rate); }
    void instanceResetUserInterface() override { api.instanceResetUserInterfaceCInterpreterDSPInstancePtr(instance); }
    void instanceClear() override { api.instanceClearCInterpreterDSPInstancePtr(instance); }
    dsp *clone() override { return nullptr; } // NyxMap creates independent factories/instances at load time.
    void metadata(Meta *meta) override
    {
        MetaGlue glue{meta,
                      [](void *p, const char *key, const char *value) { static_cast<Meta *>(p)->declare(key, value); }};
        api.metadataCInterpreterDSPInstancePtr(instance, &glue);
    }
    void buildUserInterface(UI *ui) override
    {
        UIGlue glue{};
        buildUIGlue(&glue, ui, false);
        api.buildUserInterfaceCInterpreterDSPInstancePtr(instance, &glue);
    }
    void compute(int count, float **in, float **out) override
    {
        api.computeCInterpreterDSPInstancePtr(instance, count, in, out);
    }
};
} // namespace
#endif
std::unique_ptr<dsp> compileFaustProgram(const QString &source, const QString &directory, QString *error)
{
#ifdef NYXMAP_FAUST_COMPILER
    auto &api = runtime();
    if (!api.error.isEmpty()) {
        if (error)
            *error = api.error;
        return {};
    }
    std::lock_guard<std::mutex> lock(factoryMutex);
    char message[4096] = {};
    const auto text = source.toUtf8(), path = directory.toUtf8();
    const char *args[] = {"-I", path.constData(), "-I", NYXMAP_FAUST_LIBRARY_PATH};
    auto *factory = api.createCInterpreterDSPFactoryFromStringPtr("NyxMap", text.constData(), 4, args, message);
    if (!factory) {
        if (error)
            *error = QString::fromUtf8(message);
        return {};
    }
    auto *instance = api.createCInterpreterDSPInstancePtr(factory);
    if (!instance) {
        api.deleteCInterpreterDSPFactoryPtr(factory);
        if (error)
            *error = "Cannot create Faust DSP instance";
        return {};
    }
    return std::make_unique<Program>(api, factory, instance);
#else
    Q_UNUSED(source);
    Q_UNUSED(directory);
    if (error)
        *error = "This build has no libfaust interpreter; built-in audio instruments remain available";
    return {};
#endif
}
