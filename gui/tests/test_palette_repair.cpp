// SPDX-License-Identifier: GPL-3.0-or-later
// Placeholder text stays readable whatever palette the desktop theme hands over (issue: black hints on a
// dark XFCE/GTK palette made the Go to, Filter, Find and console hints invisible).
#include "app/palette_repair.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QtTest>

using namespace lvd::gui;

namespace {

/// The palette seen on XFCE with the GTK platform theme once the window is shown.
QPalette brokenDark() {
    QPalette p;
    for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(g, QPalette::Window, QColor(0x0e, 0x0b, 0x0d));
        p.setColor(g, QPalette::Base, QColor(0x0e, 0x0b, 0x0d));
        p.setColor(g, QPalette::Text, QColor(0xd0, 0xd0, 0xdd));
        p.setColor(g, QPalette::PlaceholderText, QColor(0, 0, 0));
    }
    return p;
}

double placeholderContrast(const QPalette& p, QPalette::ColorGroup g) {
    return contrastRatio(p.color(g, QPalette::PlaceholderText), p.color(g, QPalette::Base));
}

} // namespace

class TestPaletteRepair : public QObject {
    Q_OBJECT

private slots:
    void contrastRatioOfKnownPairs() {
        QVERIFY(qAbs(contrastRatio(Qt::black, Qt::white) - 21.0) < 0.01);
        QVERIFY(qAbs(contrastRatio(Qt::white, Qt::black) - 21.0) < 0.01);
        QVERIFY(qAbs(contrastRatio(QColor(0x80, 0x80, 0x80), QColor(0x80, 0x80, 0x80)) - 1.0) < 1e-9);
    }

    void blackPlaceholderOnDarkBaseIsRepaired() {
        QPalette p = brokenDark();
        QVERIFY(placeholderContrast(p, QPalette::Active) < kMinPlaceholderContrast);
        QVERIFY(repairPalette(p));
        for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            QVERIFY2(placeholderContrast(p, g) >= kMinPlaceholderContrast,
                     qPrintable(QString("group %1: contrast %2").arg(int(g)).arg(placeholderContrast(p, g))));
            QVERIFY(p.color(g, QPalette::PlaceholderText) != p.color(g, QPalette::Text)); // still dimmer
        }
        QVERIFY(!repairPalette(p)); // nothing left to do: no endless palette change events
    }

    void readablePalettesAreLeftAlone() {
        QPalette light;
        for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            light.setColor(g, QPalette::Base, Qt::white);
            light.setColor(g, QPalette::Text, Qt::black);
            light.setColor(g, QPalette::PlaceholderText, QColor(0x70, 0x70, 0x70));
        }
        const QPalette before = light;
        QVERIFY(!repairPalette(light));
        QCOMPARE(light, before);

        QPalette dark = brokenDark();
        for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
            dark.setColor(g, QPalette::PlaceholderText, QColor(0x6c, 0x6c, 0x79));
        QVERIFY(!repairPalette(dark));
    }

    // The application palette is repaired when the theme changes it after start-up, and a line edit then
    // draws its hint in the repaired colour.
    void applicationPaletteIsRepairedAfterAThemeChange() {
        installPaletteRepair(*qApp);
        QApplication::setPalette(brokenDark()); // what the theme does once the window is up
        QCoreApplication::processEvents();
        QVERIFY(placeholderContrast(QApplication::palette(), QPalette::Active) >= kMinPlaceholderContrast);
        QLineEdit edit;
        QVERIFY(placeholderContrast(edit.palette(), QPalette::Active) >= kMinPlaceholderContrast);

        // A later switch to a light palette gets a matching placeholder again.
        QPalette light;
        for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            light.setColor(g, QPalette::Base, Qt::white);
            light.setColor(g, QPalette::Text, Qt::black);
        }
        QApplication::setPalette(light);
        QCoreApplication::processEvents();
        const QColor ph = QApplication::palette().color(QPalette::Active, QPalette::PlaceholderText);
        QCOMPARE(ph, QColor(0x7f, 0x7f, 0x7f));
    }
};

QTEST_MAIN(TestPaletteRepair)
#include "test_palette_repair.moc"
