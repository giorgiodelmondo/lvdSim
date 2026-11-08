// SPDX-License-Identifier: GPL-3.0-or-later
// lvdsim wave viewer demo: opens an FST file in the wave window.
#include "wave/wave_toolbar.hpp"
#include "rhi/rhi_wave_canvas.hpp"
#include "wave/wave_widget.hpp"

#include <lvdwave/fst_loader.hpp>

#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QPainter>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include <memory>

using namespace lvd;
using namespace lvd::wave;

namespace {

// One group per scope that has signals, up to a signal limit, so a big design stays usable.
void addScope(const WaveStore& st, ScopeId id, DisplayItem& parent, std::size_t& budget) {
    const Scope& sc = st.scope(id);
    DisplayItem group = DisplayModel::makeGroup(id == kRootScope ? "design" : st.scopePath(id));
    for (SignalId s : sc.signalIds) {
        if (budget == 0) break;
        group.children.push_back(DisplayModel::makeSignal(st.pathOf(s)));
        --budget;
    }
    if (!group.children.empty()) parent.children.push_back(std::move(group));
    for (ScopeId c : sc.children) {
        if (budget == 0) break;
        addScope(st, c, parent, budget);
    }
}

DisplayModel defaultModel(const WaveStore& st) {
    DisplayModel m;
    std::size_t budget = 400;
    addScope(st, kRootScope, m.root(), budget);
    return m;
}

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        wave_ = new gui::WaveWidget(this);
        wave_->setRenderBackend(gui::WaveWidget::backendFromEnvironment());
        setCentralWidget(wave_);
        addToolBar(Qt::TopToolBarArea, new gui::WaveToolBar(wave_, this));
        resize(1280, 720);

        auto* file = menuBar()->addMenu(tr("&File"));
        file->addAction(tr("&Open FST..."), QKeySequence::Open, this, [this] { openDialog(); });
        file->addAction(tr("&Save layout..."), this, [this] { saveLayout(); });
        file->addAction(tr("&Load layout..."), this, [this] { loadLayout(); });
        file->addSeparator();
        file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

        auto* view = menuBar()->addMenu(tr("&View"));
        view->addAction(tr("Zoom &full"), this, [this] { wave_->zoomFull(); });
        view->addAction(tr("Zoom &in"), this, [this] { wave_->zoomBy(1.5); });
        view->addAction(tr("Zoom &out"), this, [this] { wave_->zoomBy(1.0 / 1.5); });

        connect(wave_, &gui::WaveWidget::cursorsChanged, this, [this] { updateStatus(); });
        connect(wave_, &gui::WaveWidget::renderBackendChanged, this, [this] { updateStatus(); });
        updateStatus();
    }

    bool open(const QString& path) {
        QElapsedTimer timer;
        timer.start();
        auto r = loadFst(path.toStdString());
        if (!r.ok()) {
            QMessageBox::warning(this, tr("Open failed"), QString::fromStdString(r.error()));
            return false;
        }
        auto store = std::make_shared<WaveStore>(std::move(r).value());
        wave_->setDisplayModel(defaultModel(*store));
        wave_->setStore(store);
        loadInfo_ = tr("%1 signals, %2 changes, loaded in %3 ms")
                        .arg(store->signalCount())
                        .arg(store->totalChanges())
                        .arg(timer.elapsed());
        setWindowTitle(tr("lvdsim wave - %1").arg(path));
        updateStatus();
        return true;
    }

    gui::WaveWidget* wave() const { return wave_; }

