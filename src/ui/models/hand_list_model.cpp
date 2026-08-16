#include "hand_list_model.h"
#include "../../core/text/card_text_formatter.h"
#include <algorithm>
namespace fpdz {
HandListModel::HandListModel(QObject* parent) : QAbstractListModel(parent) {}
int HandListModel::rowCount(const QModelIndex&) const { return static_cast<int>(m_cards.size()); }
QVariant HandListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_cards.size())) return {};
    const auto& card = m_cards[index.row()];
    if (role == Qt::DisplayRole || role == CardTextRole) {
        return QString::fromStdWString(
            CardTextFormatter::formatSameRankSpeech(card.rank(), countOfRank(card.rank())));
    }
    if (role == Qt::AccessibleTextRole) {
        if (m_accessibilityTextSuppressed) return {};
        const bool speakSingleCard = index.row() < static_cast<int>(m_singleSelectionSpeech.size()) &&
            m_selected[index.row()] && m_singleSelectionSpeech[index.row()];
        return QString::fromStdWString(speakSingleCard
            ? CardTextFormatter::formatRankSpeech(card.rank())
            : CardTextFormatter::formatSameRankSpeech(
                  card.rank(), m_selected[index.row()] ? countOfRank(card.rank())
                                                       : unselectedCountOfRank(card.rank())));
    }
    if (role == SelectedRole) {
        return m_selected[index.row()];
    }
    if (role == PositionRole) {
        return index.row() + 1;
    }
    return {};
}
Qt::ItemFlags HandListModel::flags(const QModelIndex& index) const {
    return QAbstractListModel::flags(index) | Qt::ItemIsSelectable;
}
void HandListModel::setAccessibilityTextSuppressed(bool suppressed) {
    m_accessibilityTextSuppressed = suppressed;
}
bool HandListModel::setCards(const std::vector<Card>& cards) {
    auto sortedCards = cards;
    std::stable_sort(sortedCards.begin(), sortedCards.end(),
        [](const Card& left, const Card& right) {
            if (left.weight() != right.weight()) return left.weight() < right.weight();
            if (left.suit() != right.suit()) {
                return static_cast<int>(left.suit()) < static_cast<int>(right.suit());
            }
            return left.deckIndex() < right.deckIndex();
        });

    const bool unchanged = sortedCards.size() == m_cards.size() &&
        std::equal(sortedCards.begin(), sortedCards.end(), m_cards.begin(),
            [](const Card& left, const Card& right) { return left.id() == right.id(); });
    if (unchanged) return false;

    beginResetModel();
    m_cards = std::move(sortedCards);
    m_selected.assign(m_cards.size(), false);
    m_singleSelectionSpeech.assign(m_cards.size(), false);
    m_pickOrder.clear();
    endResetModel();
    return true;
}
Card HandListModel::cardAt(int row) const {
    if (row >= 0 && row < static_cast<int>(m_cards.size())) return m_cards[row];
    return Card{};
}
void HandListModel::setSelected(int row, bool selected) {
    if (row >= 0 && row < static_cast<int>(m_selected.size())) {
        if (m_selected[row] == selected) {
            if (selected && m_singleSelectionSpeech[row]) {
                m_singleSelectionSpeech[row] = false;
                emit dataChanged(index(row), index(row), {SelectedRole});
            }
            return;
        }
        m_selected[row] = selected;
        m_singleSelectionSpeech[row] = false;
        const CardId id = m_cards[row].id();
        if (selected) {
            m_pickOrder.push_back(id);
        } else {
            m_pickOrder.erase(std::remove(m_pickOrder.begin(), m_pickOrder.end(), id),
                              m_pickOrder.end());
        }
        emit dataChanged(index(row), index(row), {SelectedRole});
    }
}
bool HandListModel::selectSingle(int row) {
    if (row < 0 || row >= static_cast<int>(m_selected.size()) || m_selected[row]) return false;
    m_selected[row] = true;
    m_singleSelectionSpeech[row] = true;
    m_pickOrder.push_back(m_cards[row].id());
    emit dataChanged(index(row), index(row), {SelectedRole});
    return true;
}
bool HandListModel::isSelected(int row) const {
    if (row >= 0 && row < static_cast<int>(m_selected.size())) return m_selected[row];
    return false;
}
void HandListModel::clearSelection() {
    if (m_selected.empty()) return;
    std::fill(m_selected.begin(), m_selected.end(), false);
    std::fill(m_singleSelectionSpeech.begin(), m_singleSelectionSpeech.end(), false);
    m_pickOrder.clear();
    emit dataChanged(index(0), index(static_cast<int>(m_selected.size()) - 1),
                     {SelectedRole});
}
int HandListModel::selectedCount() const {
    return static_cast<int>(std::count(m_selected.begin(), m_selected.end(), true));
}
std::vector<Card> HandListModel::selectedCards() const {
    std::vector<Card> cards;
    for (int row = 0; row < static_cast<int>(m_cards.size()); ++row) {
        if (m_selected[row]) cards.push_back(m_cards[row]);
    }
    return cards;
}
std::vector<CardId> HandListModel::selectedCardIds() const {
    std::vector<CardId> ids;
    for (const auto& card : selectedCards()) ids.push_back(card.id());
    return ids;
}
std::optional<Card> HandListModel::deselectNextPickedCard() {
    while (!m_pickOrder.empty()) {
        const CardId id = m_pickOrder.front();
        m_pickOrder.erase(m_pickOrder.begin());
        for (int row = 0; row < static_cast<int>(m_cards.size()); ++row) {
            if (m_cards[row].id() == id && m_selected[row]) {
                m_selected[row] = false;
                m_singleSelectionSpeech[row] = false;
                emit dataChanged(index(row), index(row), {SelectedRole});
                return m_cards[row];
            }
        }
    }
    return std::nullopt;
}
int HandListModel::countOfRank(Rank rank) const {
    return static_cast<int>(std::count_if(m_cards.begin(), m_cards.end(),
        [rank](const Card& card) { return card.rank() == rank; }));
}
int HandListModel::unselectedCountOfRank(Rank rank) const {
    int count = 0;
    for (int row = 0; row < static_cast<int>(m_cards.size()); ++row) {
        if (m_cards[row].rank() == rank && !m_selected[row]) ++count;
    }
    return count;
}
int HandListModel::nextUnselectedRow(int row) const {
    const int firstRow = std::max(0, row + 1);
    for (int current = firstRow; current < static_cast<int>(m_cards.size()); ++current) {
        if (!m_selected[current]) return current;
    }
    return -1;
}
int HandListModel::previousUnselectedRow(int row) const {
    for (int current = std::min(row - 1, static_cast<int>(m_cards.size()) - 1);
         current >= 0; --current) {
        if (!m_selected[current]) return current;
    }
    return -1;
}
int HandListModel::firstUnselectedRow() const {
    for (int row = 0; row < static_cast<int>(m_cards.size()); ++row) {
        if (!m_selected[row]) return row;
    }
    return -1;
}
int HandListModel::groupStartRow(int row) const {
    if (row < 0 || row >= static_cast<int>(m_cards.size())) return -1;
    const Rank rank = m_cards[row].rank();
    const auto it = std::find_if(m_cards.begin(), m_cards.end(),
        [rank](const Card& card) { return card.rank() == rank; });
    return it == m_cards.end() ? -1 : static_cast<int>(std::distance(m_cards.begin(), it));
}
int HandListModel::previousGroupStartRow(int row) const {
    if (row < 0 || row >= static_cast<int>(m_cards.size())) return -1;
    const Rank currentRank = m_cards[row].rank();
    int previous = groupStartRow(row);
    for (int index = 0; index < static_cast<int>(m_cards.size()); ++index) {
        if (m_cards[index].rank() < currentRank) previous = groupStartRow(index);
    }
    return previous;
}
int HandListModel::nextGroupStartRow(int row) const {
    if (row < 0 || row >= static_cast<int>(m_cards.size())) return -1;
    const Rank currentRank = m_cards[row].rank();
    for (int index = 0; index < static_cast<int>(m_cards.size()); ++index) {
        if (m_cards[index].rank() > currentRank) return groupStartRow(index);
    }
    return groupStartRow(row);
}
int HandListModel::lastGroupStartRow() const {
    if (m_cards.empty()) return -1;
    const auto it = std::max_element(m_cards.begin(), m_cards.end(),
        [](const Card& left, const Card& right) { return left.rank() < right.rank(); });
    return groupStartRow(static_cast<int>(std::distance(m_cards.begin(), it)));
}
int HandListModel::previousBrowsableGroupStartRow(int row) const {
    if (m_cards.empty()) return -1;
    const Rank currentRank = m_cards[std::clamp(row, 0, static_cast<int>(m_cards.size()) - 1)].rank();
    int result = -1;
    for (int current = 0; current < static_cast<int>(m_cards.size()); ++current) {
        if (!m_selected[current] && m_cards[current].rank() < currentRank) {
            if (result < 0 || m_cards[current].rank() > m_cards[result].rank()) result = current;
        }
    }
    if (result >= 0) return result;
    for (int current = 0; current < static_cast<int>(m_cards.size()); ++current) {
        if (!m_selected[current] && m_cards[current].rank() == currentRank) return current;
    }
    return firstUnselectedRow();
}
int HandListModel::nextBrowsableGroupStartRow(int row) const {
    if (m_cards.empty()) return -1;
    const Rank currentRank = m_cards[std::clamp(row, 0, static_cast<int>(m_cards.size()) - 1)].rank();
    for (int current = 0; current < static_cast<int>(m_cards.size()); ++current) {
        if (!m_selected[current] && m_cards[current].rank() > currentRank) return current;
    }
    for (int current = 0; current < static_cast<int>(m_cards.size()); ++current) {
        if (!m_selected[current] && m_cards[current].rank() == currentRank) return current;
    }
    return lastBrowsableGroupStartRow();
}
int HandListModel::lastBrowsableGroupStartRow() const {
    int result = -1;
    for (int current = 0; current < static_cast<int>(m_cards.size()); ++current) {
        if (!m_selected[current] &&
            (result < 0 || m_cards[current].rank() > m_cards[result].rank())) {
            result = current;
        }
    }
    return result;
}
void HandListModel::setGroupSelected(int row, bool selected) {
    const int start = groupStartRow(row);
    if (start < 0) return;
    const Rank rank = m_cards[start].rank();
    for (int current = 0; current < static_cast<int>(m_cards.size()); ++current) {
        if (m_cards[current].rank() == rank) setSelected(current, selected);
    }
}

HandListModel::GroupSelectionResult HandListModel::selectGroup(int row) {
    GroupSelectionResult result;
    const int start = groupStartRow(row);
    if (start < 0) return result;

    result.rank = m_cards[start].rank();
    result.valid = true;
    for (int current = 0; current < static_cast<int>(m_cards.size()); ++current) {
        if (m_cards[current].rank() != result.rank) continue;
        ++result.groupCount;
        if (!m_selected[current]) {
            setSelected(current, true);
            ++result.newlySelectedCount;
        }
    }
    return result;
}
} // namespace fpdz
