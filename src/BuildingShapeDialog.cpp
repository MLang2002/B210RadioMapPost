#include "BuildingShapeDialog.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

#include "PreciseDoubleSpinBox.hpp"

namespace b210post {
namespace {

constexpr double kCoordinateLimit = 1.0e12;
constexpr double kMinimumDimension = 1.0e-17;

PreciseDoubleSpinBox* preciseSpin(double minimum, double maximum, double value, double step,
                                  const QString& tooltip) {
    auto* spin = new PreciseDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setValue(value);
    spin->setToolTip(tooltip);
    return spin;
}

class RectangleParameterWidget final : public ShapeParameterWidget {
  public:
    explicit RectangleParameterWidget(QWidget* parent = nullptr) : ShapeParameterWidget(parent) {
        east_ = preciseSpin(-kCoordinateLimit, kCoordinateLimit, 0.0, 0.1, "Rectangle center East.");
        north_ = preciseSpin(-kCoordinateLimit, kCoordinateLimit, 0.0, 0.1, "Rectangle center North.");
        width_ = preciseSpin(kMinimumDimension, kCoordinateLimit, 10.0, 0.1, "Rectangle width.");
        height_ = preciseSpin(kMinimumDimension, kCoordinateLimit, 10.0, 0.1, "Rectangle height.");
        rotation_ = preciseSpin(-360.0, 360.0, 0.0, 1.0,
                                "Counter-clockwise rotation around the rectangle center.");
        linearStep_ = preciseSpin(kMinimumDimension, kCoordinateLimit, 0.1, 0.01,
                                  "Step used by center and size spin boxes.");
        angleStep_ =
            preciseSpin(kMinimumDimension, 360.0, 1.0, 0.1, "Step used by the rotation spin box.");

        auto* form = new QFormLayout;
        form->addRow("Center East [m]", east_);
        form->addRow("Center North [m]", north_);
        form->addRow("Width [m]", width_);
        form->addRow("Height [m]", height_);
        form->addRow("Rotation [deg CCW]", rotation_);
        form->addRow("Linear step", linearStep_);
        form->addRow("Angle step", angleStep_);

        auto* edges = new QGroupBox("Edge discretization (count includes both vertices)");
        // clang-analyzer-cplusplus.NewDeleteLeaks
        auto* edgeGrid = new QGridLayout(edges);
        const std::array<QString, 4> names{{"South", "East", "North", "West"}};
        for (std::size_t index = 0; index < edgeChecks_.size(); ++index) {
            edgeChecks_[index] = new QCheckBox(QString("%1 internal points").arg(names[index]));
            edgeCounts_[index] = new QSpinBox;
            edgeCounts_[index]->setRange(2, 100000);
            edgeCounts_[index]->setValue(2);
            edgeCounts_[index]->setEnabled(false);
            edgeCounts_[index]->setToolTip(
                "Total equally spaced points on this edge, including both corner vertices.");
            edgeGrid->addWidget(edgeChecks_[index], static_cast<int>(index), 0);
            edgeGrid->addWidget(edgeCounts_[index], static_cast<int>(index), 1);
            connect(edgeChecks_[index], &QCheckBox::toggled, edgeCounts_[index], &QSpinBox::setEnabled);
            connect(edgeChecks_[index], &QCheckBox::toggled, this,
                    &ShapeParameterWidget::parametersChanged);
            connect(edgeCounts_[index], QOverload<int>::of(&QSpinBox::valueChanged), this,
                    &ShapeParameterWidget::parametersChanged);
        }

        auto* root = new QVBoxLayout(this);
        root->addLayout(form);
        root->addWidget(edges);
        root->addStretch(1);

        for (auto* spin : {
                    east_, north_, width_, height_, rotation_, linearStep_, angleStep_
                })
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    &ShapeParameterWidget::parametersChanged);
        connect(linearStep_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double step) {
            for (auto* spin : {
                        east_, north_, width_, height_
                    })
                spin->setSingleStep(step);
        });
        connect(angleStep_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), rotation_,
                &QDoubleSpinBox::setSingleStep);
    }

    BuildingShapeKind shapeKind() const override {
        return BuildingShapeKind::Rectangle;
    }

    BuildingShapeSnapshot snapshot() const override {
        RectangleShapeSnapshot result;
        result.center_east_m = east_->value();
        result.center_north_m = north_->value();
        result.width_m = width_->value();
        result.height_m = height_->value();
        result.rotation_deg = rotation_->value();
        result.linear_step = linearStep_->value();
        result.angle_step = angleStep_->value();
        for (std::size_t index = 0; index < result.discretize_edge.size(); ++index) {
            result.discretize_edge[index] = edgeChecks_[index]->isChecked();
            result.points_per_edge[index] = edgeCounts_[index]->value();
        }
        return result;
    }

    void restore(const BuildingShapeSnapshot& value) override {
        const auto& snapshot = std::get<RectangleShapeSnapshot>(value);
        const QSignalBlocker b1(east_), b2(north_), b3(width_), b4(height_), b5(rotation_),
              b6(linearStep_), b7(angleStep_);
        east_->setValue(snapshot.center_east_m);
        north_->setValue(snapshot.center_north_m);
        width_->setValue(snapshot.width_m);
        height_->setValue(snapshot.height_m);
        rotation_->setValue(snapshot.rotation_deg);
        linearStep_->setValue(snapshot.linear_step);
        angleStep_->setValue(snapshot.angle_step);
        for (std::size_t index = 0; index < snapshot.discretize_edge.size(); ++index) {
            const QSignalBlocker checkBlocker(edgeChecks_[index]);
            const QSignalBlocker countBlocker(edgeCounts_[index]);
            edgeChecks_[index]->setChecked(snapshot.discretize_edge[index]);
            edgeCounts_[index]->setValue(snapshot.points_per_edge[index]);
            edgeCounts_[index]->setEnabled(snapshot.discretize_edge[index]);
        }
        for (auto* spin : {
                    east_, north_, width_, height_
                })
            spin->setSingleStep(snapshot.linear_step);
        rotation_->setSingleStep(snapshot.angle_step);
    }

  private:
    PreciseDoubleSpinBox *east_ = nullptr, *north_ = nullptr, *width_ = nullptr, *height_ = nullptr,
                          *rotation_ = nullptr, *linearStep_ = nullptr, *angleStep_ = nullptr;
    std::array<QCheckBox*, 4> edgeChecks_{};
    std::array<QSpinBox*, 4> edgeCounts_{};
};

