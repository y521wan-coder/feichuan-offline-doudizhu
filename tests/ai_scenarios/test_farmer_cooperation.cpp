#include <QtTest>

#include "ai/standard_ai.h"

using namespace fpdz;

namespace {

std::vector<Card> cards(Rank rank, int count) {
    std::vector<Card> result;
    const Suit suits[] = {Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds};
    for (int deck = 0; deck < 2 && static_cast<int>(result.size()) < count; ++deck) {
        for (const auto suit : suits) {
            if (static_cast<int>(result.size()) == count) break;
            result.push_back(Card::create(rank, suit, static_cast<DeckIndex>(deck)));
        }
    }
    return result;
}

Hand handOf(std::initializer_list<std::pair<Rank, int>> ranks) {
    Hand hand;
    for (const auto& [rank, count] : ranks) hand.addCards(cards(rank, count));
    hand.sortByRank();
    return hand;
}

AiObservation farmerObservation(const Hand& hand, const std::vector<Card>& lastCards,
                                PlayerId lastBy = PlayerId::Player3,
                                int landlordCards = 8, int teammateCards = 8) {
    AiObservation observation;
    observation.playerId = PlayerId::Player2;
    observation.phase = GamePhase::Playing;
    observation.ownHand = hand;
    observation.decisionSeed = 12345;
    for (int index = 0; index < PLAYER_COUNT; ++index) {
        observation.publicState.players[index].id = static_cast<PlayerId>(index);
        observation.publicState.players[index].remainingCards = 8;
        observation.publicState.players[index].role = Role::Farmer;
        observation.publicState.players[index].roleRevealed = true;
    }
    observation.publicState.players[static_cast<int>(PlayerId::Player4)].role = Role::Landlord;
    observation.publicState.players[static_cast<int>(PlayerId::Player4)].remainingCards =
        landlordCards;
    observation.publicState.players[static_cast<int>(PlayerId::Player3)].remainingCards =
        teammateCards;
    observation.publicState.lastPlayedCards = lastCards;
    observation.publicState.lastPlayedBy = lastBy;
    observation.publicState.lastPlayedValid = !lastCards.empty();
    return observation;
}

Rank playedRank(const GameCommand& command) {
    return Card::create(command.cardIds.front()).rank();
}

} // namespace

class TestFarmerCooperation : public QObject {
    Q_OBJECT
private slots:
    void yieldsToTeammateSingleWithoutThreat() {
        const auto observation = farmerObservation(handOf({{Rank::Four, 1}, {Rank::Nine, 1}}),
                                                   cards(Rank::Three, 1));
        StandardAiPlayer ai(AiDifficulty::Beginner);
        const auto command = ai.decidePlay(observation);
        QCOMPARE(command.type, GameCommandType::Pass);
        QCOMPARE(command.aiDecisionReason, std::string("yield_to_farmer_teammate"));
    }

    void yieldsToTeammatePairEvenWhenOnlyHighPairCanBeat() {
        const auto observation = farmerObservation(handOf({{Rank::Two, 2}, {Rank::Three, 1}}),
                                                   cards(Rank::Ace, 2));
        StandardAiPlayer ai(AiDifficulty::Advanced);
        QCOMPARE(ai.decidePlay(observation).type, GameCommandType::Pass);
    }

    void neverBombsTeammateWithoutException() {
        const auto observation = farmerObservation(
            handOf({{Rank::Two, 6}, {Rank::Three, 1}}), cards(Rank::Ace, 5));
        StandardAiPlayer ai(AiDifficulty::Advanced);
        QCOMPARE(ai.decidePlay(observation).type, GameCommandType::Pass);
    }

    void immediateFinishMayOvertakeTeammate() {
        const auto observation = farmerObservation(handOf({{Rank::Four, 1}}),
                                                   cards(Rank::Three, 1));
        StandardAiPlayer ai(AiDifficulty::Beginner);
        const auto command = ai.decidePlay(observation);
        QCOMPARE(command.type, GameCommandType::PlayCards);
        QVERIFY(command.aiTeamRuleException);
        QCOMPARE(command.aiDecisionReason, std::string("farmer_immediate_team_win"));
    }

    void blocksVisibleOneCardLandlordThreat() {
        const auto observation = farmerObservation(
            handOf({{Rank::Five, 1}, {Rank::Nine, 1}}), cards(Rank::Four, 1),
            PlayerId::Player3, 1, 5);
        StandardAiPlayer ai(AiDifficulty::Intermediate);
        const auto command = ai.decidePlay(observation);
        QCOMPARE(command.type, GameCommandType::PlayCards);
        QVERIFY(command.aiTeamRuleException);
        QCOMPARE(playedRank(command), Rank::Five);
    }

    void usesMinimumCostResponseAgainstLandlord() {
        const auto observation = farmerObservation(
            handOf({{Rank::Five, 1}, {Rank::Two, 1}, {Rank::Nine, 1}}),
            cards(Rank::Four, 1), PlayerId::Player4);
        StandardAiPlayer ai(AiDifficulty::Advanced);
        const auto command = ai.decidePlay(observation);
        QCOMPARE(command.type, GameCommandType::PlayCards);
        QCOMPARE(playedRank(command), Rank::Five);
    }

    void leadsLowSingleForOneCardTeammate() {
        const auto observation = farmerObservation(
            handOf({{Rank::Three, 1}, {Rank::Four, 2}, {Rank::Two, 1}}), {},
            PlayerId::Player3, 8, 1);
        StandardAiPlayer ai(AiDifficulty::Advanced);
        const auto command = ai.decidePlay(observation);
        QCOMPARE(command.type, GameCommandType::PlayCards);
        QCOMPARE(playedRank(command), Rank::Three);
        QCOMPARE(command.aiDecisionReason,
                 std::string("feed_low_single_to_one_card_teammate"));
    }

    void invisibleHandsCannotChangeDecision() {
        const auto observation = farmerObservation(
            handOf({{Rank::Five, 1}, {Rank::Nine, 1}}), cards(Rank::Four, 1),
            PlayerId::Player4);
        StandardAiPlayer ai(AiDifficulty::Advanced);
        const auto first = ai.decidePlay(observation);
        const auto second = ai.decidePlay(observation);
        QCOMPARE(first.type, second.type);
        QCOMPARE(first.cardIds, second.cardIds);
        QCOMPARE(first.aiDecisionReason, second.aiDecisionReason);
    }
};

QTEST_MAIN(TestFarmerCooperation)
#include "test_farmer_cooperation.moc"