private:
    void openDialog() {
        const QString p = QFileDialog::getOpenFileName(this, tr("Open waveform"), {}, tr("FST files (*.fst);;All files (*)"));
        if (!p.isEmpty()) open(p);
    }

    void saveLayout() {
        const QString p = QFileDialog::getSaveFileName(this, tr("Save layout"), {}, tr("Layouts (*.lvdlayout)"));
        if (p.isEmpty()) return;
        QFile f(p);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(wave_->displayModel().serialize().c_str());
    }

    void loadLayout() {
        const QString p = QFileDialog::getOpenFileName(this, tr("Load layout"), {}, tr("Layouts (*.lvdlayout)"));
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        auto parsed = DisplayModel::parse(f.readAll().toStdString());
        if (!parsed.ok()) {
            QMessageBox::warning(this, tr("Load layout"), QString::fromStdString(parsed.error()));
            return;
        }
        wave_->setDisplayModel(std::move(parsed).value());
    }

    void updateStatus() {
        QString s = loadInfo_;
        if (wave_->store() != nullptr && wave_->cursorCount() > 0) {
            const int exp = wave_->store()->timescaleExponent();
            const double active = *wave_->cursorTime(wave_->activeCursor());
            s += tr("   |   cursor %1: %2").arg(wave_->activeCursor() + 1).arg(QString::fromStdString(gui::formatTime(active, exp)));
            // Distance to the nearest other cursor.
            double best = -1;
            for (int i = 0; i < wave_->cursorCount(); ++i) {
                if (i == wave_->activeCursor()) continue;
                const double d = std::abs(*wave_->cursorTime(i) - active);
                if (best < 0 || d < best) best = d;
            }
            if (best >= 0) s += tr("   |   delta to nearest %1").arg(QString::fromStdString(gui::formatTime(best, exp)));
        }
        s += tr("   |   renderer: %1").arg(wave_->renderBackend() == gui::WaveWidget::Backend::Gpu ? (wave_->gpuCanvas()->backendName().isEmpty() ? tr("GPU") : tr("GPU (%1)").arg(wave_->gpuCanvas()->backendName())) : QString::fromUtf8(wave_->renderer().name()));
        statusBar()->showMessage(s);
    }

    gui::WaveWidget* wave_ = nullptr;
    QString loadInfo_;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    if (argc > 1) w.open(QString::fromLocal8Bit(argv[1]));

    // Headless checks: lvdsim-wave-demo trace.fst --screenshot out.png [options], applied in order:
    //   --view START END      --cursor T (repeatable, adds a cursor)    --analog NAME
    //   --radix NAME RADIX    --bits NAME        --color NAME #RRGGBB
    //   --select NAME[,NAME]  --bus LABEL        --divider LABEL        --goto TEXT (e.g. "1500 ps")
    const QStringList args = QCoreApplication::arguments();
    const int shot = static_cast<int>(args.indexOf("--screenshot"));
    if (shot > 0 && shot + 1 < args.size()) {
        gui::WaveWidget* wv = w.wave();
        auto find = [&](const QString& name) {
            for (int i = 0; i < wv->rowCount(); ++i)
                if (QString::fromStdString(wv->rowName(i)) == name) return i;
            return -1;
        };
        auto selectNames = [&](const QString& names) {
            bool first = true;
            for (const QString& n : names.split(',')) {
                const int row = find(n);
                if (row >= 0) wv->selectRow(row, !first);
                first = false;
            }
        };
        for (int i = 2; i < args.size(); ++i) {
            const QString a = args[i];
            auto next = [&](int k) { return i + k < args.size() ? args[i + k] : QString(); };
            if (a == "--cursor") {
                wv->setCursorTime(wv->cursorCount(), next(1).toDouble());
                ++i;
            } else if (a == "--view") {
                const double a0 = next(1).toDouble(), a1 = next(2).toDouble();
                i += 2;
                wv->setView(a0, (a1 - a0) / wv->waveWidth());
            } else if (a == "--analog") {
                selectNames(next(1));
                wv->toggleAnalogForSelection();
                ++i;
            } else if (a == "--radix") {
                selectNames(next(1));
                Radix r = Radix::Default;
                parseRadix(next(2).toStdString(), r);
                wv->setRadixForSelection(r);
                i += 2;
            } else if (a == "--bits") {
                const int row = find(next(1));
                if (row >= 0) wv->toggleBits(row);
                ++i;
            } else if (a == "--color") {
                selectNames(next(1));
                wv->setColorForSelection(QColor(next(2)));
                i += 2;
            } else if (a == "--select") {
                selectNames(next(1));
                ++i;
            } else if (a == "--bus") {
                wv->busFromSelection(next(1).toStdString());
                ++i;
            } else if (a == "--divider") {
                wv->insertDivider(next(1).toStdString());
                ++i;
            } else if (a == "--goto") {
                const auto t = gui::parseTime(next(1).toStdString(), wv->store()->timescaleExponent());
                if (t) wv->goToTime(*t);
                ++i;
            }
        }
        QCoreApplication::processEvents();
        if (wv->requestedBackend() == gui::WaveWidget::Backend::Gpu) {
            QElapsedTimer wait;
            wait.start();
            while (wait.elapsed() < 800) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        QImage img = w.grab().toImage();
        if (wv->renderBackend() == gui::WaveWidget::Backend::Gpu) {
            QPainter p(&img);
            p.drawImage(wv->mapTo(&w, QPoint(0, 0)), wv->snapshot());
        }
        return img.save(args[shot + 1]) ? 0 : 1;
    }
    return app.exec();
}
