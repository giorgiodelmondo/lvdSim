// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/palette_repair.hpp"

#include <QApplication>
#include <QEvent>
#include <QObject>

#include <algorithm>
#include <cmath>

namespace lvd::gui {

namespace {

double luminance(const QColor& c) {
    auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

QColor halfWay(const QColor& a, const QColor& b) {
    return QColor((a.red() + b.red()) / 2, (a.green() + b.green()) / 2, (a.blue() + b.blue()) / 2);
}

constexpr QPalette::ColorGroup kGroups[] = {QPalette::Active, QPalette::Inactive, QPalette::Disabled};

/// Placeholder colour of every group from its Text and Base. Returns true when one was different.
bool setPlaceholders(QPalette& palette) {
    bool changed = false;
    for (QPalette::ColorGroup g : kGroups) {
        const QColor want = halfWay(palette.color(g, QPalette::Text), palette.color(g, QPalette::Base));
        if (palette.color(g, QPalette::PlaceholderText) != want) {
            palette.setColor(g, QPalette::PlaceholderText, want);
            changed = true;
        }
    }
    return changed;
}

/// Keeps the application palette repaired. Once the placeholder colour has been replaced it is an explicit
/// part of the application palette, which the theme no longer updates, so from then on it is derived again
/// from Text and Base after every change (a switch to a light theme gets a matching grey).
class PaletteRepair : public QObject {
public:
    explicit PaletteRepair(QApplication& app)
        : QObject(&app) {
        apply();
        app.installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange && !applying_)
            apply();
        return false;
    }

private:
    void apply() {
        QPalette p = QApplication::palette();
        const bool changed = owned_ ? setPlaceholders(p) : repairPalette(p);
        if (!changed)
            return;
        owned_ = true;
        applying_ = true;
        QApplication::setPalette(p);
        applying_ = false;
    }

    bool owned_ = false;
    bool applying_ = false;
};

} // namespace

double contrastRatio(const QColor& a, const QColor& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

bool repairPalette(QPalette& palette) {
    bool poor = false;
    for (QPalette::ColorGroup g : kGroups)
        poor = poor || contrastRatio(palette.color(g, QPalette::PlaceholderText),
                                     palette.color(g, QPalette::Base)) < kMinPlaceholderContrast;
    return poor && setPlaceholders(palette);
}

void installPaletteRepair(QApplication& app) {
    new PaletteRepair(app);
}

} // namespace lvd::gui
