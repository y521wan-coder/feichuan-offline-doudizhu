#include "game_state.h"
#include <QJsonArray>

namespace fpdz {

GameState::GameState() {
    m_fullState.players[0].id = PlayerId::Player1;
    m_fullState.players[0].name = L"玩家一";
    m_fullState.players[0].seat = SeatPosition::East;
    m_fullState.players[0].isHuman = true;

    m_fullState.players[1].id = PlayerId::Player2;
    m_fullState.players[1].name = L"玩家二";
    m_fullState.players[1].seat = SeatPosition::South;

    m_fullState.players[2].id = PlayerId::Player3;
    m_fullState.players[2].name = L"玩家三";
    m_fullState.players[2].seat = SeatPosition::West;

    m_fullState.players[3].id = PlayerId::Player4;
    m_fullState.players[3].name = L"玩家四";
    m_fullState.players[3].seat = SeatPosition::North;
}

PublicGameSnapshot GameState::publicSnapshot() const {
    PublicGameSnapshot snap;
    snap.gameId = m_fullState.gameId;
    snap.phase = m_phase;
    snap.currentPlayer = m_fullState.currentPlayer;
    snap.bottomCards = m_fullState.bottomCardsRevealed ? m_fullState.bottomCards : std::vector<Card>{};
    snap.bottomCardsRevealed = m_fullState.bottomCardsRevealed;
    snap.lastPlayedCards = m_fullState.lastPlayedCards;
    snap.lastPlayedBy = m_fullState.lastPlayedBy;
    snap.lastPlayedValid = !m_fullState.lastPlayedCards.empty();
    snap.baseScore = m_fullState.baseScore;
    snap.currentMultiplier = m_fullState.currentMultiplier;
    snap.bombCount = m_fullState.bombCount;
    snap.roundResult = m_fullState.roundResult;
    snap.consecutivePasses = m_fullState.consecutivePasses;
    snap.actionHistory = m_fullState.actionHistory;

    for (int i = 0; i < PLAYER_COUNT; ++i) {
        auto& ps = m_fullState.players[i];
        auto& pub = snap.players[i];
        pub.id = ps.id;
        pub.name = ps.name;
        pub.seat = ps.seat;
        pub.role = ps.role;
        pub.roleRevealed = (ps.role != Role::Undetermined);
        pub.remainingCards = ps.hand.size();
        pub.bidScore = ps.bidScore;
        pub.hasPassedBid = ps.hasPassedBid;
        pub.lastActionWasPass = ps.lastActionWasPass;
    }

    return snap;
}

QJsonObject GameState::toJson() const {
    QJsonObject json;
    json["schemaVersion"] = 3;
    json["phase"] = static_cast<int>(m_phase);
    json["previousPhase"] = static_cast<int>(m_previousPhase);
    json["gameId"] = static_cast<qint64>(m_fullState.gameId);
    json["randomSeed"] = static_cast<qint64>(m_fullState.randomSeed);
    json["deterministicRandom"] = m_fullState.deterministicRandom;
    json["currentPlayer"] = static_cast<int>(m_fullState.currentPlayer);
    json["lastPlayedBy"] = static_cast<int>(m_fullState.lastPlayedBy);
    json["consecutivePasses"] = m_fullState.consecutivePasses;
    json["currentBid"] = m_fullState.currentBid;
    json["highestBidder"] = static_cast<int>(m_fullState.highestBidder);
    json["highestBid"] = m_fullState.highestBid;
    json["biddingStartPlayer"] = static_cast<int>(m_fullState.biddingStartPlayer);
    json["biddingPlayerCount"] = m_fullState.biddingPlayerCount;
    json["baseScore"] = m_fullState.baseScore;
    json["currentMultiplier"] = static_cast<qint64>(m_fullState.currentMultiplier);
    json["bombCount"] = m_fullState.bombCount;
    json["bottomCardsRevealed"] = m_fullState.bottomCardsRevealed;
    json["eventSequence"] = static_cast<qint64>(m_fullState.eventSequence);
    json["springDetected"] = m_fullState.springDetected;
    json["antiSpringDetected"] = m_fullState.antiSpringDetected;
    QJsonObject roundResult;
    roundResult["valid"] = m_fullState.roundResult.valid;
    roundResult["winner"] = static_cast<int>(m_fullState.roundResult.winner);
    roundResult["landlordWon"] = m_fullState.roundResult.landlordWon;
    roundResult["spring"] = m_fullState.roundResult.spring;
    roundResult["antiSpring"] = m_fullState.roundResult.antiSpring;
    roundResult["finalMultiplier"] = static_cast<qint64>(m_fullState.roundResult.finalMultiplier);
    QJsonArray scoreChanges;
    for (const auto score : m_fullState.roundResult.scoreChanges) {
        scoreChanges.append(static_cast<qint64>(score));
    }
    roundResult["scoreChanges"] = scoreChanges;
    json["roundResult"] = roundResult;
    json["ruleVersion"] = m_ruleSet.ruleVersion;

    // 玩家状态
    QJsonArray playersArray;
    for (int i = 0; i < PLAYER_COUNT; ++i) {
        QJsonObject playerJson;
        const auto& p = m_fullState.players[i];
        playerJson["id"] = static_cast<int>(p.id);
        playerJson["name"] = QString::fromStdWString(p.name);
        playerJson["seat"] = static_cast<int>(p.seat);
        playerJson["role"] = static_cast<int>(p.role);
        playerJson["bidScore"] = p.bidScore;
        playerJson["hasPassedBid"] = p.hasPassedBid;
        playerJson["isHuman"] = p.isHuman;
        playerJson["cardsPlayedCount"] = p.cardsPlayedCount;
        playerJson["hasPlayedThisRound"] = p.hasPlayedThisRound;
        
        // 手牌
        QJsonArray cardsArray;
        for (const auto& card : p.hand.cards()) {
            cardsArray.append(static_cast<int>(card.id()));
        }
        playerJson["hand"] = cardsArray;
        
        playersArray.append(playerJson);
    }
    json["players"] = playersArray;

    QJsonArray historyArray;
    for (const auto& action : m_fullState.actionHistory) {
        QJsonObject actionJson;
        actionJson["type"] = static_cast<int>(action.type);
        actionJson["playerId"] = static_cast<int>(action.playerId);
        actionJson["bidValue"] = action.bidValue;
        actionJson["sequence"] = static_cast<qint64>(action.sequence);
        QJsonArray cards;
        for (const auto& card : action.cards) cards.append(static_cast<int>(card.id()));
        actionJson["cards"] = cards;
        historyArray.append(actionJson);
    }
    json["publicActionHistory"] = historyArray;

    // 底牌
    QJsonArray bottomCardsArray;
    for (const auto& card : m_fullState.bottomCards) {
        bottomCardsArray.append(static_cast<int>(card.id()));
    }
    json["bottomCards"] = bottomCardsArray;

    // 上一手牌
    QJsonArray lastPlayedArray;
    for (const auto& card : m_fullState.lastPlayedCards) {
        lastPlayedArray.append(static_cast<int>(card.id()));
    }
    json["lastPlayedCards"] = lastPlayedArray;

    return json;
}

GameState GameState::fromJson(const QJsonObject& json) {
    GameState state;
    
    state.m_phase = static_cast<GamePhase>(json["phase"].toInt());
    state.m_previousPhase = static_cast<GamePhase>(json["previousPhase"].toInt());
    state.m_fullState.gameId = json["gameId"].toVariant().toULongLong();
    state.m_fullState.randomSeed = json["randomSeed"].toVariant().toULongLong();
    state.m_fullState.deterministicRandom = json["deterministicRandom"].toBool(false);
    state.m_fullState.currentPlayer = static_cast<PlayerId>(json["currentPlayer"].toInt());
    state.m_fullState.lastPlayedBy = static_cast<PlayerId>(json["lastPlayedBy"].toInt());
    state.m_fullState.consecutivePasses = json["consecutivePasses"].toInt();
    state.m_fullState.currentBid = json["currentBid"].toInt();
    state.m_fullState.highestBidder = static_cast<PlayerId>(json["highestBidder"].toInt());
    state.m_fullState.highestBid = json["highestBid"].toInt();
    state.m_fullState.biddingStartPlayer = static_cast<PlayerId>(json["biddingStartPlayer"].toInt());
    state.m_fullState.biddingPlayerCount = json["biddingPlayerCount"].toInt();
    state.m_fullState.baseScore = json["baseScore"].toInt();
    state.m_fullState.currentMultiplier = json["currentMultiplier"].toVariant().toULongLong();
    state.m_fullState.bombCount = json["bombCount"].toInt();
    state.m_fullState.bottomCardsRevealed = json["bottomCardsRevealed"].toBool();
    state.m_fullState.eventSequence = json["eventSequence"].toVariant().toULongLong();
    state.m_fullState.springDetected = json["springDetected"].toBool(false);
    state.m_fullState.antiSpringDetected = json["antiSpringDetected"].toBool(false);
    const auto roundResult = json["roundResult"].toObject();
    state.m_fullState.roundResult.valid = roundResult["valid"].toBool(false);
    state.m_fullState.roundResult.winner = static_cast<PlayerId>(roundResult["winner"].toInt());
    state.m_fullState.roundResult.landlordWon = roundResult["landlordWon"].toBool(false);
    state.m_fullState.roundResult.spring = roundResult["spring"].toBool(false);
    state.m_fullState.roundResult.antiSpring = roundResult["antiSpring"].toBool(false);
    state.m_fullState.roundResult.finalMultiplier =
        roundResult["finalMultiplier"].toVariant().toLongLong();
    const auto scoreChanges = roundResult["scoreChanges"].toArray();
    for (int i = 0; i < scoreChanges.size() && i < PLAYER_COUNT; ++i) {
        state.m_fullState.roundResult.scoreChanges[static_cast<size_t>(i)] =
            scoreChanges[i].toVariant().toLongLong();
    }

    // 玩家
    auto playersArray = json["players"].toArray();
    for (int i = 0; i < playersArray.size() && i < PLAYER_COUNT; ++i) {
        auto playerJson = playersArray[i].toObject();
        auto& p = state.m_fullState.players[i];
        p.id = static_cast<PlayerId>(playerJson["id"].toInt());
        p.name = playerJson["name"].toString().toStdWString();
        p.seat = static_cast<SeatPosition>(playerJson["seat"].toInt());
        p.role = static_cast<Role>(playerJson["role"].toInt());
        p.bidScore = playerJson["bidScore"].toInt();
        p.hasPassedBid = playerJson["hasPassedBid"].toBool();
        p.isHuman = playerJson["isHuman"].toBool();
        p.cardsPlayedCount = playerJson["cardsPlayedCount"].toInt();
        p.hasPlayedThisRound = playerJson["hasPlayedThisRound"].toBool();
        
        // 手牌
        p.hand.clear();
        auto cardsArray = playerJson["hand"].toArray();
        for (const auto& cardVal : cardsArray) {
            CardId id = static_cast<CardId>(cardVal.toInt());
            Card card = Card::create(id);
            if (card.isValid()) {
                p.hand.addCard(card);
            }
        }
        p.hand.sortByRank();
    }

    state.m_fullState.actionHistory.clear();
    const auto historyArray = json["publicActionHistory"].toArray();
    for (const auto& historyValue : historyArray) {
        const auto actionJson = historyValue.toObject();
        PublicActionRecord action;
        action.type = static_cast<PublicActionType>(actionJson["type"].toInt());
        action.playerId = static_cast<PlayerId>(actionJson["playerId"].toInt());
        action.bidValue = actionJson["bidValue"].toInt();
        action.sequence = actionJson["sequence"].toVariant().toULongLong();
        for (const auto& cardValue : actionJson["cards"].toArray()) {
            const auto card = Card::create(static_cast<CardId>(cardValue.toInt()));
            if (card.isValid()) action.cards.push_back(card);
        }
        state.m_fullState.actionHistory.push_back(std::move(action));
    }

    // 底牌
    state.m_fullState.bottomCards.clear();
    auto bottomCardsArray = json["bottomCards"].toArray();
    for (const auto& cardVal : bottomCardsArray) {
        CardId id = static_cast<CardId>(cardVal.toInt());
        Card card = Card::create(id);
        if (card.isValid()) {
            state.m_fullState.bottomCards.push_back(card);
        }
    }
    if (state.m_phase == GamePhase::Bidding &&
        state.m_fullState.bottomCards.size() == BOTTOM_CARDS) {
        state.m_fullState.bottomCardsRevealed = true;
    }

    // 上一手牌
    state.m_fullState.lastPlayedCards.clear();
    auto lastPlayedArray = json["lastPlayedCards"].toArray();
    for (const auto& cardVal : lastPlayedArray) {
        CardId id = static_cast<CardId>(cardVal.toInt());
        Card card = Card::create(id);
        if (card.isValid()) {
            state.m_fullState.lastPlayedCards.push_back(card);
        }
    }

    return state;
}

} // namespace fpdz
