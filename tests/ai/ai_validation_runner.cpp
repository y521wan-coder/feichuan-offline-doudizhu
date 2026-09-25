#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

#include <QJsonDocument>
#include <QSet>

#include "ai/standard_ai.h"
#include "ai/ai_decision_request.h"
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
    int cloudRequests = 0;
    int catalogFailures = 0;
    int maximumCatalogActions = 0;
    int trimmedCatalogs = 0;
};

bool validateObservation(const AiObservation& observation);

QString validationMoveSignature(const LegalMove& move) {
    std::array<int, RANK_COUNT> counts{};
    for (const auto& card : move.cards) ++counts[rankWeight(card.rank())];
    QString result = QStringLiteral("%1:%2:%3:")
        .arg(static_cast<int>(move.pattern.type))
        .arg(rankWeight(move.pattern.mainRank))
        .arg(move.pattern.mainLength);
    for (int index = 0; index < RANK_COUNT; ++index) {
        if (counts[static_cast<std::size_t>(index)] > 0) {
            result += QStringLiteral("%1x%2,").arg(index).arg(counts[index]);
        }
    }
    return result;
}

bool validateReducedCatalog(const AiDecisionRequest& request,
                            const AiObservation& observation) {
    QSet<QString> kept;
    QSet<QString> keptGroups;
    bool keptPass = false;
    for (const auto& action : request.actions) {
        kept.insert(action.semanticSignature);
        if (action.command.type == GameCommandType::Pass) keptPass = true;
        if (action.command.type == GameCommandType::PlayCards) {
            keptGroups.insert(QStringLiteral("%1:%2")
                .arg(action.description.value("pattern").toInt())
                .arg(action.description.value("main_rank").toInt()));
        }
    }
    const bool leader = observation.publicState.lastPlayedCards.empty();
    if (!leader && !keptPass) return false;
    std::optional<CardPattern> lastPattern;
    if (!leader) {
        lastPattern = PatternAnalyzer::analyze(observation.publicState.lastPlayedCards,
                                               observation.publicState.activePlayerCount);
    }
    const auto moves = LegalMoveGenerator::generateLegalMoves(
        observation.ownHand, lastPattern, observation.publicState.activePlayerCount);
    int landlordIndex = -1;
    for (int index = 0; index < observation.publicState.activePlayerCount; ++index) {
        if (observation.publicState.players[index].role == Role::Landlord) {
            landlordIndex = index;
            break;
        }
    }
    const int nextPlayer = (static_cast<int>(observation.playerId) + 1) %
                           observation.publicState.activePlayerCount;
    const bool landlordCanWinNext = landlordIndex == nextPlayer &&
        observation.publicState.players[landlordIndex].remainingCards == 1;
    for (const auto& move : moves) {
        const QString signature = validationMoveSignature(move);
        const QString group = QStringLiteral("%1:%2")
            .arg(static_cast<int>(move.pattern.type))
            .arg(rankWeight(move.pattern.mainRank));
        if (!keptGroups.contains(group)) return false;
        if ((move.pattern.isBomb() ||
             static_cast<int>(move.cards.size()) == observation.ownHand.size()) &&
            !kept.contains(signature)) return false;
        if (landlordCanWinNext) {
            const auto responses = LegalMoveGenerator::generateLegalMoves(
                observation.fullInformation.allHands[landlordIndex], move.pattern,
                observation.publicState.activePlayerCount);
            if (responses.empty() && !kept.contains(signature)) return false;
        }
    }
    const GameCommand preferred = StandardAiPlayer(AiDifficulty::Advanced)
        .decidePlay(observation);
    auto normalized = [](std::vector<CardId> ids) {
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    return std::any_of(request.actions.begin(), request.actions.end(),
        [&](const AiLegalAction& action) {
            return action.command.type == preferred.type &&
                   normalized(action.command.cardIds) == normalized(preferred.cardIds);
        });
}

AiDifficulty parseDifficulty(const std::string& value) {
    const int level = std::stoi(value);
    if (level <= 0) return AiDifficulty::Beginner;
    if (level == 1) return AiDifficulty::Intermediate;
    return AiDifficulty::Advanced;
}

const AiLegalAction* chooseFakeModelAction(const AiDecisionRequest& request,
                                           int ownCardCount) {
    const AiLegalAction* firstPlayable = nullptr;
    for (const auto& action : request.actions) {
        if (action.command.type != GameCommandType::PlayCards) continue;
        if (static_cast<int>(action.command.cardIds.size()) == ownCardCount) {
            return &action;
        }
        if (!firstPlayable) firstPlayable = &action;
    }
    if (firstPlayable) return firstPlayable;
    return request.actions.isEmpty() ? nullptr : &request.actions.back();
}

bool runCloudCatalogGame(int playerCount, uint64_t seed, Summary& summary) {
    StandardAiPlayer human(AiDifficulty::Advanced);
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
        const GamePhase phase = engine.state().phase();
        const PlayerId player = engine.fullState().currentPlayer;
        const AiObservation observation = makeAiObservation(engine.state(), player);
        if (!validateObservation(observation)) ++summary.duplicateDeals;
        GameCommand command;
        if (player == PlayerId::Player1) {
            command = phase == GamePhase::Bidding
                ? human.decideBid(observation) : human.decidePlay(observation);
        } else {
            SeatControllerConfig controller;
            controller.kind = SeatControllerKind::CloudAi;
            controller.credentialId = QStringLiteral("fake");
            controller.model = QStringLiteral("deterministic-fake-model");
            const QString requestId = QStringLiteral("%1-%2")
                .arg(seed).arg(engine.fullState().eventSequence);
            const AiDecisionRequest request = AiActionCatalog::create(
                engine.state(), player, controller, requestId);
            ++summary.cloudRequests;
            summary.maximumCatalogActions = std::max(
                summary.maximumCatalogActions, static_cast<int>(request.actions.size()));
            if (request.actions.size() == AI_MAX_ACTIONS) ++summary.trimmedCatalogs;
            if (request.actions.isEmpty() || request.actions.size() > AI_MAX_ACTIONS ||
                QJsonDocument(request.toServiceJson()).toJson(QJsonDocument::Compact).size() >
                    AI_MAX_MESSAGE_BYTES ||
                request.gameId != engine.state().gameId() ||
                request.eventSequence != engine.fullState().eventSequence ||
                request.phase != phase || request.playerId != player) {
                ++summary.catalogFailures;
                return false;
            }
            if (request.actions.size() == AI_MAX_ACTIONS &&
                !validateReducedCatalog(request, observation)) {
                ++summary.catalogFailures;
                return false;
            }
            const AiLegalAction* chosen = nullptr;
            if (phase == GamePhase::Bidding) {
                chosen = &request.actions.back();
            } else if (phase == GamePhase::Playing) {
                chosen = chooseFakeModelAction(request, observation.ownHand.size());
            }
            if (!chosen || AiActionCatalog::find(request, chosen->actionId) != chosen) {
                ++summary.catalogFailures;
                return false;
            }
            command = chosen->command;
        }
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
    return true;
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
    std::cout << label
              << " cloud_requests=" << summary.cloudRequests
              << " catalog_failures=" << summary.catalogFailures
              << " max_catalog_actions=" << summary.maximumCatalogActions
              << " trimmed_catalogs=" << summary.trimmedCatalogs << '\n';
}

int runCloudStability(int playerCount, int games, int firstSeed) {
    Summary summary;
    summary.requested = games;
    for (int offset = 0; offset < games; ++offset) {
        runCloudCatalogGame(playerCount, static_cast<uint64_t>(firstSeed + offset), summary);
    }
    printSummary("CLOUD_STABILITY players=" + std::to_string(playerCount), summary);
    return summary.completed == games && summary.illegal == 0 &&
           summary.unfinished == 0 && summary.duplicateDeals == 0 &&
           summary.catalogFailures == 0 ? 0 : 1;
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
    if (mode == "cloud-stability" && argc == 5) {
        return runCloudStability(std::stoi(argv[2]), std::stoi(argv[3]),
                                 std::stoi(argv[4]));
    }
    std::cerr << "invalid arguments\n";
    return 2;
}
