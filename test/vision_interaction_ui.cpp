#include "Engine.h"
#include "InteractionPanel.h"
#include "Widgets.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QUndoStack>
#include <iostream>
#include <stdexcept>
static void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    theme::applyToApplication(app);
    Engine engine;
    QUndoStack undo;
    try {
        InteractionPanel panel(&engine, &undo);
        panel.resize(720, 1100);
        panel.show();
        panel.learnFor(0, "opacity", InteractionSource::Pixels);
        app.processEvents();
        auto *sourceChoice = panel.findChild<QComboBox *>("visionSource");
        auto *tapChoice = panel.findChild<QComboBox *>("visionTap");
        require(sourceChoice && sourceChoice->findText("Composition") == -1,
                "analysis source is a layer, without composition option");
        require(tapChoice && tapChoice->count() == 2 && tapChoice->itemData(0).toInt() == 1 &&
                    tapChoice->itemData(1).toInt() == 2,
                "pre/post FX are the only two analysis taps");
        auto *tabs = panel.findChild<QTabBar *>();
        require(tabs && tabs->tabText(tabs->currentIndex()) == "Pixels", "Pixels tab selected");
        auto *learn = panel.findChild<QToolButton *>("interactionLearn");
        require(learn && !learn->isChecked() && !learn->isEnabled(), "Pixels does not start external Learn");
        auto *channel = panel.findChild<QComboBox *>("pixelChannel");
        auto *feature = panel.findChild<QComboBox *>("pixelFeature");
        auto *source = panel.findChild<QComboBox *>("visionSource");
        require(channel && feature && source && channel->count() == 6,
                "pixel channel, feature and image selection widgets");
        channel->setCurrentIndex(3);
        emit channel->activated(3);
        require(engine.interactions()[0].pixels.channel == PixelAnalysis::Channel::Alpha,
                "channel edits reach the engine");
        auto *roi = panel.findChild<QDoubleSpinBox *>("pixelROI_width");
        require(roi, "ROI editor");
        roi->setValue(.4);
        require(engine.interactions()[0].pixels.roi.width() == .4, "ROI editor updates source");
        undo.undo();
        require(engine.interactions()[0].pixels.roi.width() == 1, "ROI change undoes");
        panel.learnFor(0, "opacity", InteractionSource::Hands);
        app.processEvents();
        require(tabs->tabText(tabs->currentIndex()) == "Hands", "Hands tab selected");
        require(panel.findChild<QComboBox *>("trackedHand") && panel.findChild<QComboBox *>("handFeature"),
                "hand selection and gesture features");
        require(!learn->isChecked() && !learn->isEnabled(), "Hands does not start external Learn");
        std::cout << "Pixels/Hands UI, source/channel/ROI editors and undo passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
