#include "hand_list_model.h"
#include "../../core/text/card_text_formatter.h"
#include "../../core/rules/pattern_analyzer.h"
#include <QSignalBlocker>
#include <algorithm>
#include <array>
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

std::optional<CardPattern> HandListModel::completeEndpointSelection(
        int activePlayerCount, bool allowStraight) {
    if (activePlayerCount != TWO_PLAYER_COUNT &&
        activePlayerCount != THREE_PLAYER_COUNT &&
        activePlayerCount != PLAYER_COUNT) {
        return std::nullopt;
    }
    if (m_cards.size() != m_selected.size() ||
        m_cards.size() != m_singleSelectionSpeech.size()) {
        return std::nullopt;
    }

    // 1) 只在局部副本上读取当前选择，不改变模型。
    const std::vector<Card> originalSelected = selectedCards();
    std::array<int, RANK_COUNT> selectedPerRank{};
    std::vector<CardId> selectedIds;
    selectedIds.reserve(originalSelected.size());
    for (const auto& card : originalSelected) {
        if (!card.isValid()) return std::nullopt;
        ++selectedPerRank[static_cast<size_t>(card.rank())];
        selectedIds.push_back(card.id());
    }
    std::sort(selectedIds.begin(), selectedIds.end());
    if (std::adjacent_find(selectedIds.begin(), selectedIds.end()) != selectedIds.end()) {
        return std::nullopt;
    }

    std::vector<Rank> endpointRanks;
    for (int rankIndex = 0; rankIndex < RANK_COUNT; ++rankIndex) {
        if (selectedPerRank[static_cast<size_t>(rankIndex)] > 0) {
            endpointRanks.push_back(static_cast<Rank>(rankIndex));
        }
    }
    if (endpointRanks.size() != 2) return std::nullopt;

    const Rank lowRank = endpointRanks[0];
    const Rank highRank = endpointRanks[1];
    int copiesPerRank = 0;
    CardPatternType expectedType = CardPatternType::Invalid;
    int minimumLength = 0;
    if (allowStraight && originalSelected.size() == 2 &&
        selectedPerRank[static_cast<size_t>(lowRank)] == 1 &&
        selectedPerRank[static_cast<size_t>(highRank)] == 1) {
        copiesPerRank = 1;
        expectedType = CardPatternType::Straight;
        minimumLength = 5;
    } else if (originalSelected.size() == 4 &&
               selectedPerRank[static_cast<size_t>(lowRank)] == 2 &&
               selectedPerRank[static_cast<size_t>(highRank)] == 2) {
        copiesPerRank = 2;
        expectedType = CardPatternType::ConsecutivePairs;
        minimumLength = 3;
    } else {
        return std::nullopt;
    }

    // 2) 两端只能在 3 到 A 之间，闭区间长度固定。
    if (!canBeInSequence(lowRank) || !canBeInSequence(highRank)) return std::nullopt;
    const int length = rankWeight(highRank) - rankWeight(lowRank) + 1;
    if (length < minimumLength || length > 12) return std::nullopt;

    // 3) 手牌实体必须全部有效且不重复。
    std::vector<CardId> handIds;
    handIds.reserve(m_cards.size());
    for (const auto& card : m_cards) {
        if (!card.isValid()) return std::nullopt;
        handIds.push_back(card.id());
    }
    std::sort(handIds.begin(), handIds.end());
    if (std::adjacent_find(handIds.begin(), handIds.end()) != handIds.end()) {
        return std::nullopt;
    }

    // 4) 先在局部候选上补齐每个中间点数，全部成功后才提交。
    std::vector<Card> candidateCards = originalSelected;
    std::vector<int> addedRows;
    std::vector<bool> taken(m_cards.size(), false);
    for (int rankIndex = rankWeight(lowRank); rankIndex <= rankWeight(highRank); ++rankIndex) {
        const Rank rank = static_cast<Rank>(rankIndex);
        int need = copiesPerRank - selectedPerRank[static_cast<size_t>(rank)];
        if (need < 0) return std::nullopt;
        for (int row = 0; row < static_cast<int>(m_cards.size()) && need > 0; ++row) {
            if (m_cards[row].rank() != rank || m_selected[row] || taken[row]) continue;
            taken[row] = true;
            candidateCards.push_back(m_cards[row]);
            addedRows.push_back(row);
            --need;
        }
        if (need > 0) return std::nullopt;
    }

    if (static_cast<int>(candidateCards.size()) != length * copiesPerRank) {
        return std::nullopt;
    }
    std::vector<CardId> candidateIds;
    candidateIds.reserve(candidateCards.size());
    for (const auto& card : candidateCards) candidateIds.push_back(card.id());
    std::sort(candidateIds.begin(), candidateIds.end());
    if (std::adjacent_find(candidateIds.begin(), candidateIds.end()) != candidateIds.end()) {
        return std::nullopt;
    }

    const CardPattern pattern = PatternAnalyzer::analyze(candidateCards, activePlayerCount);
    if (!pattern.isValid() || pattern.type != expectedType ||
        pattern.mainRank != lowRank || pattern.mainLength != length ||
        pattern.totalCards != static_cast<int>(candidateCards.size())) {
        return std::nullopt;
    }

    if (addedRows.empty()) return std::nullopt;
    std::sort(addedRows.begin(), addedRows.end());
    if (std::adjacent_find(addedRows.begin(), addedRows.end()) != addedRows.end()) {
        return std::nullopt;
    }

    // 5) 全部校验通过后才借用原有 setSelected 提交，保留原端点实体与拿牌顺序。
    {
        const QSignalBlocker blocker(this);
        for (int row : addedRows) setSelected(row, true);
    }
    emit dataChanged(index(addedRows.front(), 0), index(addedRows.back(), 0), {SelectedRole});
    return pattern;
}
} // namespace fpdz
