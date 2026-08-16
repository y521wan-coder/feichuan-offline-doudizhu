#pragma once
#include "game_phase.h"
#include "game_command.h"
#include "game_event.h"
#include "../model/game_snapshot.h"
#include "../rules/rule_set.h"
#include <vector>
#include <functional>
#include <QJsonObject>
#include <QJsonArray>

namespace fpdz {

class GameState {
public:
    GameState();

    GamePhase phase() const { return m_phase; }
    void setPhase(GamePhase phase) { m_phase = phase; }

    FullGameState& fullState() { return m_fullState; }
    const FullGameState& fullState() const { return m_fullState; }

    PublicGameSnapshot publicSnapshot() const;

    uint64_t nextEventSequence() { return ++m_fullState.eventSequence; }

    const RuleSet& ruleSet() const { return m_ruleSet; }
    void setRuleSet(const RuleSet& rs) { m_ruleSet = rs; }

    uint64_t gameId() const { return m_fullState.gameId; }
    void setGameId(uint64_t id) { m_fullState.gameId = id; }

    GamePhase previousPhase() const { return m_previousPhase; }
    void setPreviousPhase(GamePhase p) { m_previousPhase = p; }

    // 序列化
    QJsonObject toJson() const;
    static GameState fromJson(const QJsonObject& json);

private:
    GamePhase m_phase = GamePhase::NotStarted;
    GamePhase m_previousPhase = GamePhase::NotStarted;
    FullGameState m_fullState;
    RuleSet m_ruleSet = RuleSet::defaultRules();
};

} // namespace fpdz
