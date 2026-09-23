# InstallRules.cmake - Installation and packaging rules

include(GNUInstallDirs)

install(TARGETS FeichuanOfflineDoudizhu FeichuanOfflineDoudizhuUpdater
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install Qt DLLs using windeployqt
find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${CMAKE_PREFIX_PATH}/bin"
)

if(WINDEPLOYQT_EXECUTABLE)
    install(CODE "
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/飞船斗地主.exe\"
        )
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/飞船斗地主更新器.exe\"
        )
    ")
endif()

# Install documentation
install(FILES
    README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

install(DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    FILES_MATCHING PATTERN "*.md"
)
