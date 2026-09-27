cmake_minimum_required(VERSION 3.24)
project(FeichuanOnlineServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Sql WebSockets)

# Compile the existing rule implementation directly. The Windows UI and its
# Qt 6.8 dependency are deliberately outside this Linux build entry.
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_library(fpdz_online_rules STATIC
    ${REPO_ROOT}/src/core/audio/card_pattern_sound_plan.cpp
    ${REPO_ROOT}/src/core/model/card.cpp
    ${REPO_ROOT}/src/core/model/deck.cpp
    ${REPO_ROOT}/src/core/model/hand.cpp
    ${REPO_ROOT}/src/core/model/player.cpp
    ${REPO_ROOT}/src/core/model/game_snapshot.cpp
    ${REPO_ROOT}/src/core/rules/rule_set.cpp
    ${REPO_ROOT}/src/core/rules/rank_histogram.cpp
    ${REPO_ROOT}/src/core/rules/card_pattern.cpp
    ${REPO_ROOT}/src/core/rules/pattern_analyzer.cpp
    ${REPO_ROOT}/src/core/rules/pattern_comparator.cpp
    ${REPO_ROOT}/src/core/rules/scoring_engine.cpp
    ${REPO_ROOT}/src/core/engine/game_state.cpp
    ${REPO_ROOT}/src/core/engine/game_engine.cpp
    ${REPO_ROOT}/src/core/engine/turn_manager.cpp
    ${REPO_ROOT}/src/core/text/card_text_formatter.cpp
    ${REPO_ROOT}/src/core/text/game_text_formatter.cpp
)
target_include_directories(fpdz_online_rules PUBLIC ${REPO_ROOT}/src)
target_link_libraries(fpdz_online_rules PUBLIC Qt6::Core)

add_executable(feichuan-online-prototype main.cpp)
target_link_libraries(feichuan-online-prototype PRIVATE
    fpdz_online_rules Qt6::Core Qt6::Network Qt6::Sql Qt6::WebSockets)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
add_executable(feichuan-online-server production_main.cpp
    ${REPO_ROOT}/src/ai/ai_level_profile.cpp
    ${REPO_ROOT}/src/ai/bidding_strategy.cpp
    ${REPO_ROOT}/src/ai/farmer_team_strategy.cpp
    ${REPO_ROOT}/src/ai/full_information_search.cpp
    ${REPO_ROOT}/src/ai/imperfect_information_search.cpp
    ${REPO_ROOT}/src/ai/landlord_strategy.cpp
    ${REPO_ROOT}/src/ai/legal_move_generator.cpp
    ${REPO_ROOT}/src/ai/simple_ai.cpp
    ${REPO_ROOT}/src/ai/standard_ai.cpp
    ${REPO_ROOT}/src/ai/strategic_search_evaluator.cpp)
target_link_libraries(feichuan-online-server PRIVATE
    fpdz_online_rules Qt6::Core Qt6::Network Qt6::Sql Qt6::WebSockets
    PkgConfig::SODIUM)
