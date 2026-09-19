#pragma once

#include "../../core/model/game_snapshot.h"

#include <QWidget>
#include <vector>

namespace fpdz {

class CardTableWidget final : public QWidget {
    Q_OBJECT

public:
    explicit CardTableWidget(QWidget* parent = nullptr);

    void setTableState(const PublicGameSnapshot& snapshot,
                       const std::vector<Card>& humanCards,
                       const std::vector<CardId>& selectedCardIds);

    int displayedHumanCardCount() const;
    int displayedBottomCardCount() const;
    int displayedLastPlayedCardCount() const;
    int displayedSelectedCardCount() const;
    int displayedOpponentBackCount(PlayerId playerId) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    PublicGameSnapshot m_snapshot;
    std::vector<Card> m_humanCards;
    std::vector<CardId> m_selectedCardIds;
};

} // namespace fpdz
