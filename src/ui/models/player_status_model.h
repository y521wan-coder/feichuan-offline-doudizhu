#pragma once
#include <QAbstractListModel>
#include "../../core/model/player.h"
#include <array>
namespace fpdz {
class PlayerStatusModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit PlayerStatusModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    void updateFromSnapshot(const std::array<PlayerPublicState, PLAYER_COUNT>& players);
private:
    std::array<PlayerPublicState, PLAYER_COUNT> m_players;
};
} // namespace fpdz
