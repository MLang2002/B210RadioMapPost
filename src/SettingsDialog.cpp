#include "SettingsDialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace b210post {
namespace {
QDoubleSpinBox* spin(double lo, double hi, int decimals, const QString& tip) {
  auto* s = new QDoubleSpinBox;
  s->setRange(lo, hi);
  s->setDecimals(decimals);
  s->setToolTip(tip);
  return s;
}
}  // namespace

SettingsDialog::SettingsDialog(const AppSettings& s, QWidget* p) : QDialog(p), base_(s) {
  setWindowTitle("Settings");
  setMinimumWidth(520);
  auto* root = new QVBoxLayout(this);
  auto* origin = new QGroupBox("Fallback Geographic Anchor");
  auto* of = new QFormLayout(origin);
  lat_ = spin(-90, 90, 10,
              "Fallback latitude for new/private/recovery data. Standard Type-A plotting never "
              "uses this value.");
  lon_ = spin(-180, 180, 10,
              "Fallback longitude for new/private/recovery data. Standard Type-A plotting never "
              "uses this value.");
  alt_ = spin(-1000, 20000, 6,
              "Fallback altitude for new/private/recovery data. Standard Type-A plotting never "
              "uses this value.");
  of->addRow("Reference latitude [deg]", lat_);
  of->addRow("Reference longitude [deg]", lon_);
  of->addRow("Reference altitude [m]", alt_);
  root->addWidget(origin);
  auto* display = new QGroupBox("Canvas Display");
  auto* df = new QFormLayout(display);
  pointSize_ = new QSpinBox;
  pointSize_->setRange(2, 30);
  pointSize_->setToolTip("Diameter scale used for power and building points on the ENU canvas.");
  opacity_ = spin(0.02, 1, 2, "Opacity of the non-editable layer in the current edit mode.");
  df->addRow("Point size [px]", pointSize_);
  df->addRow("Inactive opacity", opacity_);
  root->addWidget(display);
  lat_->setValue(s.fallback_latitude_deg);
  lon_->setValue(s.fallback_longitude_deg);
  alt_->setValue(s.fallback_altitude_m);
  pointSize_->setValue(s.point_size_px);
  opacity_->setValue(s.inactive_opacity);
  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  bb->button(QDialogButtonBox::Ok)->setToolTip("Apply coordinate and canvas settings.");
  bb->button(QDialogButtonBox::Cancel)->setToolTip("Close without changing settings.");
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(bb);
}

AppSettings SettingsDialog::settings() const {
  auto s = base_;
  s.fallback_latitude_deg = lat_->value();
  s.fallback_longitude_deg = lon_->value();
  s.fallback_altitude_m = alt_->value();
  s.point_size_px = pointSize_->value();
  s.inactive_opacity = opacity_->value();
  return s;
}

}  // namespace b210post
