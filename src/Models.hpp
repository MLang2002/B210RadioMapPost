#ifndef __B210_MODELS_HPP
#define __B210_MODELS_HPP
#include <QAbstractTableModel>

#include "Types.hpp"

namespace b210post {

class RadioTableModel : public QAbstractTableModel {
    Q_OBJECT
  public:
    explicit RadioTableModel(QObject* p = nullptr);
    void setDataset(RadioDataset* d);
    int rowCount(const QModelIndex& p = {}) const override;
    int columnCount(const QModelIndex& p = {}) const override;
    QVariant data(const QModelIndex& i, int role = Qt::DisplayRole) const override;
    QVariant headerData(int, Qt::Orientation, int) const override;
    Qt::ItemFlags flags(const QModelIndex& i) const override;
    bool setData(const QModelIndex& i, const QVariant& v, int role) override;
    RadioPoint* pointAt(int row);
    int rowForId(std::uint64_t id) const;
    void refresh();
  signals:
    void changed();
    void visibilityChanged();

  private:
    RadioDataset* d_ = nullptr;
};

class BuildingTableModel : public QAbstractTableModel {
    Q_OBJECT
  public:
    explicit BuildingTableModel(QObject* p = nullptr);
    void setDataSource(BuildingDataset* d, const EnuReference* projectRef);
    int rowCount(const QModelIndex& p = {}) const override;
    int columnCount(const QModelIndex& p = {}) const override;
    QVariant data(const QModelIndex& i, int role = Qt::DisplayRole) const override;
    QVariant headerData(int, Qt::Orientation, int) const override;
    Qt::ItemFlags flags(const QModelIndex& i) const override;
    bool setData(const QModelIndex& i, const QVariant& v, int role) override;
    SurveyPoint* pointAt(int row);
    int rowForId(std::uint64_t id) const;
    void refresh();
  signals:
    void changed();
    void visibilityChanged();

  private:
    BuildingDataset* d_ = nullptr;
    const EnuReference* ref_ = nullptr;
};
}  // namespace b210post

#endif // __B210_MODELS_HPP