class CircleParameterWidget final : public ShapeParameterWidget {
  public:
    explicit CircleParameterWidget(QWidget* parent = nullptr) : ShapeParameterWidget(parent) {
        east_ = preciseSpin(-kCoordinateLimit, kCoordinateLimit, 0.0, 0.1, "Circle center East.");
        north_ = preciseSpin(-kCoordinateLimit, kCoordinateLimit, 0.0, 0.1, "Circle center North.");
        radius_ = preciseSpin(kMinimumDimension, kCoordinateLimit, 5.0, 0.1, "Circle radius.");
        linearStep_ = preciseSpin(kMinimumDimension, kCoordinateLimit, 0.1, 0.01,
                                  "Step used by center and radius spin boxes.");
        pointCount_ = new QSpinBox;
        pointCount_->setRange(3, 4096);
        pointCount_->setValue(32);
        pointCount_->setToolTip("Number of equally spaced points generated around the circumference.");
        auto* form = new QFormLayout(this);
        form->addRow("Center East [m]", east_);
        form->addRow("Center North [m]", north_);
        form->addRow("Radius [m]", radius_);
        form->addRow("Linear step", linearStep_);
        form->addRow("Circumference points", pointCount_);
        for (auto* spin : {
                    east_, north_, radius_, linearStep_
                })
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    &ShapeParameterWidget::parametersChanged);
        connect(pointCount_, QOverload<int>::of(&QSpinBox::valueChanged), this,
                &ShapeParameterWidget::parametersChanged);
        connect(linearStep_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double step) {
            for (auto* spin : {
                        east_, north_, radius_
                    })
                spin->setSingleStep(step);
        });
    }

    BuildingShapeKind shapeKind() const override {
        return BuildingShapeKind::Circle;
    }

    BuildingShapeSnapshot snapshot() const override {
        CircleShapeSnapshot result;
        result.center_east_m = east_->value();
        result.center_north_m = north_->value();
        result.radius_m = radius_->value();
        result.circumference_points = pointCount_->value();
        result.linear_step = linearStep_->value();
        return result;
    }

    void restore(const BuildingShapeSnapshot& value) override {
        const auto& snapshot = std::get<CircleShapeSnapshot>(value);
        const QSignalBlocker b1(east_), b2(north_), b3(radius_), b4(linearStep_), b5(pointCount_);
        east_->setValue(snapshot.center_east_m);
        north_->setValue(snapshot.center_north_m);
        radius_->setValue(snapshot.radius_m);
        linearStep_->setValue(snapshot.linear_step);
        pointCount_->setValue(snapshot.circumference_points);
        for (auto* spin : {
                    east_, north_, radius_
                })
            spin->setSingleStep(snapshot.linear_step);
    }

  private:
    PreciseDoubleSpinBox *east_ = nullptr, *north_ = nullptr, *radius_ = nullptr,
                          *linearStep_ = nullptr;
    QSpinBox* pointCount_ = nullptr;
};

