add_executable(FeichuanOnlineDoudizhu
    main.cpp
    online_window.cpp
    online_window.h
    online_storage.cpp
    online_storage.h
)
if(WIN32)
    set(FPDZ_FILE_DESCRIPTION "飞船斗地主在线真人版")
    set(FPDZ_INTERNAL_NAME "FeichuanOnlineDoudizhu")
    set(FPDZ_ORIGINAL_FILENAME "飞船斗地主在线版.exe")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/windows_version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/online_version.rc" @ONLY)
    target_sources(FeichuanOnlineDoudizhu PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/online_version.rc")
endif()

if(WIN32)
    enable_testing()
    add_executable(test_online_storage test_online_storage.cpp online_storage.cpp online_storage.h)
    target_link_libraries(test_online_storage PRIVATE Qt6::Core Qt6::Test crypt32)
    set_target_properties(test_online_storage PROPERTIES AUTOMOC ON)
    add_test(NAME test_online_storage COMMAND test_online_storage)
    get_filename_component(_online_test_qt_root "${Qt6_DIR}/../../.." ABSOLUTE)
    add_custom_command(TARGET test_online_storage POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_online_test_qt_root}/bin/Qt6Core$<$<CONFIG:Debug>:d>.dll"
            "${_online_test_qt_root}/bin/Qt6Test$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:test_online_storage>"
    )
endif()

target_include_directories(FeichuanOnlineDoudizhu PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(FeichuanOnlineDoudizhu PRIVATE
    fpdz_core fpdz_accessibility fpdz_ui
    Qt6::Core Qt6::Widgets Qt6::Network Qt6::WebSockets
    crypt32 winmm
)
target_compile_definitions(FeichuanOnlineDoudizhu PRIVATE FPDZ_APP_VERSION="${FPDZ_VERSION}")
set_target_properties(FeichuanOnlineDoudizhu PROPERTIES
    OUTPUT_NAME "飞船斗地主在线版"
    WIN32_EXECUTABLE $<CONFIG:Release>
    AUTOMOC ON
)
fpdz_apply_warnings(FeichuanOnlineDoudizhu)

if(WIN32)
    get_filename_component(_online_qt_root "${Qt6_DIR}/../../.." ABSOLUTE)
    add_custom_command(TARGET FeichuanOnlineDoudizhu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_online_qt_root}/bin/Qt6Core$<$<CONFIG:Debug>:d>.dll"
            "${_online_qt_root}/bin/Qt6Gui$<$<CONFIG:Debug>:d>.dll"
            "${_online_qt_root}/bin/Qt6Widgets$<$<CONFIG:Debug>:d>.dll"
            "${_online_qt_root}/bin/Qt6Network$<$<CONFIG:Debug>:d>.dll"
            "${_online_qt_root}/bin/Qt6WebSockets$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:FeichuanOnlineDoudizhu>"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:FeichuanOnlineDoudizhu>/tls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_online_qt_root}/plugins/tls/qschannelbackend$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:FeichuanOnlineDoudizhu>/tls"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:FeichuanOnlineDoudizhu>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_online_qt_root}/plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:FeichuanOnlineDoudizhu>/platforms"
        COMMENT "Deploying isolated online client's Qt runtime"
    )
endif()
