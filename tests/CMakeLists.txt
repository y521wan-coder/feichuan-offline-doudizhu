# tests/CMakeLists.txt - Test configuration

find_package(Qt6 REQUIRED COMPONENTS Test)

# Derive Qt6 bin directory for DLL deployment
get_filename_component(_qt6_root "${Qt6_DIR}/../../.." ABSOLUTE)
set(_qt6_bin "${_qt6_root}/bin")

# Helper function for creating test targets
function(fpdz_add_test TEST_NAME)
    set(options NO_DEFAULT_TEST)
    set(oneValueArgs "")
    set(multiValueArgs SOURCES LIBS)
    cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME} PRIVATE
        ${TEST_LIBS}
        Qt6::Core
        Qt6::Test
    )
    target_include_directories(${TEST_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src")
    if(NOT TEST_NO_DEFAULT_TEST)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endif()

    # Copy Qt DLLs next to test executable so it can run without PATH changes
    add_custom_command(TARGET ${TEST_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt6_bin}/Qt6Core$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:${TEST_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt6_bin}/Qt6Test$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:${TEST_NAME}>"
        COMMENT "Deploying Qt DLLs for ${TEST_NAME}"
    )
endfunction()

# Core model tests
fpdz_add_test(test_app_settings
    SOURCES app/test_app_settings.cpp
    LIBS fpdz_app
)

fpdz_add_test(test_update_service
    SOURCES updater/test_update_service.cpp
            ${CMAKE_SOURCE_DIR}/apps/updater/update_service.cpp
            ${CMAKE_SOURCE_DIR}/apps/updater/signature_verifier.cpp
    LIBS Qt6::Widgets Qt6::Network crypt32 bcrypt
)
target_include_directories(test_update_service PRIVATE
    "${CMAKE_SOURCE_DIR}/apps/updater"
)
target_compile_definitions(test_update_service PRIVATE
    FPDZ_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

fpdz_add_test(test_card
    SOURCES core/test_card.cpp
    LIBS fpdz_core
)

fpdz_add_test(test_card_text_formatter
    SOURCES core/test_card_text_formatter.cpp
    LIBS fpdz_core
)

fpdz_add_test(test_card_pattern_sound_plan
    SOURCES core/test_card_pattern_sound_plan.cpp
    LIBS fpdz_core
)
target_compile_definitions(test_card_pattern_sound_plan PRIVATE
    FPDZ_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

fpdz_add_test(test_deck
    SOURCES core/test_deck.cpp
    LIBS fpdz_core
)

fpdz_add_test(test_hand
    SOURCES core/test_hand.cpp
    LIBS fpdz_core
)

# Rules tests
fpdz_add_test(test_pattern_analyzer
    SOURCES rules/test_pattern_analyzer.cpp
    LIBS fpdz_core
)

fpdz_add_test(test_pattern_comparator
    SOURCES rules/test_pattern_comparator.cpp
    LIBS fpdz_core
)

fpdz_add_test(test_scoring_engine
    SOURCES rules/test_scoring_engine.cpp
    LIBS fpdz_core
)

# Engine tests
fpdz_add_test(test_game_engine
    SOURCES engine/test_game_engine.cpp
    LIBS fpdz_core fpdz_ai
)

fpdz_add_test(test_engine_simulation
    NO_DEFAULT_TEST
    SOURCES simulation/test_engine_simulation.cpp
    LIBS fpdz_core fpdz_ai
)
add_test(NAME test_engine_simulation_seeds_1_250
    COMMAND test_engine_simulation testSeeds1To250)
add_test(NAME test_engine_simulation_seeds_251_500
    COMMAND test_engine_simulation testSeeds251To500)
add_test(NAME test_engine_simulation_three_player_seeds_1_50
    COMMAND test_engine_simulation testThreePlayerSeeds1To50)
add_test(NAME test_engine_simulation_two_player_seeds_1_50
    COMMAND test_engine_simulation testTwoPlayerSeeds1To50)
set_tests_properties(
    test_engine_simulation_seeds_1_250
    test_engine_simulation_seeds_251_500
    test_engine_simulation_three_player_seeds_1_50
    test_engine_simulation_two_player_seeds_1_50
    PROPERTIES TIMEOUT 300
)

fpdz_add_test(test_ai_battle_persistence
    SOURCES persistence/test_ai_battle_persistence.cpp
    LIBS fpdz_persistence fpdz_ai
)

add_executable(ai_service_test_host
    ${CMAKE_SOURCE_DIR}/apps/ai_service/main.cpp
    ${CMAKE_SOURCE_DIR}/apps/ai_service/ai_service.cpp
    ${CMAKE_SOURCE_DIR}/apps/ai_service/ai_service.h
    ${CMAKE_SOURCE_DIR}/apps/ai_service/credential_store.cpp
    ${CMAKE_SOURCE_DIR}/apps/ai_service/credential_store.h
)
target_include_directories(ai_service_test_host PRIVATE
    "${CMAKE_SOURCE_DIR}/apps/ai_service"
)
target_compile_definitions(ai_service_test_host PRIVATE FPDZ_AI_SERVICE_TEST_HOOKS=1)
target_link_libraries(ai_service_test_host PRIVATE Qt6::Core Qt6::Network crypt32)
set_target_properties(ai_service_test_host PROPERTIES AUTOMOC ON)

fpdz_add_test(test_ai_service
    SOURCES ai_service/test_ai_service.cpp
            ${CMAKE_SOURCE_DIR}/apps/ai_service/credential_store.cpp
    LIBS Qt6::Network crypt32
)
add_dependencies(test_ai_service ai_service_test_host FeichuanDoudizhuAiService)
target_include_directories(test_ai_service PRIVATE
    "${CMAKE_SOURCE_DIR}/apps/ai_service"
)

# AI tests
fpdz_add_test(test_ai
    SOURCES ai/test_ai.cpp
    LIBS fpdz_core fpdz_ai
)
fpdz_add_test(test_farmer_cooperation
    SOURCES ai_scenarios/test_farmer_cooperation.cpp
    LIBS fpdz_core fpdz_ai
)
fpdz_add_test(test_ai_strength
    NO_DEFAULT_TEST
    SOURCES ai/test_ai_strength.cpp
    LIBS fpdz_core fpdz_ai
)
add_executable(ai_validation_runner ai/ai_validation_runner.cpp)
target_link_libraries(ai_validation_runner PRIVATE fpdz_core fpdz_ai Qt6::Core)
target_include_directories(ai_validation_runner PRIVATE "${CMAKE_SOURCE_DIR}/src")

# Manual real-provider runner. Never registered with CTest because it spends
# real API quota; it is only invoked by an operator after explicit authorization.
add_executable(ai_real_model_runner ai/ai_real_model_runner.cpp)
target_link_libraries(ai_real_model_runner PRIVATE fpdz_app fpdz_ai fpdz_core Qt6::Core)
target_include_directories(ai_real_model_runner PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_target_properties(ai_real_model_runner PROPERTIES AUTOMOC ON)
set_tests_properties(test_ai PROPERTIES
    TIMEOUT 900
    ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=900000"
)

add_executable(test_main_window_shortcuts ui/test_main_window_shortcuts.cpp)
target_link_libraries(test_main_window_shortcuts PRIVATE
    fpdz_ui
    fpdz_app
    fpdz_core
    fpdz_ai
    fpdz_persistence
    fpdz_accessibility
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    winmm.lib
)
target_include_directories(test_main_window_shortcuts PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_target_properties(test_main_window_shortcuts PROPERTIES AUTOMOC ON)
add_test(NAME test_main_window_shortcuts COMMAND test_main_window_shortcuts)
set_tests_properties(test_main_window_shortcuts PROPERTIES
    ENVIRONMENT "FPDZ_DISABLE_SCREEN_READER_APIS=1"
)

add_executable(test_sound_management ui/test_sound_management.cpp)
target_link_libraries(test_sound_management PRIVATE
    fpdz_ui fpdz_app fpdz_core fpdz_persistence
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test winmm.lib
)
target_include_directories(test_sound_management PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(test_sound_management PRIVATE FPDZ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(test_sound_management PROPERTIES AUTOMOC ON)
add_test(NAME test_sound_management COMMAND test_sound_management)
add_custom_command(TARGET test_sound_management POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_bin}/Qt6Core$<$<CONFIG:Debug>:d>.dll"
        "${_qt6_bin}/Qt6Gui$<$<CONFIG:Debug>:d>.dll"
        "${_qt6_bin}/Qt6Widgets$<$<CONFIG:Debug>:d>.dll"
        "${_qt6_bin}/Qt6Test$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:test_sound_management>"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:test_sound_management>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_root}/plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:test_sound_management>/platforms"
)
add_custom_command(TARGET test_main_window_shortcuts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_bin}/Qt6Core$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:test_main_window_shortcuts>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_bin}/Qt6Gui$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:test_main_window_shortcuts>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_bin}/Qt6Widgets$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:test_main_window_shortcuts>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_bin}/Qt6Test$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:test_main_window_shortcuts>"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:test_main_window_shortcuts>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qt6_root}/plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:test_main_window_shortcuts>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/sounds"
        "$<TARGET_FILE_DIR:test_main_window_shortcuts>/resources/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/docs"
        "$<TARGET_FILE_DIR:test_main_window_shortcuts>/resources/docs"
    COMMENT "Deploying Qt Widgets DLLs for test_main_window_shortcuts"
)
