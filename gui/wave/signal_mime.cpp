// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/signal_mime.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

namespace lvd::gui {

QString addWavePathFromStorePath(const QString& dotted) {
    QString p = dotted;
    p.replace('.', '/');
    return QLatin1Char('/') + p;
}

QMimeData* makeSignalMime(const QStringList& addWavePaths) {
    auto* mime = new QMimeData;
    QJsonArray list;
    for (const QString& p : addWavePaths)
        list.append(p);
    QJsonObject root;
    root.insert(QStringLiteral("v"), 1);
    root.insert(QStringLiteral("signals"), list);
    mime->setData(QLatin1String(kSignalPathsMime), QJsonDocument(root).toJson(QJsonDocument::Compact));
    mime->setText(addWavePaths.join(QLatin1Char('\n')));
    return mime;
}

bool hasSignalPaths(const QMimeData* mime) {
    return mime != nullptr && mime->hasFormat(QLatin1String(kSignalPathsMime));
}

QStringList signalPathsFromMime(const QMimeData* mime) {
    QStringList out;
    if (!hasSignalPaths(mime))
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(mime->data(QLatin1String(kSignalPathsMime)));
    if (!doc.isObject())
        return out;
    for (const QJsonValue& v : doc.object().value(QStringLiteral("signals")).toArray())
        if (v.isString() && !v.toString().isEmpty())
            out << v.toString();
    return out;
}

} // namespace lvd::gui
