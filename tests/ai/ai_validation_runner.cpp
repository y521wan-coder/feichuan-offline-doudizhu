#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

#include "ai/standard_ai.h"
#include "core/engine/game_engine.h"
#include "core/rules/pattern_analyzer.h"

using namespace fpdz;

namespace {

struct Summary {
    int requested = 0;
    int completed = 0;
    int illegal = 0;
    int unfinished = 0;
    int duplicateDeals = 0;
    int setAsidePlayed = 0;
    int teammateBombs = 0;
    int landlordWins = 0;
    int farmerWins = 0;
    std::array<int, PLAYER_COUNT> winningSeats{};
    double maximumDecisionMs = 0.0;
};

AiDifficulty parseDifficulty(const std::string& value) {
    const int level = std::stoi(value);
    if (level <= 0) return AiDifficulty::Beginner;
    if (level == 1) return AiDifficulty::Intermediate;
    return AiDifficulty::Advanced;
}

const char* difficultyName(AiDifficulty difficulty) {
    switch (difficulty) {
    case AiDifficulty::Beginner: return "beginner";
    case AiDifficulty::Intermediate: return "intermediate";
    case AiDifficulty::Advanced: return "advanced";
    }
    return "unknown";
}

bool validateObservation(const AiObservation& observation) {
    if (!observation.fullInformation.available) return false;
    std::array<bool, TOTAL_CARDS> seen{};
    for (int player = 0; player < observation.publicState.activePlayerCount; ++player) {
        for (const auto& card : observation.fullInformation.allHands[player].cards()) {
            if (seen[card.id()]) return false;
            seen[card.id()] = true;
        }
    }
    for (const auto& action : observation.publicState.actionHistory) {
        if (action.type != PublicActionType::Play) continue;
        for (const auto& card : action.cards) {
            if (seen[card.id()]) return false;
            seen[card.id()] = true;
        }
    }
    if (observation.publicState.activePlayerCount == TWO_PLAYER_COUNT) {
        if (observation.fullInformation.setAsideCards.size() !=
            TWO_PLAYER_SET_ASIDE_CARDS) return false;
        for (const auto& card : observation.fullInformation.setAsideCards) {
            if (seen[card.id()]) return false;
            seen[card.id()] = true;
        }
    }
    return true;
}

bool containsCard(const std::vector<Card>& cards, CardId id) {
    return std::any_of(cards.begin(), cards.end(),
        [id](const Card& card) { return card.id() == id; });
}

bool runGame(int playerCount, uint64_t seed,
             AiDifficulty landlordDifficulty,
             AiDifficulty farmerDifficulty,
             AiDifficulty biddingDifficulty,
             Summary& summary) {
    StandardAiPlayer landlord(landlordDifficulty);
    StandardAiPlayer farmer(farmerDifficulty);
    StandardAiPlayer bidder(biddingDifficulty);
    GameEngine engine;
    GameCommand start;
    start.type = GameCommandType::StartGame;
    start.randomSeed = seed;
    start.playerCount = playerCount;
    if (!engine.execute(start).success) {
        ++summary.illegal;
        return false;
    }

    int actionsRemaining = 2000;
    while (actionsRemaining-- > 0 && engine.state().phase() != GamePhase::Finished) {
        const auto phase = engine.state().phase();
        const PlayerId player = engine.fullState().currentPlayer;
        const auto observation = makeAiObservation(engine.state(), player);
        if (!validateObservation(observation)) ++summary.duplicateDeals;
        const auto started = std::chrono::steady_clock::now();
        GameCommand command;
        if (phase == GamePhase::Bidding) {
            command = bidder.decideBid(observation);
        } else if (phase == GamePhase::Playing) {
            const Role role = observation.publicState.players[
                static_cast<int>(player)].role;
            command = role == Role::Landlord
                ? landlord.decidePlay(observation)
                : farmer.decidePlay(observation);
            for (const auto id : command.cardIds) {
                if (containsCard(observation.fullInformation.setAsideCards, id)) {
                    ++summary.setAsidePlayed;
                }
            }
            if (command.type == GameCommandType::PlayCards &&
                role == Role::Farmer &&
                observation.publicState.lastPlayedValid &&
                observation.publicState.players[
                    static_cast<int>(observation.publicState.lastPlayedBy)].role ==
                    Role::Farmer) {
                std::vector<Card> cards;
                for (const auto id : command.cardIds) cards.push_back(Card::create(id));
                const auto pattern = PatternAnalyzer::analyze(cards, playerCount);
                if (pattern.isBomb() && !command.aiTeamRuleException) {
                    ++summary.teammateBombs;
                }
            }
        } else {
            ++summary.unfinished;
            return false;
        }
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        summary.maximumDecisionMs = std::max(summary.maximumDecisionMs, elapsed);
        if (!engine.execute(command).success) {
            ++summary.illegal;
            return false;
        }
    }

    if (engine.state().phase() != GamePhase::Finished ||
        !engine.fullState().roundResult.valid) {
        ++summary.unfinished;
        return false;
    }
    ++summary.completed;
    if (engine.fullState().roundResult.landlordWon) ++summary.landlordWins;
    else ++summary.farmerWins;
    ++summary.winningSeats[static_cast<int>(engine.fullState().roundResult.winner)];
    return true;
}

void printSummary(const std::string& label, const Summary& summary) {
    std::cout << label
              << " requested=" << summary.requested
              << " completed=" << summary.completed
              << " illegal=" << summary.illegal
              << " unfinished=" << summary.unfinished
              << " duplicate_deals=" << summary.duplicateDeals
              << " set_aside_played=" << summary.setAsidePlayed
              << " teammate_bombs=" << summary.teammateBombs
              << " landlord_wins=" << summary.landlordWins
              << " farmer_wins=" << summary.farmerWins
              << " seat1=" << summary.winningSeats[0]
              << " seat2=" << summary.winningSeats[1]
              << " seat3=" << summary.winningSeats[2]
              << " seat4=" << summary.winningSeats[3]
              << " max_ms=" << summary.maximumDecisionMs << '\n';
}

int runStability(int playerCount, AiDifficulty difficulty,
                 int games, int firstSeed) {
    Summary summary;
    summary.requested = games;
    for (int offset = 0; offset < games; ++offset) {
        runGame(playerCount, static_cast<uint64_t>(firstSeed + offset),
                difficulty, difficulty, difficulty, summary);
    }
    printSummary("STABILITY players=" + std::to_string(playerCount) +
                 " level=" + difficultyName(difficulty), summary);
    return summary.completed == games && summary.illegal == 0 &&
           summary.unfinished == 0 && summary.duplicateDeals == 0 &&
           summary.setAsidePlayed == 0 && summary.teammateBombs == 0 ? 0 : 1;
}

int runComparison(int playerCount, AiDifficulty challengerDifficulty,
                  AiDifficulty referenceDifficulty, int seeds, int firstSeed) {
    Summary baseline;
    Summary challengerLandlord;
    Summary challengerFarmers;
    baseline.requested = seeds;
    challengerLandlord.requested = seeds;
    challengerFarmers.requested = seeds;
    for (int offset = 0; offset < seeds; ++offset) {
        const auto seed = static_cast<uint64_t>(firstSeed + offset);
        runGame(playerCount, seed, referenceDifficulty, referenceDifficulty,
                AiDifficulty::Advanced, baseline);
        runGame(playerCount, seed, challengerDifficulty, referenceDifficulty,
                AiDifficulty::Advanced, challengerLandlord);
        runGame(playerCount, seed, referenceDifficulty, challengerDifficulty,
                AiDifficulty::Advanced, challengerFarmers);
    }
    const std::string prefix = "COMPARE players=" + std::to_string(playerCount) +
        " challenger=" + difficultyName(challengerDifficulty) +
        " reference=" + difficultyName(referenceDifficulty);
    printSummary(prefix + " baseline", baseline);
    printSummary(prefix + " challenger_landlord", challengerLandlord);
    printSummary(prefix + " challenger_farmers", challengerFarmers);
    const int challengerWins = challengerLandlord.landlordWins +
        challengerFarmers.farmerWins;
    const int comparisons = seeds * 2;
    const double winRate = comparisons > 0
        ? 100.0 * challengerWins / comparisons : 0.0;
    std::cout << prefix << " challenger_wins=" << challengerWins
              << " comparisons=" << comparisons
              << " win_rate=" << winRate << '\n';
    const bool clean = baseline.completed == seeds &&
        challengerLandlord.completed == seeds && challengerFarmers.completed == seeds &&
        baseline.illegal + challengerLandlord.illegal + challengerFarmers.illegal == 0 &&
        baseline.unfinished + challengerLandlord.unfinished +
            challengerFarmers.unfinished == 0;
    return clean && challengerWins > seeds ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ai_validation_runner stability|compare ...\n";
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "stability" && argc == 6) {
        return runStability(std::stoi(argv[2]), parseDifficulty(argv[3]),
                            std::stoi(argv[4]), std::stoi(argv[5]));
    }
    if (mode == "compare" && argc == 7) {
        return runComparison(std::stoi(argv[2]), parseDifficulty(argv[3]),
                             parseDifficulty(argv[4]), std::stoi(argv[5]),
                             std::stoi(argv[6]));
    }
    std::cerr << "invalid arguments\n";
    return 2;
}
