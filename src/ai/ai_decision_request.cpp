#include "ai_decision_request.h"

#include "legal_move_generator.h"
#include "standard_ai.h"
#include "strategic_search_evaluator.h"
#include "ai_level_profile.h"
#include "../core/rules/pattern_analyzer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <array>

namespace fpdz {
namespace {

QString phaseName(GamePhase phase) {
    return phase == GamePhase::Bidding ? QStringLiteral("bidding") :
           phase == GamePhase::Playing ? QStringLiteral("playing") :
                                         QStringLiteral("invalid");
}

QJsonObject cardJson(const Card& card) {
    return {{QStringLiteral("id"), card.id()},
            {QStringLiteral("rank"), rankWeight(card.rank())}};
}

QJsonArray cardsJson(const std::vector<Card>& cards) {
    QJsonArray array;
    for (const auto& card : cards) array.append(cardJson(card));
    return array;
}

QString moveSignature(const LegalMove& move) {
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

bool cardIdsLess(const std::vector<Card>& left, const std::vector<Card>& right) {
    std::vector<CardId> leftIds;
    std::vector<CardId> rightIds;
    for (const auto& card : left) leftIds.push_back(card.id());
    for (const auto& card : right) rightIds.push_back(card.id());
    return leftIds < rightIds;
}

QJsonObject observationJson(const AiObservation& observation) {
    QJsonObject root;
    root[QStringLiteral("player_id")] = static_cast<int>(observation.playerId);
    root[QStringLiteral("role")] = static_cast<int>(
        observation.publicState.players[static_cast<int>(observation.playerId)].role);
    root[QStringLiteral("phase")] = phaseName(observation.phase);
    root[QStringLiteral("player_count")] = observation.publicState.activePlayerCount;
    root[QStringLiteral("highest_bid")] = observation.highestBid;
    root[QStringLiteral("base_score")] = observation.publicState.baseScore;
    root[QStringLiteral("multiplier")] = static_cast<qint64>(observation.publicState.currentMultiplier);
    root[QStringLiteral("landlord")] = -1;
    QJsonArray remaining;
    for (int index = 0; index < observation.publicState.activePlayerCount; ++index) {
        const auto& player = observation.publicState.players[index];
        remaining.append(player.remainingCards);
        if (player.role == Role::Landlord) root[QStringLiteral("landlord")] = index;
    }
    root[QStringLiteral("remaining_cards")] = remaining;
    root[QStringLiteral("last_played_by")] =
        observation.publicState.lastPlayedValid
            ? static_cast<int>(observation.publicState.lastPlayedBy) : -1;
    root[QStringLiteral("last_played_cards")] = cardsJson(observation.publicState.lastPlayedCards);
    root[QStringLiteral("consecutive_passes")] = observation.publicState.consecutivePasses;

    QJsonArray hands;
    if (observation.fullInformation.available) {
        for (int index = 0; index < observation.publicState.activePlayerCount; ++index) {
            hands.append(cardsJson(observation.fullInformation.allHands[index].cards()));
        }
    }
    root[QStringLiteral("all_hands")] = hands;
    root[QStringLiteral("bottom_cards")] = cardsJson(observation.fullInformation.hiddenBottomCards);
    root[QStringLiteral("set_aside_cards")] = cardsJson(observation.fullInformation.setAsideCards);

    QJsonArray history;
    for (const auto& action : observation.publicState.actionHistory) {
        QJsonObject item{{QStringLiteral("type"), static_cast<int>(action.type)},
                         {QStringLiteral("player_id"), static_cast<int>(action.playerId)},
                         {QStringLiteral("sequence"), static_cast<qint64>(action.sequence)}};
        if (action.type == PublicActionType::Bid) item[QStringLiteral("bid")] = action.bidValue;
        if (action.type == PublicActionType::Play) item[QStringLiteral("cards")] = cardsJson(action.cards);
        history.append(item);
    }
    root[QStringLiteral("history")] = history;
    return root;
}

QJsonObject actionJson(const AiLegalAction& action) {
    QJsonObject object = action.description;
    object[QStringLiteral("action_id")] = action.actionId;
    return object;
}

} // namespace

QJsonObject AiDecisionRequest::toServiceJson() const {
    QJsonArray catalog;
    for (const auto& action : actions) catalog.append(actionJson(action));
    QJsonObject payload = observation;
    payload[QStringLiteral("legal_actions")] = catalog;
    return {{QStringLiteral("protocol_version"), AI_SERVICE_PROTOCOL_VERSION},
            {QStringLiteral("type"), QStringLiteral("decision")},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("game_id"), static_cast<qint64>(gameId)},
            {QStringLiteral("event_sequence"), static_cast<qint64>(eventSequence)},
            {QStringLiteral("phase"), phaseName(phase)},
            {QStringLiteral("seat"), static_cast<int>(playerId)},
            {QStringLiteral("credential_id"), controller.credentialId},
            {QStringLiteral("model"), controller.model},
            {QStringLiteral("strength"), cloudStrengthApiValue(controller.strength)},
            {QStringLiteral("timeout_ms"), controller.timeoutSeconds * 1000},
            {QStringLiteral("strategy_prompt"), strategyPrompt},
            {QStringLiteral("payload"), payload}};
}

AiDecisionResponse AiDecisionResponse::fromServiceJson(const QJsonObject& json) {
    AiDecisionResponse value;
    value.requestId = json.value("request_id").toString();
    value.gameId = static_cast<uint64_t>(json.value("game_id").toInteger());
    value.eventSequence = static_cast<uint64_t>(json.value("event_sequence").toInteger());
    const QString phase = json.value("phase").toString();
    value.phase = phase == QStringLiteral("bidding") ? GamePhase::Bidding :
                  phase == QStringLiteral("playing") ? GamePhase::Playing :
                                                       GamePhase::NotStarted;
    const int seat = json.value("seat").toInt(-1);
    value.playerId = seat >= 0 && seat < PLAYER_COUNT
        ? static_cast<PlayerId>(seat) : static_cast<PlayerId>(255);
    value.success = json.value("ok").toBool(false);
    value.actionId = json.value("action_id").toInt(-1);
    value.errorCode = json.value("error_code").toString();
    value.safeMessage = json.value("message").toString();
    value.latencyMilliseconds = json.value("latency_ms").toInt();
    value.inputTokens = json.value("input_tokens").toInteger(-1);
    value.outputTokens = json.value("output_tokens").toInteger(-1);
    return value;
}

bool AiDecisionResponse::matches(const AiDecisionRequest& request,
                                 const GameState& currentState) const {
    return requestId == request.requestId && gameId == request.gameId &&
           eventSequence == request.eventSequence && phase == request.phase &&
           playerId == request.playerId && currentState.gameId() == request.gameId &&
           currentState.fullState().eventSequence == request.eventSequence &&
           currentState.phase() == request.phase &&
           currentState.fullState().currentPlayer == request.playerId;
}

AiDecisionRequest AiActionCatalog::create(const GameState& state, PlayerId playerId,
                                          const SeatControllerConfig& controller,
                                          const QString& requestId) {
    AiDecisionRequest request;
    request.requestId = requestId;
    request.gameId = state.gameId();
    request.eventSequence = state.fullState().eventSequence;
    request.phase = state.phase();
    request.playerId = playerId;
    request.controller = controller;
    const AiObservation observation = makeAiObservation(state, playerId);
    request.observation = observationJson(observation);

    if (request.phase == GamePhase::Bidding) {
        int actionId = 0;
        for (int bid = 0; bid <= 3; ++bid) {
            if (bid > 0 && bid <= observation.highestBid) continue;
            AiLegalAction action;
            action.actionId = actionId++;
            action.command.type = GameCommandType::Bid;
            action.command.playerId = playerId;
            action.command.bidValue = bid;
            action.semanticSignature = QStringLiteral("bid:%1").arg(bid);
            action.description = {{QStringLiteral("kind"), QStringLiteral("bid")},
                                  {QStringLiteral("value"), bid}};
            request.actions.append(action);
        }
        return request;
    }

    const bool leader = observation.publicState.lastPlayedCards.empty();
    if (!leader) {
        AiLegalAction pass;
        pass.command.type = GameCommandType::Pass;
        pass.command.playerId = playerId;
        pass.semanticSignature = QStringLiteral("pass");
        pass.description = {{QStringLiteral("kind"), QStringLiteral("pass")}};
        request.actions.append(pass);
    }
    std::optional<CardPattern> lastPattern;
    if (!leader) {
        lastPattern = PatternAnalyzer::analyze(observation.publicState.lastPlayedCards,
                                               observation.publicState.activePlayerCount);
    }
    auto moves = LegalMoveGenerator::generateLegalMoves(
        observation.ownHand, lastPattern, observation.publicState.activePlayerCount);
    std::sort(moves.begin(), moves.end(), [](const LegalMove& left, const LegalMove& right) {
        const auto leftKey = left.pattern.key();
        const auto rightKey = right.pattern.key();
        if (leftKey != rightKey) return leftKey < rightKey;
        return cardIdsLess(left.cards, right.cards);
    });
    QSet<QString> signatures;
    for (const auto& move : moves) {
        const QString signature = moveSignature(move);
        if (signatures.contains(signature)) continue;
        signatures.insert(signature);
        AiLegalAction action;
        action.command.type = GameCommandType::PlayCards;
        action.command.playerId = playerId;
        for (const auto& card : move.cards) action.command.cardIds.push_back(card.id());
        action.semanticSignature = signature;
        action.description = {
            {QStringLiteral("kind"), QStringLiteral("play")},
            {QStringLiteral("pattern"), static_cast<int>(move.pattern.type)},
            {QStringLiteral("main_rank"), rankWeight(move.pattern.mainRank)},
            {QStringLiteral("main_length"), move.pattern.mainLength},
            {QStringLiteral("card_count"), static_cast<int>(move.cards.size())},
            {QStringLiteral("rank_signature"), signature}};
        request.actions.append(action);
    }

    if (request.actions.size() > AI_MAX_ACTIONS) {
        StandardAiPlayer advanced(AiDifficulty::Advanced);
        const GameCommand preferred = advanced.decidePlay(observation);
        StrategicSearchEvaluator evaluator(
            observation, aiLevelProfile(AiDifficulty::Advanced));
        QVector<int> scores(request.actions.size(), -1000000000);
        QVector<bool> blocksImmediateLandlordWin(request.actions.size(), false);
        int landlordIndex = -1;
        for (int index = 0; index < observation.publicState.activePlayerCount; ++index) {
            if (observation.publicState.players[index].role == Role::Landlord) {
                landlordIndex = index;
                break;
            }
        }
        const int nextPlayer = (static_cast<int>(playerId) + 1) %
                               observation.publicState.activePlayerCount;
        const bool landlordCanWinNext = landlordIndex == nextPlayer &&
            observation.publicState.players[landlordIndex].remainingCards == 1;
        for (int index = 0; index < request.actions.size(); ++index) {
            const auto& action = request.actions[index];
            if (action.command.type != GameCommandType::PlayCards) continue;
            Hand remaining = observation.ownHand;
            remaining.removeCards(action.command.cardIds);
            LegalMove move;
            for (const CardId id : action.command.cardIds) {
                move.cards.push_back(Card::create(id));
            }
            move.pattern = PatternAnalyzer::analyze(
                move.cards, observation.publicState.activePlayerCount);
            scores[index] = evaluator.handPlanAdjustment(remaining) +
                            evaluator.refinedHandPlanBonus(remaining) +
                            evaluator.publicInformationAdjustment(move, remaining, leader);
            if (landlordCanWinNext && observation.fullInformation.available) {
                const auto responses = LegalMoveGenerator::generateLegalMoves(
                    observation.fullInformation.allHands[landlordIndex], move.pattern,
                    observation.publicState.activePlayerCount);
                blocksImmediateLandlordWin[index] = responses.empty();
            }
        }

        QSet<int> selected;
        QVector<int> selectedOrder;
        auto addIndex = [&](int index) {
            if (index < 0 || index >= request.actions.size() || selected.contains(index)) return;
            selected.insert(index);
            selectedOrder.append(index);
        };
        auto sameCards = [](std::vector<CardId> left, std::vector<CardId> right) {
            std::sort(left.begin(), left.end());
            std::sort(right.begin(), right.end());
            return left == right;
        };
        for (int index = 0; index < request.actions.size(); ++index) {
            const auto& action = request.actions[index];
            if (action.command.type == GameCommandType::Pass ||
                sameCards(action.command.cardIds, preferred.cardIds) ||
                static_cast<int>(action.command.cardIds.size()) == observation.ownHand.size() ||
                isBombType(static_cast<CardPatternType>(
                    action.description.value("pattern").toInt())) ||
                blocksImmediateLandlordWin[index]) {
                addIndex(index);
            }
        }

        // Keep the highest advanced-evaluator representative for each semantic
        // pattern/main-rank pair before filling the remaining catalog by score.
        QHash<QString, int> representatives;
        for (int index = 0; index < request.actions.size(); ++index) {
            const auto& action = request.actions[index];
            if (action.command.type != GameCommandType::PlayCards) continue;
            const QString key = QStringLiteral("%1:%2")
                .arg(action.description.value("pattern").toInt())
                .arg(action.description.value("main_rank").toInt());
            const int current = representatives.value(key, -1);
            if (current < 0 || scores[index] > scores[current] ||
                (scores[index] == scores[current] &&
                 action.semanticSignature < request.actions[current].semanticSignature)) {
                representatives[key] = index;
            }
        }
        QStringList representativeKeys = representatives.keys();
        representativeKeys.sort();
        for (const QString& key : representativeKeys) addIndex(representatives.value(key));

        QVector<int> ranked;
        ranked.reserve(request.actions.size());
        for (int index = 0; index < request.actions.size(); ++index) ranked.append(index);
        std::stable_sort(ranked.begin(), ranked.end(), [&](int left, int right) {
            if (scores[left] != scores[right]) return scores[left] > scores[right];
            return request.actions[left].semanticSignature <
                   request.actions[right].semanticSignature;
        });
        for (const int index : ranked) {
            if (selectedOrder.size() >= AI_MAX_ACTIONS) break;
            addIndex(index);
        }
        if (selectedOrder.size() > AI_MAX_ACTIONS) selectedOrder.resize(AI_MAX_ACTIONS);
        QVector<AiLegalAction> reduced;
        reduced.reserve(selectedOrder.size());
        for (const int index : selectedOrder) reduced.append(request.actions[index]);
        request.actions = std::move(reduced);
    }
    for (int index = 0; index < request.actions.size(); ++index) {
        request.actions[index].actionId = index;
    }
    return request;
}

const AiLegalAction* AiActionCatalog::find(const AiDecisionRequest& request, int actionId) {
    for (const auto& action : request.actions) {
        if (action.actionId == actionId) return &action;
    }
    return nullptr;
}

} // namespace fpdz
