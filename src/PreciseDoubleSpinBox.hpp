#ifndef __B210_PRECISE_DOUBLE_SPINBOX_HPP
#define __B210_PRECISE_DOUBLE_SPINBOX_HPP

#include <QDoubleSpinBox>

namespace b210post {

class PreciseDoubleSpinBox : public QDoubleSpinBox {
  public:
    explicit PreciseDoubleSpinBox(QWidget* parent = nullptr);

  protected:
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
};

}  // namespace b210post

#endif // __B210_PRECISE_DOUBLE_SPINBOX_HPP
