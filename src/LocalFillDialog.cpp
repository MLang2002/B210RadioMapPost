#include "LocalFillDialog.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace b210post {

LocalFillDialog::LocalFillDialog(Callbacks callbacks, QWidget* parent)
    : QDialog(parent),
      callbacks_(std::move(callbacks)),
      methods_(createBuiltInInterpolationMethods()) {
    setWindowTitle(QStringLiteral("Local Power Fill"));
    setWindowFlag(Qt::Window, true);
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(520, 650);

    methodCombo_ = new QComboBox;
    methodCombo_->setToolTip(
        "Choose a registered interpolation module. Its parameter panel is recreated on change.");
    for (const auto& method : methods_) {
        methodCombo_->addItem(method->displayName(), method->id());
    }
    description_ = new QLabel;
    description_->setWordWrap(true);
    description_->setToolTip("Scientific meaning of the selected interpolation module.");

    auto* parameterGroup = new QGroupBox(QStringLiteral("Method Parameters"));
    parameterLayout_ = new QVBoxLayout(parameterGroup);

    auto* selectRegion = new QPushButton(QStringLiteral("Select Region on Map"));
    selectRegion->setToolTip(
        "Drag a rectangle on the main ENU map. Include both measured support and the hole to fill.");
    auto* preview = new QPushButton(QStringLiteral("Preview Interpolation"));
    preview->setToolTip(
        "Calculate a temporary overlay. No Type-A rows are changed until Confirm is clicked.");
    commitButton_ = new QPushButton(QStringLiteral("Confirm and Append"));
    commitButton_->setEnabled(false);
    commitButton_->setToolTip(
        "Append the current preview as one RX interpolation batch at the end of Type-A.");
    auto* clear = new QPushButton(QStringLiteral("Clear Preview"));
    clear->setToolTip("Remove temporary interpolation markers without changing Type-A.");
    regionLabel_ = new QLabel(QStringLiteral("Region: not selected"));
    regionLabel_->setWordWrap(true);
    regionLabel_->setToolTip("Current axis-aligned ENU selection used for source and target points.");
    statusLabel_ = new QLabel(QStringLiteral("Select a region, then preview."));
    statusLabel_->setWordWrap(true);
    statusLabel_->setToolTip("Preview counts, rejection reasons, and validation messages.");

    auto* actions = new QHBoxLayout;
    actions->addWidget(selectRegion);
    actions->addWidget(preview);
    actions->addWidget(commitButton_);
    actions->addWidget(clear);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("Interpolation mode")));
    root->addWidget(methodCombo_);
    root->addWidget(description_);
    root->addWidget(parameterGroup, 1);
    root->addWidget(regionLabel_);
    root->addLayout(actions);
    root->addWidget(statusLabel_);

    connect(methodCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        rebuildParameterWidget();
    });
    connect(selectRegion, &QPushButton::clicked, this, [this] {
        invalidatePreview(QStringLiteral("Drag a rectangle on the main map."));
        if (callbacks_.begin_region_selection) {
            callbacks_.begin_region_selection();
        }
    });
    connect(preview, &QPushButton::clicked, this, [this] {
        requestPreview();
    });
    connect(commitButton_, &QPushButton::clicked, this, [this] {
        if (callbacks_.commit) {
            callbacks_.commit();
        }
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (callbacks_.clear_preview) {
            callbacks_.clear_preview();
        }
        invalidatePreview(QStringLiteral("Preview cleared."));
    });
    rebuildParameterWidget();
}

LocalFillDialog::~LocalFillDialog() = default;

const InterpolationMethod* LocalFillDialog::currentMethod() const {
    const QString id = currentMethodId();
    for (const auto& method : methods_) {
        if (method->id() == id) {
            return method.get();
        }
    }
    return nullptr;
}

QString LocalFillDialog::currentMethodId() const {
    return methodCombo_->currentData().toString();
}

QJsonObject LocalFillDialog::currentParameters() const {
    return parameterWidget_ ? parameterWidget_->parameters() : QJsonObject{};
}

void LocalFillDialog::rebuildParameterWidget() {
    if (parameterWidget_) {
        parameterLayout_->removeWidget(parameterWidget_);
        delete parameterWidget_;
        parameterWidget_ = nullptr;
    }
    const auto* method = currentMethod();
    if (!method) {
        description_->setText(QStringLiteral("No interpolation module is registered."));
        return;
    }
    description_->setText(method->description());
    auto widget = method->createParameterWidget(nullptr);
    parameterWidget_ = widget.release();
    parameterLayout_->addWidget(parameterWidget_);
    invalidatePreview(QStringLiteral("Method changed; generate a new preview."));
}

void LocalFillDialog::setRegion(const QRectF& region) {
    region_ = region.normalized();
    regionLabel_->setText(QStringLiteral("Region: E %1 to %2 m, N %3 to %4 m (%5 × %6 m)")
                          .arg(region_.left(), 0, 'f', 3)
                          .arg(region_.right(), 0, 'f', 3)
                          .arg(region_.top(), 0, 'f', 3)
                          .arg(region_.bottom(), 0, 'f', 3)
                          .arg(region_.width(), 0, 'f', 3)
                          .arg(region_.height(), 0, 'f', 3));
    invalidatePreview(QStringLiteral("Region selected; generate a preview."));
}

void LocalFillDialog::setPreviewSummary(const QString& summary, bool canCommit) {
    statusLabel_->setText(summary);
    commitButton_->setEnabled(canCommit);
}

void LocalFillDialog::invalidatePreview(const QString& reason) {
    statusLabel_->setText(reason);
    commitButton_->setEnabled(false);
}

void LocalFillDialog::requestPreview() {
    if (region_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Select Region"),
                                 QStringLiteral("Select a non-empty region on the main map first."));
        return;
    }
    const auto* method = currentMethod();
    if (!method) {
        return;
    }
    const QJsonObject parameters = currentParameters();
    QString error;
    if (!method->validateParameters(parameters, &error)) {
        QMessageBox::warning(this, QStringLiteral("Invalid Parameters"), error);
        return;
    }
    if (callbacks_.preview) {
        callbacks_.preview(method->id(), parameters, region_);
    }
}

void LocalFillDialog::closeEvent(QCloseEvent* event) {
    if (callbacks_.clear_preview) {
        callbacks_.clear_preview();
    }
    QDialog::closeEvent(event);
}

}  // namespace b210post