QString shapeName(BuildingShapeKind kind) {
    return kind == BuildingShapeKind::Rectangle ? QStringLiteral("Rectangle")
           : QStringLiteral("Circle");
}

}  // namespace

BuildingShapeDialog::BuildingShapeDialog(Callbacks callbacks, QWidget* parent)
    : QDialog(parent), callbacks_(std::move(callbacks)) {
    setWindowTitle(QStringLiteral("Building Shape Designer"));
    setWindowFlag(Qt::Window, true);
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(720, 760);

    sourceNotice_ = new QLabel;
    sourceNotice_->setWordWrap(true);
    sourceNotice_->setStyleSheet(QStringLiteral(
                                     "QLabel { background: palette(alternate-base); border: 1px solid palette(mid); "
                                     "padding: 6px; }"));

    newShapeType_ = new QComboBox;
    newShapeType_->addItem("Rectangle", int(BuildingShapeKind::Rectangle));
    newShapeType_->addItem("Circle", int(BuildingShapeKind::Circle));
    auto* placeShape = new QPushButton("Place Shape");
    auto* copyShape = new QPushButton("Copy Selected");
    auto* deleteShape = new QPushButton("Delete Selected");
    shapeList_ = new QListWidget;
    shapeList_->setToolTip("Select a draft outline to edit its parameters.");

    auto* shapeActions = new QHBoxLayout;
    shapeActions->addWidget(newShapeType_);
    shapeActions->addWidget(placeShape);
    shapeActions->addWidget(copyShape);
    shapeActions->addWidget(deleteShape);

    rectangleParameters_ = new RectangleParameterWidget;
    circleParameters_ = new CircleParameterWidget;
    parameterStack_ = new QStackedWidget;
    parameterStack_->addWidget(rectangleParameters_);
    parameterStack_->addWidget(circleParameters_);

    auto* parameterGroup = new QGroupBox("Selected shape parameters");
    auto* parameterLayout = new QVBoxLayout(parameterGroup);
    parameterLayout->addWidget(parameterStack_);

    auto* horizontalRuler = new QPushButton("Place Horizontal Ruler");
    auto* verticalRuler = new QPushButton("Place Vertical Ruler");
    auto* measure = new QPushButton("Measure Pair");
    auto* deleteRulers = new QPushButton("Delete Selected Rulers");
    auto* rulerActions = new QHBoxLayout;
    rulerActions->addWidget(horizontalRuler);
    rulerActions->addWidget(verticalRuler);
    rulerActions->addWidget(measure);
    rulerActions->addWidget(deleteRulers);

    rulerTable_ = new QTableWidget(0, 5);
    rulerTable_->setHorizontalHeaderLabels(
    {"ID", "Orientation", "Position [m]", "Step [m]", "Visible"});
    rulerTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rulerTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    rulerTable_->horizontalHeader()->setStretchLastSection(true);
    measureResult_ = new QLabel("Distance: select two rulers of the same orientation.");
    measureResult_->setWordWrap(true);
    instruction_ = new QLabel("Place a shape or ruler, or select a draft outline on the ENU map.");
    instruction_->setWordWrap(true);
    auto* applyClose = new QPushButton("Apply and Close");

    auto* root = new QVBoxLayout(this);
    root->addWidget(sourceNotice_);
    root->addLayout(shapeActions);
    root->addWidget(shapeList_);
    root->addWidget(parameterGroup, 1);
    root->addLayout(rulerActions);
    root->addWidget(rulerTable_);
    root->addWidget(measureResult_);
    root->addWidget(instruction_);
    root->addWidget(applyClose);

    connect(newShapeType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (selectedShapeId_ == 0) {
            const auto kind = static_cast<BuildingShapeKind>(newShapeType_->currentData().toInt());
            parameterStack_->setCurrentWidget(parameterWidget(kind));
        }
    });
    connect(placeShape, &QPushButton::clicked, this, &BuildingShapeDialog::beginPlaceShape);
    connect(copyShape, &QPushButton::clicked, this, &BuildingShapeDialog::beginCopyShape);
    connect(deleteShape, &QPushButton::clicked, this, &BuildingShapeDialog::removeSelectedShape);
    connect(shapeList_, &QListWidget::currentItemChanged, this,
    [this](QListWidgetItem* current, QListWidgetItem*) {
        selectShape(current ? current->data(Qt::UserRole).toULongLong() : 0);
    });
    connect(rectangleParameters_, &ShapeParameterWidget::parametersChanged, this,
            &BuildingShapeDialog::onParametersChanged);
    connect(circleParameters_, &ShapeParameterWidget::parametersChanged, this,
            &BuildingShapeDialog::onParametersChanged);
    connect(horizontalRuler, &QPushButton::clicked, this, [this] {
        setPickMode(BuildingDesignPickMode::PlaceHorizontalRuler);
        instruction_->setText("Click the ENU map to place a horizontal ruler.");
    });
    connect(verticalRuler, &QPushButton::clicked, this, [this] {
        setPickMode(BuildingDesignPickMode::PlaceVerticalRuler);
        instruction_->setText("Click the ENU map to place a vertical ruler.");
    });
    connect(measure, &QPushButton::clicked, this, &BuildingShapeDialog::beginMeasurement);
    connect(rulerTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (pickMode_ != BuildingDesignPickMode::MeasureRulers)
            return;
        const auto ids = selectedRulerIds();
        if (ids.size() == 2) {
            updateMeasurement(ids);
            setPickMode(BuildingDesignPickMode::None);
        }
    });
    connect(rulerTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (rebuildingRulers_ || !item || item->column() != 4)
            return;
        const auto* idItem = rulerTable_->item(item->row(), 0);
        if (idItem)
            if (auto* ruler = findRuler(idItem->data(Qt::UserRole).toULongLong())) {
                ruler->visible = item->checkState() == Qt::Checked;
                publishOverlay();
            }
    });
    connect(deleteRulers, &QPushButton::clicked, this, [this] {
        const auto ids = selectedRulerIds();
        rulers_.erase(std::remove_if(rulers_.begin(), rulers_.end(),
        [&ids](const auto& ruler) {
            return std::find(ids.begin(), ids.end(), ruler.id) != ids.end();
        }),
        rulers_.end());
        measurementIds_.clear();
        rebuildRulerTable();
        publishOverlay();
    });
    connect(applyClose, &QPushButton::clicked, this, &QDialog::close);

    parameterStack_->setCurrentWidget(rectangleParameters_);
    publishOverlay();
}

