// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/icons.hpp"
#include "wave/wave_toolbar.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QToolBar>
#include <QtTest>

using namespace lvd::gui;

namespace {

// Fraction of pixels that are not transparent.
double coverage(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 40)
                ++n;
    return static_cast<double>(n) / (img.width() * img.height());
}

QImage image(Icon i, int size, const QColor& fg = Qt::black) {
    return renderIcon(i, size, fg).toImage();
}

} // namespace

class TestIcons : public QObject {
    Q_OBJECT
private slots:
    void everyIconDrawsSomethingAtEverySize() {
        for (int i = 0; i < kIconCount; ++i) {
            for (int size : {16, 20, 24, 32, 48}) {
                const QImage img = image(static_cast<Icon>(i), size);
                QCOMPARE(img.width(), size);
                const double c = coverage(img);
                QVERIFY2(
                    c > 0.04,
                    qPrintable(QString("icon %1 at %2 px is nearly empty (%3)").arg(i).arg(size).arg(c)));
                QVERIFY2(c < 0.75,
                         qPrintable(QString("icon %1 at %2 px is a blob (%3)").arg(i).arg(size).arg(c)));
            }
        }
    }

    void iconsAreDistinct() {
        for (int a = 0; a < kIconCount; ++a)
            for (int b = a + 1; b < kIconCount; ++b)
                QVERIFY2(image(static_cast<Icon>(a), 24) != image(static_cast<Icon>(b), 24),
                         qPrintable(QString("icons %1 and %2 look the same").arg(a).arg(b)));
    }

    void accentColoursAreFixedAndOthersFollowTheTheme() {
        auto dominant = [](const QImage& img) {
            long r = 0, g = 0, b = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QColor c = img.pixelColor(x, y);
                    if (c.alpha() > 200) {
                        r += c.red();
                        g += c.green();
                        b += c.blue();
                    }
                }
            return std::array<long, 3>{r, g, b};
        };

        // run is green and stop is red whatever the theme
        for (const QColor& fg : {QColor(Qt::black), QColor(Qt::white)}) {
            const auto run = dominant(image(Icon::Run, 32, fg));
            QVERIFY(run[1] > run[0] && run[1] > run[2]);
            const auto stop = dominant(image(Icon::Stop, 32, fg));
            QVERIFY(stop[0] > stop[1] && stop[0] > stop[2]);
        }

        // outline icons use the given text colour: white on a dark theme, black on a light one
        const auto onDark = dominant(image(Icon::ZoomFit, 32, Qt::white));
        const auto onLight = dominant(image(Icon::ZoomFit, 32, Qt::black));
        QVERIFY(onDark[0] > 0 && onLight[0] == 0);
        QVERIFY(image(Icon::ZoomFit, 32, Qt::white) != image(Icon::ZoomFit, 32, Qt::black));
    }

    void iconsAreSharpAtEveryScale() {
        // the same drawing scaled: a larger icon has proportionally similar coverage, not a different shape
        for (int i = 0; i < kIconCount; ++i) {
            const double small = coverage(image(static_cast<Icon>(i), 16));
            const double large = coverage(image(static_cast<Icon>(i), 48));
            QVERIFY2(std::fabs(small - large) < 0.14,
                     qPrintable(QString("icon %1: %2 vs %3").arg(i).arg(small).arg(large)));
        }
    }

    void makeIconHasSeveralSizesAndADisabledLook() {
        const QIcon icon = makeIcon(Icon::Run, QApplication::palette());
        QVERIFY(!icon.isNull());
        QVERIFY(icon.availableSizes().size() >= 4);
        QVERIFY(!icon.pixmap(QSize(24, 24)).isNull());
        QVERIFY(icon.pixmap(QSize(24, 24), QIcon::Disabled).toImage() !=
                icon.pixmap(QSize(24, 24)).toImage());
    }

    void toolbarsShowIconsOnly() {
        WaveWidget wave;
        WaveToolBar bar(&wave);
        QCOMPARE(bar.toolButtonStyle(), Qt::ToolButtonIconOnly);
        int actions = 0;
        for (QAction* a : bar.actions()) {
            if (a->isSeparator() || a->objectName().isEmpty())
                continue;
            ++actions;
            QVERIFY2(!a->icon().isNull(), qPrintable(a->objectName() + " has no icon"));
            QVERIFY2(!a->toolTip().isEmpty(), qPrintable(a->objectName() + " has no tooltip"));
            QVERIFY2(!a->text().isEmpty(),
                     qPrintable(a->objectName() + " has no text for menus and accessibility"));
        }
        QCOMPARE(actions, 11);
    }
};

QTEST_MAIN(TestIcons)
#include "test_icons.moc"
