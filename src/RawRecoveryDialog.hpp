#ifndef __B210_RAW_RECOVERY_DIALOG_HPP
#define __B210_RAW_RECOVERY_DIALOG_HPP

#include <QDialog>

#include "Types.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace b210post {

class RawRecoveryDialog : public QDialog {
    Q_OBJECT

  public:
    RawRecoveryDialog(const EnuReference& fallback, QWidget* parent = nullptr);

  private slots:
    void choose();
    void load();
    void save();

  private:
    EnuReference fallback_;
    RadioDataset recovered_;
    QLineEdit* path_;
    QLabel* summary_;
    QTableWidget* table_;
    QPushButton* save_;
};

}  // namespace b210post

#endif // __B210_RAW_RECOVERY_DIALOG_HPP