void BuildingShapeDialog::setSourceNotice(const QString& notice) {
    sourceNotice_->setText(notice);
    sourceNotice_->setVisible(!notice.trimmed().isEmpty());
}

ShapeParameterWidget* BuildingShapeDialog::parameterWidget(BuildingShapeKind kind) const {
    return kind == BuildingShapeKind::Rectangle ? rectangleParameters_ : circleParameters_;
}

BuildingShapeDialog::ShapeRecord* BuildingShapeDialog::findShape(std::uint64_t id) {
    const auto found = std::find_if(shapes_.begin(), shapes_.end(), [id](const auto& shape) {
        return shape.id == id;
    });
    return found == shapes_.end() ? nullptr : &*found;
}

BuildingShapeDialog::RulerRecord* BuildingShapeDialog::findRuler(std::uint64_t id) {
    const auto found = std::find_if(rulers_.begin(), rulers_.end(), [id](const auto& ruler) {
        return ruler.id == id;
    });
    return found == rulers_.end() ? nullptr : &*found;
}

void BuildingShapeDialog::setPickMode(BuildingDesignPickMode mode) {
    pickMode_ = mode;
    if (callbacks_.set_pick_mode)
        callbacks_.set_pick_mode(mode);
}

void BuildingShapeDialog::beginPlaceShape() {
    const auto kind = static_cast<BuildingShapeKind>(newShapeType_->currentData().toInt());
    pendingShape_ = parameterWidget(kind)->snapshot();
    setPickMode(BuildingDesignPickMode::PlaceShape);
    instruction_->setText(QString("Click the ENU map to place the %1 center.").arg(shapeName(kind)));
}

