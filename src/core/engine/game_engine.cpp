#include "game_engine.h"
#include <algorithm>
#include <cassert>
#include "../rules/scoring_engine.h"

namespace fpdz {

GameEngine::GameEngine() {}

CommandResult GameEngine::execute(const GameCommand& cmd) {
    switch (cmd.type) {
        case GameCommandType::StartGame:      return handleStartGame(cmd);
        case GameCommandType::Bid:            return handleBid(cmd);
        case GameCommandType::PlayCards:      return handlePlayCards(cmd);
        case GameCommandType::Pass:           return handlePass(cmd);
        case GameCommandType::Pause:          return handlePause(cmd);
        case GameCommandType::Resume:         return handleResume(cmd);
        case GameCommandType::AbandonGame:    return handleAbandon(cmd);
        case GameCommandType::StartNextRound: return handleNextRound(cmd);
        default:
            return {false, ErrorCode::InternalError, L"未知命令类型"};
    }
}

CommandResult GameEngine::handleStartGame(const GameCommand& cmd) {
    if (m_state.phase() != GamePhase::NotStarted && m_state.phase() != GamePhase::Finished) {
        return {false, ErrorCode::GameAlreadyStarted, L"游戏已经开始"};
    }

    m_state = GameState();
    m_state.setGameId(m_gameIdCounter++);

    const bool deterministicDeal = cmd.randomSeed.has_value();
    const uint64_t seed = cmd.randomSeed.value_or(0);
    m_state.fullState().randomSeed = seed;
    m_state.fullState().deterministicRandom = deterministicDeal;

    CommandResult result;
    result.success = true;
    result.stateChanged = true;

    // Create GameStarted event
    auto evt = createEvent(GameEventType::GameStarted);
    evt.message = L"游戏开始";
    result.events.push_back(evt);

    // Deal cards
    m_state.setPhase(GamePhase::Dealing);
    auto deck = Deck::createDoubleDeck();
    const bool shuffled = deterministicDeal ? (Deck::shuffle(deck, seed), true)
                                            : Deck::secureShuffle(deck);
    if (!shuffled) {
        m_state.setPhase(GamePhase::NotStarted);
        return {false, ErrorCode::InternalError, L"无法取得安全随机数，发牌已取消"};
    }
    dealCards(deck);

    auto dealEvt = createEvent(GameEventType::CardsDealt);
    dealEvt.message = L"发牌完成，每人25张";
    result.events.push_back(dealEvt);

    m_state.fullState().bottomCardsRevealed = true;
    auto revealEvt = createEvent(GameEventType::BottomCardsRevealed);
    revealEvt.cards = m_state.fullState().bottomCards;
    revealEvt.message = L"叫分底牌已公开";
    result.events.push_back(revealEvt);

    // Start bidding
    m_state.setPhase(GamePhase::Bidding);
    const uint64_t startingSeat = deterministicDeal
        ? Deck::deterministicBounded(seed ^ 0x4249445F53544152ULL, PLAYER_COUNT)
        : Deck::secureBounded(PLAYER_COUNT);
    m_state.fullState().biddingStartPlayer = static_cast<PlayerId>(startingSeat);
    m_state.fullState().currentPlayer = m_state.fullState().biddingStartPlayer;
    m_state.fullState().biddingPlayerCount = 0;

    auto bidReqEvt = createEvent(GameEventType::BidRequested);
    bidReqEvt.playerId = m_state.fullState().currentPlayer;
    bidReqEvt.message = playerIdDisplayName(m_state.fullState().currentPlayer) + L"，请叫分";
    result.events.push_back(bidReqEvt);

    return result;
}

CommandResult GameEngine::handleBid(const GameCommand& cmd) {
    if (m_state.phase() != GamePhase::Bidding) {
        return {false, ErrorCode::InvalidPhase, L"当前不在叫分阶段"};
    }

    auto& fs = m_state.fullState();
    if (cmd.playerId != fs.currentPlayer) {
        return {false, ErrorCode::NotCurrentPlayer, L"还没轮到你叫分"};
    }

    // Validate bid value
    if (cmd.bidValue < 0 || cmd.bidValue > RuleSet::MAX_BID) {
        return {false, ErrorCode::InvalidBid, L"叫分无效，请选择0到3分"};
    }

    // Must be higher than current highest (unless passing with 0)
    if (cmd.bidValue > 0 && cmd.bidValue <= fs.highestBid) {
        return {false, ErrorCode::BidTooLow, L"叫分必须高于当前最高分" + std::to_wstring(fs.highestBid)};
    }

    CommandResult result;
    result.success = true;
    result.stateChanged = true;

    auto& player = fs.players[static_cast<int>(cmd.playerId)];

    if (cmd.bidValue == 0) {
        player.hasPassedBid = true;
        player.bidScore = 0;
    } else {
        player.bidScore = cmd.bidValue;
        fs.highestBid = cmd.bidValue;
        fs.highestBidder = cmd.playerId;
    }

    fs.biddingPlayerCount++;

    auto evt = createEvent(GameEventType::PlayerBid);
    evt.playerId = cmd.playerId;
    evt.bidValue = cmd.bidValue;
    if (cmd.bidValue == 0) {
        evt.message = playerIdDisplayName(cmd.playerId) + L"不叫";
    } else {
        evt.message = playerIdDisplayName(cmd.playerId) + L"叫" + std::to_wstring(cmd.bidValue) + L"分";
    }
    result.events.push_back(evt);
    fs.actionHistory.push_back(PublicActionRecord{
        PublicActionType::Bid, cmd.playerId, cmd.bidValue, {}, evt.sequence});

    // Check if bid 3 - immediate landlord
    if (cmd.bidValue == 3) {
        // Determine landlord
        player.role = Role::Landlord;
        for (int i = 0; i < PLAYER_COUNT; ++i) {
            if (i != static_cast<int>(cmd.playerId)) {
                fs.players[i].role = Role::Farmer;
            }
        }
        fs.baseScore = 3;

        // Give bottom cards to landlord
        for (const auto& card : fs.bottomCards) {
            player.hand.addCard(card);
        }
        player.hand.sortByRank();
        fs.bottomCardsRevealed = true;
        fs.currentPlayer = cmd.playerId; // Landlord plays first

        m_state.setPhase(GamePhase::Playing);

        auto llEvt = createEvent(GameEventType::LandlordDetermined);
        llEvt.playerId = cmd.playerId;
        llEvt.message = playerIdDisplayName(cmd.playerId) + L"成为地主！";
        result.events.push_back(llEvt);

        return result;
    }

    // Check if bidding round is over
    int passCount = 0;
    for (int i = 0; i < PLAYER_COUNT; ++i) {
        if (fs.players[i].hasPassedBid) passCount++;
    }

    const bool allPlayersActed = fs.biddingPlayerCount >= PLAYER_COUNT;
    const bool onlyHighestBidderRemains = fs.highestBid > 0 && passCount == PLAYER_COUNT - 1;
    if (allPlayersActed || onlyHighestBidderRemains) {
        // Bidding round over
        if (fs.highestBid == 0) {
            auto noBidEvt = createEvent(GameEventType::BidRequested);
            noBidEvt.message = L"无人叫分，重新发牌";
            result.events.push_back(noBidEvt);

            const bool deterministicRedeal = fs.deterministicRandom;
            const uint64_t previousSeed = fs.randomSeed;
            const int redealCount = fs.consecutiveRedeals + 1;
            m_state.setPhase(GamePhase::NotStarted);
            GameCommand redealCmd;
            redealCmd.type = GameCommandType::StartGame;
            if (deterministicRedeal) {
                redealCmd.randomSeed = previousSeed ^ 0x52454445414C5F31ULL ^
                    (static_cast<uint64_t>(redealCount) * 0x9E3779B97F4A7C15ULL);
            }
            auto redealResult = handleStartGame(redealCmd);
            if (!redealResult.success) {
                return redealResult;
            }
            result.events.insert(result.events.end(),
                                 redealResult.events.begin(),
                                 redealResult.events.end());
            m_state.fullState().consecutiveRedeals = redealCount;
            return result;
        }

        // Highest bidder becomes landlord
        auto& landlord = fs.players[static_cast<int>(fs.highestBidder)];
        landlord.role = Role::Landlord;
        for (int i = 0; i < PLAYER_COUNT; ++i) {
            if (i != static_cast<int>(fs.highestBidder)) {
                fs.players[i].role = Role::Farmer;
            }
        }
        fs.baseScore = fs.highestBid;

        for (const auto& card : fs.bottomCards) {
            landlord.hand.addCard(card);
        }
        landlord.hand.sortByRank();
        fs.bottomCardsRevealed = true;
        fs.currentPlayer = fs.highestBidder;

        m_state.setPhase(GamePhase::Playing);

        auto llEvt = createEvent(GameEventType::LandlordDetermined);
        llEvt.playerId = fs.highestBidder;
        llEvt.message = playerIdDisplayName(fs.highestBidder) + L"成为地主！";
        result.events.push_back(llEvt);

        return result;
    }

    // Advance to next player who hasn't decided
    do {
        fs.currentPlayer = nextPlayer(fs.currentPlayer);
    } while (fs.players[static_cast<int>(fs.currentPlayer)].hasPassedBid);

    auto nextEvt = createEvent(GameEventType::BidRequested);
    nextEvt.playerId = fs.currentPlayer;
    nextEvt.message = playerIdDisplayName(fs.currentPlayer) + L"，请叫分";
    result.events.push_back(nextEvt);

    return result;
}

CommandResult GameEngine::handlePlayCards(const GameCommand& cmd) {
    if (m_state.phase() != GamePhase::Playing) {
        return {false, ErrorCode::InvalidPhase, L"当前不在出牌阶段"};
    }

    auto& fs = m_state.fullState();
    if (cmd.playerId != fs.currentPlayer) {
        return {false, ErrorCode::NotCurrentPlayer, L"还没轮到你出牌"};
    }

    if (cmd.cardIds.empty()) {
        return {false, ErrorCode::InvalidCards, L"请选择要出的牌"};
    }

    // Check for duplicate IDs
    auto sortedIds = cmd.cardIds;
    std::sort(sortedIds.begin(), sortedIds.end());
    for (size_t i = 1; i < sortedIds.size(); ++i) {
        if (sortedIds[i] == sortedIds[i-1]) {
            return {false, ErrorCode::DuplicateCardId, L"选择了重复的牌"};
        }
    }

    // Check all cards are in hand
    auto& player = fs.players[static_cast<int>(cmd.playerId)];
    for (CardId id : cmd.cardIds) {
        if (!player.hand.contains(id)) {
            return {false, ErrorCode::CardsNotInHand, L"选择的牌不在手牌中"};
        }
    }

    // Gather cards
    std::vector<Card> playedCards;
    for (CardId id : cmd.cardIds) {
        for (const auto& c : player.hand.cards()) {
            if (c.id() == id) {
                playedCards.push_back(c);
                break;
            }
        }
    }

    // Analyze pattern
    CardPattern pattern = PatternAnalyzer::analyze(playedCards);
    if (!pattern.isValid()) {
        return {false, ErrorCode::InvalidPattern, L"选择的牌不能组成有效牌型"};
    }

    // Check if it can beat the last play
    if (!TurnManager::isLeader(m_state)) {
        CardPattern lastPattern = PatternAnalyzer::analyze(fs.lastPlayedCards);
        if (!PatternComparator::canBeat(pattern, lastPattern)) {
            return {false, ErrorCode::CannotBeatLastPlay, L"无法压过上一手牌"};
        }
    }

    // Execute the play
    CommandResult result;
    result.success = true;
    result.stateChanged = true;

    // Remove cards from hand
    player.hand.removeCards(cmd.cardIds);

    // Apply bomb multiplier
    if (pattern.isBomb()) {
        applyBombMultiplier(pattern.type);
    }

    // Record play
    TurnManager::recordPlay(m_state, cmd.playerId, playedCards);

    auto evt = createEvent(GameEventType::CardsPlayed);
    evt.playerId = cmd.playerId;
    evt.cards = playedCards;
    evt.pattern = pattern;
    evt.remainingCards = player.hand.size();
    evt.message = playerIdDisplayName(cmd.playerId) + L"出了" +
                  patternTypeName(pattern.type) + L"，" +
                  std::to_wstring(playedCards.size()) + L"张牌";
    result.events.push_back(evt);
    fs.actionHistory.push_back(PublicActionRecord{
        PublicActionType::Play, cmd.playerId, 0, playedCards, evt.sequence});

    if (pattern.isBomb()) {
        auto multiplierEvt = createEvent(GameEventType::MultiplierChanged);
        multiplierEvt.playerId = cmd.playerId;
        multiplierEvt.multiplier = fs.currentMultiplier;
        multiplierEvt.message = L"当前倍数" + std::to_wstring(fs.currentMultiplier);
        result.events.push_back(multiplierEvt);
    }

    // Check if player won (no cards left)
    if (player.hand.empty()) {
        m_state.setPhase(GamePhase::Settling);

        auto finishEvt = createEvent(GameEventType::GameFinished);
        finishEvt.playerId = cmd.playerId;
        const auto score = ScoringEngine::calculate(fs);
        fs.springDetected = score.spring;
        fs.antiSpringDetected = score.antiSpring;
        fs.roundResult.valid = true;
        fs.roundResult.winner = cmd.playerId;
        fs.roundResult.landlordWon = score.landlordWon;
        fs.roundResult.spring = score.spring;
        fs.roundResult.antiSpring = score.antiSpring;
        fs.roundResult.finalMultiplier = score.totalMultiplier;
        fs.roundResult.scoreChanges = score.scoreChanges;

        finishEvt.landlordWon = score.landlordWon;
        finishEvt.spring = score.spring;
        finishEvt.antiSpring = score.antiSpring;
        finishEvt.multiplier = score.totalMultiplier;
        finishEvt.scoreChanges = score.scoreChanges;
        if (score.landlordWon) {
            finishEvt.message = L"地主获胜！";
        } else {
            finishEvt.message = L"农民获胜！";
        }
        result.events.push_back(finishEvt);

        m_state.setPhase(GamePhase::Finished);
        return result;
    }

    // Check for low card warnings
    if (player.hand.size() <= 2) {
        auto warnEvt = createEvent(GameEventType::PlayerLowCards);
        warnEvt.playerId = cmd.playerId;
        warnEvt.remainingCards = player.hand.size();
        warnEvt.message = playerIdDisplayName(cmd.playerId) + L"还剩" +
                          std::to_wstring(player.hand.size()) + L"张牌！";
        result.events.push_back(warnEvt);
    }

    // Advance turn
    TurnManager::advanceTurn(m_state);

    // Skip passed players
    // In our model, passes are per-trick, not permanent
    // So we just advance to next player

    auto turnEvt = createEvent(GameEventType::TurnChanged);
    turnEvt.playerId = fs.currentPlayer;
    turnEvt.message = L"轮到" + playerIdDisplayName(fs.currentPlayer);
    result.events.push_back(turnEvt);

    return result;
}

CommandResult GameEngine::handlePass(const GameCommand& cmd) {
    if (m_state.phase() != GamePhase::Playing) {
        return {false, ErrorCode::InvalidPhase, L"当前不在出牌阶段"};
    }

    auto& fs = m_state.fullState();
    if (cmd.playerId != fs.currentPlayer) {
        return {false, ErrorCode::NotCurrentPlayer, L"还没轮到你出牌"};
    }

    const bool leaderPass = TurnManager::isLeader(m_state);
    if (leaderPass && !cmd.allowPassAsLeader) {
        return {false, ErrorCode::CannotPassAsLeader, L"新一轮你必须出牌"};
    }

    CommandResult result;
    result.success = true;
    result.stateChanged = true;

    auto evt = createEvent(GameEventType::PlayerPassed);
    evt.playerId = cmd.playerId;
    evt.message = playerIdDisplayName(cmd.playerId) + L"过牌";
    result.events.push_back(evt);
    fs.actionHistory.push_back(PublicActionRecord{
        PublicActionType::Pass, cmd.playerId, 0, {}, evt.sequence});

    if (leaderPass) {
        TurnManager::advanceTurn(m_state);

        auto turnEvt = createEvent(GameEventType::TurnChanged);
        turnEvt.playerId = fs.currentPlayer;
        turnEvt.message = L"轮到" + playerIdDisplayName(fs.currentPlayer);
        result.events.push_back(turnEvt);

        return result;
    }

    TurnManager::recordPass(m_state);

    // Check if 3 consecutive passes -> trick reset
    if (fs.consecutivePasses >= 3) {
        // The last player who played gets free turn
        fs.currentPlayer = fs.lastPlayedBy;
        fs.consecutivePasses = 0;
        fs.lastPlayedCards.clear();

        auto resetEvt = createEvent(GameEventType::TrickReset);
        resetEvt.playerId = fs.lastPlayedBy;
        resetEvt.message = L"其他玩家均过牌，" + playerIdDisplayName(fs.lastPlayedBy) + L"获得自由出牌权";
        result.events.push_back(resetEvt);

        return result;
    }

    // Advance turn
    TurnManager::advanceTurn(m_state);

    auto turnEvt = createEvent(GameEventType::TurnChanged);
    turnEvt.playerId = fs.currentPlayer;
    turnEvt.message = L"轮到" + playerIdDisplayName(fs.currentPlayer);
    result.events.push_back(turnEvt);

    return result;
}

CommandResult GameEngine::handlePause(const GameCommand&) {
    if (m_state.phase() != GamePhase::Playing && m_state.phase() != GamePhase::Bidding) {
        return {false, ErrorCode::InvalidPhase, L"只能在牌局进行中停战"};
    }

    m_state.setPreviousPhase(m_state.phase());
    m_state.setPhase(GamePhase::Paused);

    CommandResult result;
    result.success = true;
    result.stateChanged = true;

    auto evt = createEvent(GameEventType::GamePaused);
    evt.message = L"游戏已暂停";
    result.events.push_back(evt);

    return result;
}

CommandResult GameEngine::handleResume(const GameCommand&) {
    if (m_state.phase() != GamePhase::Paused) {
        return {false, ErrorCode::InvalidPhase, L"游戏未在暂停状态"};
    }

    m_state.setPhase(m_state.previousPhase());

    CommandResult result;
    result.success = true;
    result.stateChanged = true;

    auto evt = createEvent(GameEventType::GameResumed);
    evt.message = L"游戏已恢复";
    result.events.push_back(evt);

    return result;
}

CommandResult GameEngine::handleAbandon(const GameCommand&) {
    m_state.setPhase(GamePhase::Finished);

    CommandResult result;
    result.success = true;
    result.stateChanged = true;
    return result;
}

CommandResult GameEngine::handleNextRound(const GameCommand& cmd) {
    if (m_state.phase() != GamePhase::Finished) {
        return {false, ErrorCode::InvalidPhase, L"请先结束当前牌局"};
    }

    // Start a new game
    GameCommand newCmd;
    newCmd.type = GameCommandType::StartGame;
    newCmd.randomSeed = cmd.randomSeed;
    return handleStartGame(newCmd);
}

void GameEngine::dealCards(const std::vector<Card>& deck) {
    auto& fs = m_state.fullState();

    // Reserve bottom cards (last 8)
    fs.bottomCards.clear();
    for (int i = TOTAL_CARDS - BOTTOM_CARDS; i < TOTAL_CARDS; ++i) {
        fs.bottomCards.push_back(deck[i]);
    }

    // Deal 25 cards to each player
    for (int p = 0; p < PLAYER_COUNT; ++p) {
        fs.players[p].hand.clear();
        for (int i = 0; i < CARDS_PER_PLAYER; ++i) {
            fs.players[p].hand.addCard(deck[p * CARDS_PER_PLAYER + i]);
        }
        fs.players[p].hand.sortByRank();
        fs.players[p].role = Role::Undetermined;
        fs.players[p].bidScore = 0;
        fs.players[p].hasPassedBid = false;
        fs.players[p].cardsPlayedCount = 0;
        fs.players[p].hasPlayedThisRound = false;
    }
}

GameEvent GameEngine::createEvent(GameEventType type) {
    GameEvent evt;
    evt.eventId = m_state.gameId() * 1000000 + m_state.fullState().eventSequence;
    evt.gameId = m_state.gameId();
    evt.sequence = m_state.nextEventSequence();
    evt.type = type;
    return evt;
}

void GameEngine::applyBombMultiplier(CardPatternType bombType) {
    auto& fs = m_state.fullState();
    int mult = bombMultiplier(bombType);
    fs.currentMultiplier *= mult;
    fs.currentMultiplier = ScoringEngine::applyMultiplierCap(fs.currentMultiplier);
    fs.bombCount++;
}

} // namespace fpdz
