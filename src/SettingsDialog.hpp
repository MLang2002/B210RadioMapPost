#ifndef __B210_SETTINGS_DIALOG_HPP
#define __B210_SETTINGS_DIALOG_HPP

#include <QDialog>

#include "Types.hpp"

class QDoubleSpinBox;
class QSpinBox;

namespace b210post {

class SettingsDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsDialog(const AppSettings& settings, QWidget* parent = nullptr);
    AppSettings settings() const;

  private:
    AppSettings base_;
    QDoubleSpinBox* lat_;
    QDoubleSpinBox* lon_;
    QDoubleSpinBox* alt_;
    QDoubleSpinBox* opacity_;
    QSpinBox* pointSize_;
};

}  // namespace b210post

#endif // __B210_SETTINGS_DIALOG_HPP