void BuildingShapeDialog::beginCopyShape() {
    auto* shape = findShape(selectedShapeId_);
    if (!shape) {
        QMessageBox::information(this, "Select Shape", "Select a draft shape before copying it.");
        return;
    }
    pendingShape_ = shape->snapshot;
    setPickMode(BuildingDesignPickMode::CopyShape);
    instruction_->setText("Click the ENU map to place the copied shape center.");
}

void BuildingShapeDialog::acceptPickedPosition(const QPointF& position) {
    if ((pickMode_ == BuildingDesignPickMode::PlaceShape ||
            pickMode_ == BuildingDesignPickMode::CopyShape) &&
            pendingShape_) {
        ShapeRecord record;
        record.id = nextShapeId_++;
        record.snapshot = shapeMovedTo(*pendingShape_, position);
        shapes_.push_back(std::move(record));
        pendingShape_.reset();
        selectedShapeId_ = shapes_.back().id;
        setPickMode(BuildingDesignPickMode::None);
        rebuildShapeList();
        selectShape(selectedShapeId_);
        instruction_->setText("Shape placed. Adjust parameters or add another shape.");
        return;
    }
    if (pickMode_ == BuildingDesignPickMode::PlaceHorizontalRuler) {
        addRuler(RulerOrientation::Horizontal, position);
        return;
    }
    if (pickMode_ == BuildingDesignPickMode::PlaceVerticalRuler)
        addRuler(RulerOrientation::Vertical, position);
}

void BuildingShapeDialog::selectShape(std::uint64_t id) {
    auto* shape = findShape(id);
    selectedShapeId_ = shape ? id : 0;
    if (shape) {
        auto* widget = parameterWidget(shapeKind(shape->snapshot));
        loadingParameters_ = true;
        parameterStack_->setCurrentWidget(widget);
        widget->restore(shape->snapshot);
        loadingParameters_ = false;
        for (int row = 0; row < shapeList_->count(); ++row) {
            if (shapeList_->item(row)->data(Qt::UserRole).toULongLong() == id) {
                const QSignalBlocker blocker(shapeList_);
                shapeList_->setCurrentRow(row);
                break;
            }
        }
    }
    publishOverlay();
}

