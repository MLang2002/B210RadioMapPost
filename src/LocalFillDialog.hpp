#ifndef __B210_LOCALFILL_DIALOG_HPP
#define __B210_LOCALFILL_DIALOG_HPP

#include <QDialog>
#include <QJsonObject>
#include <QRectF>
#include <functional>
#include <memory>
#include <vector>

#include "Interpolation.hpp"

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace b210post {

class LocalFillDialog : public QDialog {
  public:
    struct Callbacks {
        std::function<void()> begin_region_selection;
        std::function<void(const QString&, const QJsonObject&, const QRectF&)> preview;
        std::function<void()> commit;
        std::function<void()> clear_preview;
    };

    explicit LocalFillDialog(Callbacks callbacks, QWidget* parent = nullptr);
    ~LocalFillDialog() override;

    void setRegion(const QRectF& region);
    void setPreviewSummary(const QString& summary, bool canCommit);
    void invalidatePreview(const QString& reason);
    QJsonObject currentParameters() const;
    QString currentMethodId() const;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void rebuildParameterWidget();
    const InterpolationMethod* currentMethod() const;
    void requestPreview();

    Callbacks callbacks_;
    std::vector<std::unique_ptr<InterpolationMethod>> methods_;
    QComboBox* methodCombo_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* regionLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QVBoxLayout* parameterLayout_ = nullptr;
    InterpolationParameterWidget* parameterWidget_ = nullptr;
    QPushButton* commitButton_ = nullptr;
    QRectF region_;
};

}  // namespace b210post

#endif // __B210_LOCALFILL_DIALOG_HPP