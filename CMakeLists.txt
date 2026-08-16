cmake_minimum_required(VERSION 3.24)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" FPDZ_VERSION LIMIT_COUNT 1)
string(STRIP "${FPDZ_VERSION}" FPDZ_VERSION)

project(FourPlayerDoudizhu
    VERSION ${FPDZ_VERSION}
    DESCRIPTION "Four Player Doudizhu - Accessible Windows Desktop Game"
    LANGUAGES CXX
)

if(MSVC)
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

# Core library (no Qt dependency)
add_library(fpdz_core STATIC
    src/core/audio/card_pattern_sound_plan.cpp
    src/core/model/card.cpp
    src/core/model/deck.cpp
    src/core/model/hand.cpp
    src/core/model/player.cpp
    src/core/model/game_snapshot.cpp
    src/core/rules/rule_set.cpp
    src/core/rules/rank_histogram.cpp
    src/core/rules/card_pattern.cpp
    src/core/rules/pattern_analyzer.cpp
    src/core/rules/pattern_comparator.cpp
    src/core/rules/scoring_engine.cpp
    src/core/engine/game_state.cpp
    src/core/engine/game_engine.cpp
    src/core/engine/turn_manager.cpp
    src/core/text/card_text_formatter.cpp
    src/core/text/game_text_formatter.cpp
)
target_include_directories(fpdz_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fpdz_core PRIVATE Qt6::Core)
if(WIN32)
    target_link_libraries(fpdz_core PRIVATE bcrypt.lib)
endif()
fpdz_apply_warnings(fpdz_core)

# AI library
add_library(fpdz_ai STATIC
    src/ai/heuristic_model.cpp
    src/ai/legal_move_generator.cpp
    src/ai/bidding_strategy.cpp
    src/ai/simple_ai.cpp
    src/ai/standard_ai.cpp
    src/ai/hint_service.cpp
)
target_include_directories(fpdz_ai PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fpdz_ai PUBLIC fpdz_core PRIVATE Qt6::Core)
set_target_properties(fpdz_ai PROPERTIES AUTOMOC ON)
fpdz_apply_warnings(fpdz_ai)

# Persistence library
add_library(fpdz_persistence STATIC
    src/persistence/data_paths.cpp
    src/persistence/settings_repository.cpp
    src/persistence/save_repository.cpp
    src/persistence/statistics_repository.cpp
    src/persistence/replay_repository.cpp
    src/persistence/log_service.cpp
    src/persistence/diagnostic_trace_service.cpp
)
set_target_properties(fpdz_persistence PROPERTIES AUTOMOC ON)
target_include_directories(fpdz_persistence PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fpdz_persistence PUBLIC fpdz_core PRIVATE Qt6::Core)
fpdz_apply_warnings(fpdz_persistence)

# Accessibility library
add_library(fpdz_accessibility STATIC
    src/accessibility/accessibility_service.cpp
    src/accessibility/announcement_scheduler.cpp
    src/accessibility/accessibility_text.cpp
)
target_include_directories(fpdz_accessibility PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fpdz_accessibility PUBLIC fpdz_core PRIVATE Qt6::Core Qt6::Widgets)
fpdz_apply_warnings(fpdz_accessibility)

# Application library
add_library(fpdz_app STATIC
    src/app/application.cpp
    src/app/service_registry.cpp
    src/app/app_settings.cpp
    src/app/update_service.cpp
)
target_include_directories(fpdz_app PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fpdz_app PUBLIC
    fpdz_core fpdz_ai fpdz_persistence fpdz_accessibility
    PRIVATE Qt6::Core Qt6::Widgets Qt6::Network
)
target_compile_definitions(fpdz_app PRIVATE FPDZ_APP_VERSION="${FPDZ_VERSION}")
fpdz_apply_warnings(fpdz_app)

# UI library
add_library(fpdz_ui STATIC
    src/ui/main_window.cpp
    src/ui/models/hand_list_model.cpp
    src/ui/models/player_status_model.cpp
    src/ui/widgets/hand_view.cpp
    src/ui/widgets/game_status_widget.h
    src/ui/widgets/game_status_widget.cpp
    src/ui/widgets/player_status_widget.cpp
    src/ui/dialogs/settings_dialog.cpp
    src/ui/dialogs/sound_manager_dialog.cpp
    src/ui/dialogs/shortcut_dialog.cpp
    src/ui/dialogs/result_dialog.cpp
    src/ui/dialogs/about_dialog.cpp
    src/ui/sound_service.cpp
    src/ui/sound_catalog.cpp
    src/ui/sound_override_manager.cpp
)
target_include_directories(fpdz_ui PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fpdz_ui PUBLIC
    fpdz_core fpdz_ai fpdz_accessibility fpdz_app
    PRIVATE Qt6::Core Qt6::Widgets Qt6::Network winmm.lib
)
set_target_properties(fpdz_ui PROPERTIES AUTOMOC ON AUTORCC ON)
fpdz_apply_warnings(fpdz_ui)

# Main executable
add_executable(FourPlayerDoudizhu
    src/main.cpp
    resources/app.qrc
)
target_link_libraries(FourPlayerDoudizhu PRIVATE
    fpdz_app fpdz_ui fpdz_core fpdz_ai fpdz_persistence fpdz_accessibility
    Qt6::Core Qt6::Widgets Qt6::Network winmm.lib
)
set_target_properties(FourPlayerDoudizhu PROPERTIES
    WIN32_EXECUTABLE $<CONFIG:Release>
    AUTOMOC ON
    AUTORCC ON
)
fpdz_apply_warnings(FourPlayerDoudizhu)

# Deploy Qt DLLs alongside the executable after build
get_filename_component(_qt6_root "${Qt6_DIR}/../../.." ABSOLUTE)
set(_qt6_bin "${_qt6_root}/bin")
set(_qt6_plugins "${_qt6_root}/plugins")
add_custom_command(TARGET FourPlayerDoudizhu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_bin}/Qt6Core$<$<CONFIG:Debug>:d>.dll"
        "${_qt6_bin}/Qt6Gui$<$<CONFIG:Debug>:d>.dll"
        "${_qt6_bin}/Qt6Widgets$<$<CONFIG:Debug>:d>.dll"
        "${_qt6_bin}/Qt6Network$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_plugins}/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/sounds"
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>/resources/sounds"
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>/nvdaControllerClient64.dll"
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>/licenses/NVDA-Controller-Client-LGPL-2.1.txt"
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>/Qt6Sql.dll"
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>/Qt6Concurrent.dll"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:FourPlayerDoudizhu>/sqldrivers"
    COMMENT "Deploying Qt DLLs for FourPlayerDoudizhu"
)
# Testing
enable_testing()
add_subdirectory(tests)

# Install rules
include(InstallRules)