void BuildingShapeDialog::removeSelectedShape() {
    if (!selectedShapeId_)
        return;
    shapes_.erase(std::remove_if(shapes_.begin(), shapes_.end(),
    [this](const auto& shape) {
        return shape.id == selectedShapeId_;
    }),
    shapes_.end());
    selectedShapeId_ = 0;
    rebuildShapeList();
    publishOverlay();
}

void BuildingShapeDialog::onParametersChanged() {
    if (loadingParameters_)
        return;
    auto* shape = findShape(selectedShapeId_);
    auto* widget = qobject_cast<ShapeParameterWidget*>(parameterStack_->currentWidget());
    if (shape && widget && shapeKind(shape->snapshot) == widget->shapeKind()) {
        shape->snapshot = widget->snapshot();
        publishOverlay();
    }
}

void BuildingShapeDialog::rebuildShapeList() {
    const QSignalBlocker blocker(shapeList_);
    shapeList_->clear();
    for (std::size_t index = 0; index < shapes_.size(); ++index) {
        const auto& shape = shapes_[index];
        auto* item = new QListWidgetItem(
            QString("%1 %2").arg(shapeName(shapeKind(shape.snapshot))).arg(index + 1));
        item->setData(Qt::UserRole, qulonglong(shape.id));
        shapeList_->addItem(item);
        if (shape.id == selectedShapeId_)
            shapeList_->setCurrentItem(item);
    }
}

void BuildingShapeDialog::addRuler(RulerOrientation orientation, const QPointF& position) {
    RulerRecord ruler;
    ruler.id = nextRulerId_++;
    ruler.orientation = orientation;
    ruler.position_m = orientation == RulerOrientation::Horizontal ? position.y() : position.x();
    rulers_.push_back(ruler);
    setPickMode(BuildingDesignPickMode::None);
    rebuildRulerTable();
    publishOverlay();
    instruction_->setText("Ruler placed. Its position and step can be edited in the table.");
}

void BuildingShapeDialog::rebuildRulerTable() {
    rebuildingRulers_ = true;
    rulerTable_->setRowCount(0);
    for (const auto& ruler : rulers_) {
        const int row = rulerTable_->rowCount();
        rulerTable_->insertRow(row);
        auto* id = new QTableWidgetItem(QString::number(qulonglong(ruler.id)));
        id->setData(Qt::UserRole, qulonglong(ruler.id));
        id->setFlags(id->flags() & ~Qt::ItemIsEditable);
        auto* orientation = new QTableWidgetItem(
            ruler.orientation == RulerOrientation::Horizontal ? "Horizontal" : "Vertical");
        orientation->setFlags(orientation->flags() & ~Qt::ItemIsEditable);
        auto* position = preciseSpin(-kCoordinateLimit, kCoordinateLimit, ruler.position_m, ruler.step,
                                     "Horizontal rulers store North; vertical rulers store East.");
        auto* step = preciseSpin(kMinimumDimension, kCoordinateLimit, ruler.step, 0.01,
                                 "Increment used by this ruler's position spin box.");
        auto* visible = new QTableWidgetItem;
        visible->setFlags((visible->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        visible->setCheckState(ruler.visible ? Qt::Checked : Qt::Unchecked);
        rulerTable_->setItem(row, 0, id);
        rulerTable_->setItem(row, 1, orientation);
        rulerTable_->setCellWidget(row, 2, position);
        rulerTable_->setCellWidget(row, 3, step);
        rulerTable_->setItem(row, 4, visible);
        const std::uint64_t rulerId = ruler.id;
        connect(position, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, rulerId](double value) {
            if (auto* target = findRuler(rulerId)) {
                target->position_m = value;
                publishOverlay();
                if (measurementIds_.size() == 2)
                    updateMeasurement(measurementIds_);
            }
        });
        connect(step, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, rulerId, position](double value) {
            if (auto* target = findRuler(rulerId)) {
                target->step = value;
                position->setSingleStep(value);
            }
        });
    }
    rebuildingRulers_ = false;
}

