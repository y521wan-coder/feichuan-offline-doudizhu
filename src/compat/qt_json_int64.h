#pragma once

#include <QJsonValue>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace fpdz::compat {

inline QJsonValue jsonInt64(qint64 value) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QJsonValue(value);
#else
    // Qt 5 stores JSON numbers as double. Use a decimal string for identifiers
    // so a 64-bit game ID survives a client/service round trip unchanged.
    return QJsonValue(QString::number(value));
#endif
}

inline qint64 jsonToInt64(const QJsonValue& value, qint64 fallback = 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (!value.isString()) return value.toInteger(fallback);
#endif
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        return ok ? parsed : fallback;
    }
    if (!value.isDouble()) return fallback;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < static_cast<double>(std::numeric_limits<qint64>::min()) ||
        number >= 9223372036854775808.0) {
        return fallback;
    }
    return static_cast<qint64>(number);
}

} // namespace fpdz::compat
