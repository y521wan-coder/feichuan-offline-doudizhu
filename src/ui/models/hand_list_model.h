#pragma once
#include <QAbstractListModel>
#include "../../core/model/card.h"
#include <optional>
#include <vector>
namespace fpdz {
class HandListModel : public QAbstractListModel {
    Q_OBJECT
public:
    struct GroupSelectionResult {
        Rank rank = Rank::Three;
        int groupCount = 0;
        int newlySelectedCount = 0;
        bool valid = false;
    };

    enum Roles { CardTextRole = Qt::UserRole + 1, SelectedRole, PositionRole };
    explicit HandListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void setAccessibilityTextSuppressed(bool suppressed);
    bool setCards(const std::vector<Card>& cards);
    Card cardAt(int row) const;
    void setSelected(int row, bool selected);
    bool selectSingle(int row);
    bool isSelected(int row) const;
    void clearSelection();
    int selectedCount() const;
    std::vector<Card> selectedCards() const;
    std::vector<CardId> selectedCardIds() const;
    std::optional<Card> deselectNextPickedCard();
    int countOfRank(Rank rank) const;
    int unselectedCountOfRank(Rank rank) const;
    int nextUnselectedRow(int row) const;
    int previousUnselectedRow(int row) const;
    int firstUnselectedRow() const;
    int groupStartRow(int row) const;
    int previousGroupStartRow(int row) const;
    int nextGroupStartRow(int row) const;
    int lastGroupStartRow() const;
    int previousBrowsableGroupStartRow(int row) const;
    int nextBrowsableGroupStartRow(int row) const;
    int lastBrowsableGroupStartRow() const;
    void setGroupSelected(int row, bool selected);
    GroupSelectionResult selectGroup(int row);
private:
    std::vector<Card> m_cards;
    std::vector<bool> m_selected;
    std::vector<bool> m_singleSelectionSpeech;
    std::vector<CardId> m_pickOrder;
    bool m_accessibilityTextSuppressed = false;
};
} // namespace fpdz
