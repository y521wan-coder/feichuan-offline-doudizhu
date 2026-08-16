#include "player_status_model.h"
namespace fpdz {
PlayerStatusModel::PlayerStatusModel(QObject* parent) : QAbstractListModel(parent) {}
int PlayerStatusModel::rowCount(const QModelIndex&) const { return PLAYER_COUNT; }
QVariant PlayerStatusModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= PLAYER_COUNT) return {};
    if (role == Qt::DisplayRole) {
        const auto& p = m_players[index.row()];
        std::wstring text = p.name;
        if (p.roleRevealed) text += L"(" + roleDisplayName(p.role) + L")";
        text += L" - " + std::to_wstring(p.remainingCards) + L"张";
        return QString::fromStdWString(text);
    }
    return {};
}
void PlayerStatusModel::updateFromSnapshot(const std::array<PlayerPublicState, PLAYER_COUNT>& players) {
    beginResetModel();
    m_players = players;
    endResetModel();
}
} // namespace fpdz
