#include "PreciseDoubleSpinBox.hpp"

#include <QLocale>
#include <limits>

namespace b210post {

PreciseDoubleSpinBox::PreciseDoubleSpinBox(QWidget* parent) : QDoubleSpinBox(parent) {
    setDecimals(std::numeric_limits<double>::max_digits10);
    setKeyboardTracking(false);
}

QString PreciseDoubleSpinBox::textFromValue(double value) const {
    QString text = locale().toString(value, 'f', std::numeric_limits<double>::max_digits10);
    const QChar decimalPoint = locale().decimalPoint();
    while (text.contains(decimalPoint) && text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(decimalPoint))
        text.chop(1);
    return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

double PreciseDoubleSpinBox::valueFromText(const QString& text) const {
    bool ok = false;
    const double value = locale().toDouble(text.trimmed(), &ok);
    return ok ? value : QDoubleSpinBox::valueFromText(text);
}

}  // namespace b210post
