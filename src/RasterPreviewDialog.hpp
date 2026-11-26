#ifndef __B210_RASTER_PREVIEW_DIALOG_HPP
#define __B210_RASTER_PREVIEW_DIALOG_HPP

#include <QDialog>
#include <QJsonObject>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

#include "RasterCompletion.hpp"
#include "RasterInpainting.hpp"
#include "RasterRenderer.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTimer;

namespace b210post {

class RasterImageView;

class RasterPreviewDialog : public QDialog {
  public:
    RasterPreviewDialog(RenderBundle bundle, int imageType, QString imageKind, QJsonObject metadata,
                        QWidget* parent = nullptr);
    ~RasterPreviewDialog() override;

  private:
    struct RasterOverride {
        double power_dbfs = 0.0;
        RasterCellState state = RasterCellState::ManualRasterValue;
        double confidence = 1.0;
    };

    struct ManualChange {
        std::size_t index = 0;
        bool before_present = false;
        RasterOverride before_value;
        bool after_present = false;
        RasterOverride after_value;
    };

    void initializeRadioSession();
    void applyCompletion();
    void rebuildRadioState();
    void updatePreview();
    void updateMissingOverlay();
    void setBlinkMissing(bool enabled);
    void inspectPixel(int x, int y);
    void editPixel(int x, int y, Qt::MouseButton button);
    void suggestPower();
    void assistedFill();
    void applyHistoryCommand(const std::vector<ManualChange>& command, bool forward);
    void undo();
    void redo();
    void save();
    QImage radioMapImage(RasterColorMode colorMode) const;
    QImage currentExportImage() const;
    QImage currentPresentationImage() const;
    QJsonObject currentMetadata() const;

    RenderBundle bundle_;
    int imageType_ = 0;
    QString imageKind_;
    QJsonObject metadata_;
    RasterCompletionSettings completionSettings_;
    RasterCompletionResult completion_;
    RasterInpaintingSettings inpaintingSettings_;
    RasterInpaintingResult lastInpaintingResult_;
    std::vector<double> displayedPower_;
    std::vector<RasterCellState> displayedState_;
    std::vector<std::uint8_t> originalMissingMask_;
    std::map<std::size_t, RasterOverride> rasterOverrides_;
    std::vector<std::vector<ManualChange>> undoStack_;
    std::vector<std::vector<ManualChange>> redoStack_;
    std::size_t lastRasterIndex_ = std::numeric_limits<std::size_t>::max();
    int assistedApplyCount_ = 0;
    bool blinkPhase_ = true;

    RasterImageView* view_ = nullptr;
    QCheckBox* fillMissing_ = nullptr;
    QCheckBox* showMissing_ = nullptr;
    QCheckBox* currentMissingOnly_ = nullptr;
    QPushButton* blinkMissingButton_ = nullptr;
    QCheckBox* editPixels_ = nullptr;
    QCheckBox* unlockMeasured_ = nullptr;
    QCheckBox* exportAlternateColorMode_ = nullptr;
    QDoubleSpinBox* editPower_ = nullptr;
    QComboBox* brushSize_ = nullptr;
    QPushButton* undoButton_ = nullptr;
    QPushButton* redoButton_ = nullptr;
    QTimer* blinkTimer_ = nullptr;
    QLabel* cellInfo_ = nullptr;
    QLabel* summary_ = nullptr;
};

}  // namespace b210post

#endif // __B210_RASTER_PREVIEW_DIALOG_HPP
