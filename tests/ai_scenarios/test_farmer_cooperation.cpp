#include <QtTest>

#include <algorithm>
#include <array>

#include "ai/standard_ai.h"
#include "core/model/deck.h"

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

int playedCount(const GameCommand& command, Rank rank) {
    return static_cast<int>(std::count_if(
        command.cardIds.begin(), command.cardIds.end(), [rank](CardId id) {
            return Card::create(id).rank() == rank;
        }));
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

    void landlordUsesLowestSingleAgainstFarmerAtEveryDifficulty() {
        auto observation = farmerObservation(
            handOf({{Rank::Four, 1}, {Rank::Nine, 1}, {Rank::Two, 1}}),
            cards(Rank::Three, 1), PlayerId::Player3);
        observation.publicState.players[static_cast<int>(PlayerId::Player2)].role =
            Role::Landlord;
        observation.publicState.players[static_cast<int>(PlayerId::Player4)].role =
            Role::Farmer;

        for (const auto difficulty : {AiDifficulty::Beginner,
                                      AiDifficulty::Intermediate,
                                      AiDifficulty::Advanced}) {
            StandardAiPlayer ai(difficulty);
            const auto command = ai.decidePlay(observation);
            QCOMPARE(command.type, GameCommandType::PlayCards);
            QCOMPARE(playedRank(command), Rank::Four);
        }
    }

    void farmerUsesLowestAttachmentAfterLowestBeatingTriple() {
        auto previous = cards(Rank::Three, 3);
        const auto previousPair = cards(Rank::Five, 2);
        previous.insert(previous.end(), previousPair.begin(), previousPair.end());
        const auto observation = farmerObservation(
            handOf({{Rank::Four, 3}, {Rank::Six, 2}, {Rank::King, 2}}),
            previous, PlayerId::Player4);

        for (const auto difficulty : {AiDifficulty::Beginner,
                                      AiDifficulty::Intermediate,
                                      AiDifficulty::Advanced}) {
            StandardAiPlayer ai(difficulty);
            const auto command = ai.decidePlay(observation);
            QCOMPARE(command.type, GameCommandType::PlayCards);
            QCOMPARE(static_cast<int>(command.cardIds.size()), 5);
            QCOMPARE(playedCount(command, Rank::Four), 3);
            QCOMPARE(playedCount(command, Rank::Six), 2);
            QCOMPARE(playedCount(command, Rank::King), 0);
        }
    }

    void landlordStillFinishesImmediatelyInsteadOfSavingBomb() {
        auto observation = farmerObservation(
            handOf({{Rank::Four, 4}}), cards(Rank::Three, 1), PlayerId::Player3);
        observation.publicState.players[static_cast<int>(PlayerId::Player2)].role =
            Role::Landlord;
        observation.publicState.players[static_cast<int>(PlayerId::Player4)].role =
            Role::Farmer;

        StandardAiPlayer ai(AiDifficulty::Advanced);
        const auto command = ai.decidePlay(observation);
        QCOMPARE(command.type, GameCommandType::PlayCards);
        QCOMPARE(static_cast<int>(command.cardIds.size()), 4);
        QCOMPARE(playedCount(command, Rank::Four), 4);
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

    void identicalFullStateProducesIdenticalDecision() {
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

    void feedsSingleKnownToBeBeatableByTeammate() {
        auto observation = farmerObservation(
            handOf({{Rank::Three, 1}, {Rank::Nine, 1}, {Rank::Two, 1}}), {},
            PlayerId::Player3, 8, 1);
        observation.fullInformation.available = true;
        observation.fullInformation.allHands[1] = observation.ownHand;
        observation.fullInformation.allHands[2].addCard(
            Card::create(Rank::Six, Suit::Hearts, 0));
        observation.fullInformation.allHands[3] = handOf({{Rank::Four, 1}, {Rank::Ace, 1}});

        StandardAiPlayer ai(AiDifficulty::Advanced);
        const auto command = ai.decidePlay(observation);
        QCOMPARE(command.type, GameCommandType::PlayCards);
        QCOMPARE(playedRank(command), Rank::Three);
        QCOMPARE(command.aiDecisionReason,
                 std::string("feed_safe_single_to_one_card_teammate"));
    }

    void strategicallyPassesForNextFarmerToFinish() {
        AiObservation observation;
        observation.playerId = PlayerId::Player2;
        observation.phase = GamePhase::Playing;
        observation.decisionSeed = 770031;
        observation.publicState.activePlayerCount = THREE_PLAYER_COUNT;
        observation.publicState.currentPlayer = PlayerId::Player2;

        const Card ownFour = Card::create(Rank::Four, Suit::Spades, 0);
        const Card ownFive = Card::create(Rank::Five, Suit::Spades, 0);
        observation.ownHand.addCards({ownFour, ownFive});

        for (int index = 0; index < THREE_PLAYER_COUNT; ++index) {
            auto& player = observation.publicState.players[index];
            player.id = static_cast<PlayerId>(index);
            player.role = index == 0 ? Role::Landlord : Role::Farmer;
            player.roleRevealed = true;
        }
        observation.publicState.players[0].remainingCards = 2;
        observation.publicState.players[1].remainingCards = 2;
        observation.publicState.players[2].remainingCards = 1;

        const Card lastThree = Card::create(Rank::Three, Suit::Spades, 0);
        observation.publicState.lastPlayedCards = {lastThree};
        observation.publicState.lastPlayedBy = PlayerId::Player1;
        observation.publicState.lastPlayedValid = true;

        const std::array<CardId, 5> cardsStillHeld{
            ownFour.id(), ownFive.id(),
            Card::create(Rank::Six, Suit::Spades, 0).id(),
            Card::create(Rank::Seven, Suit::Spades, 0).id(),
            Card::create(Rank::Eight, Suit::Spades, 0).id(),
        };
        PublicActionRecord publicHistory;
        publicHistory.type = PublicActionType::Play;
        for (const auto& card : Deck::createForPlayerCount(THREE_PLAYER_COUNT)) {
            if (std::find(cardsStillHeld.begin(), cardsStillHeld.end(), card.id()) ==
                cardsStillHeld.end()) {
                publicHistory.cards.push_back(card);
            }
        }
        observation.publicState.actionHistory.push_back(std::move(publicHistory));
        observation.fullInformation.available = true;
        observation.fullInformation.allHands[0].addCards({
            Card::create(Rank::Six, Suit::Spades, 0),
            Card::create(Rank::Seven, Suit::Spades, 0)});
        observation.fullInformation.allHands[1] = observation.ownHand;
        observation.fullInformation.allHands[2].addCard(
            Card::create(Rank::Eight, Suit::Spades, 0));

        StandardAiPlayer ai(AiDifficulty::Advanced);
        const auto command = ai.decidePlay(observation);
        QCOMPARE(command.type, GameCommandType::Pass);
        QCOMPARE(command.aiDecisionReason,
                 std::string("strategic_pass_for_teammate_finish"));
    }
};

QTEST_MAIN(TestFarmerCooperation)
#include "test_farmer_cooperation.moc"