std::vector<std::uint64_t> BuildingShapeDialog::selectedRulerIds() const {
    std::vector<std::uint64_t> result;
    for (const auto& range : rulerTable_->selectedRanges()) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            const auto* item = rulerTable_->item(row, 0);
            if (item) {
                const auto id = item->data(Qt::UserRole).toULongLong();
                if (std::find(result.begin(), result.end(), id) == result.end())
                    result.push_back(id);
            }
        }
    }
    return result;
}

void BuildingShapeDialog::beginMeasurement() {
    const auto selected = selectedRulerIds();
    if (selected.size() == 2) {
        updateMeasurement(selected);
        return;
    }
    measurementIds_.clear();
    setPickMode(BuildingDesignPickMode::MeasureRulers);
    measureResult_->setText("Click two visible rulers of the same orientation on the ENU map.");
    publishOverlay();
}

void BuildingShapeDialog::selectRuler(std::uint64_t id) {
    if (pickMode_ != BuildingDesignPickMode::MeasureRulers || !findRuler(id))
        return;
    for (int row = 0; row < rulerTable_->rowCount(); ++row) {
        const auto* item = rulerTable_->item(row, 0);
        if (item && item->data(Qt::UserRole).toULongLong() == id) {
            rulerTable_->selectionModel()->select(
                rulerTable_->model()->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
            break;
        }
    }
    if (std::find(measurementIds_.begin(), measurementIds_.end(), id) == measurementIds_.end())
        measurementIds_.push_back(id);
    if (measurementIds_.size() == 2) {
        updateMeasurement(measurementIds_);
        setPickMode(BuildingDesignPickMode::None);
    } else {
        measureResult_->setText("First ruler selected; click the second ruler.");
        publishOverlay();
    }
}

void BuildingShapeDialog::updateMeasurement(const std::vector<std::uint64_t>& ids) {
    if (ids.size() != 2)
        return;
    auto* first = findRuler(ids[0]);
    auto* second = findRuler(ids[1]);
    if (!first || !second || first->orientation != second->orientation) {
        measurementIds_.clear();
        measureResult_->setText("Measurement requires two horizontal or two vertical rulers.");
        publishOverlay();
        return;
    }
    measurementIds_ = ids;
    const double distance = std::abs(first->position_m - second->position_m);
    const QString axis = first->orientation == RulerOrientation::Horizontal ? "North" : "East";
    measureResult_->setText(
        QString("%1 spacing: %2 m")
        .arg(axis, QString::number(distance, 'g', std::numeric_limits<double>::max_digits10)));
    publishOverlay();
}

void BuildingShapeDialog::publishOverlay() {
    if (!callbacks_.update_overlay)
        return;
    BuildingDesignOverlay overlay;
    for (const auto& record : shapes_) {
        auto shape = createBuildingShape(record.snapshot);
        overlay.shapes.push_back(
        {record.id, shape->previewOutline(), shape->discretize(), record.id == selectedShapeId_});
    }
    for (const auto& ruler : rulers_) {
        overlay.rulers.push_back({ruler.id, ruler.orientation, ruler.position_m, ruler.visible,
                                  std::find(measurementIds_.begin(), measurementIds_.end(), ruler.id) !=
                                  measurementIds_.end()});
    }
    callbacks_.update_overlay(overlay);
}

void BuildingShapeDialog::closeEvent(QCloseEvent* event) {
    std::vector<BuildingShapeSnapshot> snapshots;
    snapshots.reserve(shapes_.size());
    for (const auto& shape : shapes_)
        snapshots.push_back(shape.snapshot);
    QString error;
    if (callbacks_.commit && !callbacks_.commit(snapshots, error)) {
        QMessageBox::warning(this, "Cannot Apply Building Shapes", error);
        event->ignore();
        return;
    }
    setPickMode(BuildingDesignPickMode::None);
    if (callbacks_.clear_overlay)
        callbacks_.clear_overlay();
    event->accept();
}

}  // namespace b210post
