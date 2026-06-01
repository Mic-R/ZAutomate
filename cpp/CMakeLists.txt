cmake_minimum_required(VERSION 3.16)
project(ZAutomateCpp VERSION 2.1.23 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)

include(FetchContent)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(Qt6 QUIET COMPONENTS Widgets)
find_package(Qt6 QUIET COMPONENTS Multimedia)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(zautomate_core
    src/cart_queue.cpp
    src/logger.cpp
    src/modules.cpp
)

target_include_directories(zautomate_core PUBLIC include)
if(MSVC)
    target_compile_options(zautomate_core PRIVATE /W4)
else()
    target_compile_options(zautomate_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(zautomate_core PUBLIC Threads::Threads)
target_compile_definitions(zautomate_core PRIVATE ZAUTOMATE_VERSION="${PROJECT_VERSION}")

add_library(zautomate_api
    src/database_client.cpp
    src/update_manager.cpp
)
target_include_directories(zautomate_api PUBLIC include)
if(MSVC)
    target_compile_options(zautomate_api PRIVATE /W4)
else()
    target_compile_options(zautomate_api PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(zautomate_api PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)

add_executable(zautomate_cli src/cli_main.cpp)
target_link_libraries(zautomate_cli PRIVATE zautomate_core zautomate_api)
target_compile_definitions(zautomate_cli PRIVATE ZAUTOMATE_VERSION="${PROJECT_VERSION}")

if(TARGET Qt6::Widgets)
    add_executable(zautomate
        src/main.cpp
        src/gui/main_window.cpp
        src/gui/easter_egg_dialog.cpp
        src/gui/resources.qrc
    )
    target_include_directories(zautomate PRIVATE include)
    target_link_libraries(zautomate PRIVATE zautomate_core zautomate_api Qt6::Widgets)
    if(TARGET Qt6::Multimedia)
        target_link_libraries(zautomate PRIVATE Qt6::Multimedia)
        target_compile_definitions(zautomate PRIVATE ZAUTOMATE_HAS_QT_MULTIMEDIA=1)
    endif()
    target_compile_definitions(zautomate PRIVATE ZAUTOMATE_VERSION="${PROJECT_VERSION}")
    if(UNIX AND NOT APPLE)
        set_target_properties(zautomate PROPERTIES
            INSTALL_RPATH "$ORIGIN/.."
            BUILD_RPATH "$ORIGIN/.."
        )
    endif()
    if(APPLE)
        set_target_properties(zautomate PROPERTIES MACOSX_BUNDLE TRUE)
    endif()
    if(WIN32)
        # Embed Windows icon resource if present
        if(EXISTS "${CMAKE_SOURCE_DIR}/src/gui/windows/app.rc")
            target_sources(zautomate PRIVATE src/gui/windows/app.rc)
        endif()
        # Instruct CPack/NSIS to use the application icon if provided
        if(EXISTS "${CMAKE_SOURCE_DIR}/src/gui/resources/app.ico")
            set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/src/gui/resources/app.ico")
            set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/src/gui/resources/app.ico")
        else()
            message(WARNING "CPack: app.ico not found at cpp/src/gui/resources/app.ico — skipping NSIS icon settings to avoid makensis errors.")
        endif()
        # Ensure NSIS installer creates shortcuts and installs the executable
        # Only set executable/shortcut-related CPACK variables when an app
        # icon is present to avoid generating Start Menu/Shortcut code on
        # environments where NSIS macro expansion can produce invalid tokens.
        if(EXISTS "${CMAKE_SOURCE_DIR}/src/gui/resources/app.ico")
            set(CPACK_PACKAGE_EXECUTABLES "zautomate;ZAutomate")
            set(CPACK_NSIS_DISPLAY_NAME "zautomate")
        else()
            message(STATUS "Skipping CPACK_PACKAGE_EXECUTABLES - no app.ico present")
        endif()
        # Use forward-slashes to avoid CMake escape-sequence issues (CMP0010)
        set(CPACK_NSIS_INSTALLED_ICON_NAME "bin/zautomate.exe")
    endif()

    if(UNIX AND NOT APPLE)
        find_program(QT_QMAKE_EXECUTABLE NAMES qmake6 qmake REQUIRED)
        execute_process(
            COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
            OUTPUT_VARIABLE QT_PLUGINS_SOURCE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_LIBS
            OUTPUT_VARIABLE QT_LIBS_SOURCE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        file(GLOB QT_SHARED_LIBS
            "${QT_LIBS_SOURCE_DIR}/libQt6*.so*"
        )

        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zautomate-wrapper.sh.in
            ${CMAKE_CURRENT_BINARY_DIR}/zautomate-wrapper
            @ONLY
        )
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zautomate.desktop.in
            ${CMAKE_CURRENT_BINARY_DIR}/zautomate.desktop
            @ONLY
        )

        install(TARGETS zautomate
            RUNTIME_DEPENDENCY_SET zautomate_runtime_deps
            RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/zautomate-bin
        )
        install(RUNTIME_DEPENDENCY_SET zautomate_runtime_deps
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            PRE_EXCLUDE_REGEXES
                ".*libc\\.so.*"
                ".*ld-.*\\.so.*"
            DIRECTORIES
                "${QT_LIBS_SOURCE_DIR}"
        )
        install(FILES ${QT_SHARED_LIBS}
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
        install(DIRECTORY "${QT_PLUGINS_SOURCE_DIR}/"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/plugins"
            FILES_MATCHING
                PATTERN "*.so"
                PATTERN "*.so.*"
                PATTERN "*.json"
        )
        install(CODE "
            message(STATUS \"Packaging: collecting Qt plugin runtime dependencies\")
            file(GLOB_RECURSE QT_PLUGIN_LIBS
                \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/qt6/plugins/*.so\"
                \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/qt6/plugins/*.so.*\"
            )
            if(QT_PLUGIN_LIBS)
                file(GET_RUNTIME_DEPENDENCIES
                    LIBRARIES \${QT_PLUGIN_LIBS}
                    RESOLVED_DEPENDENCIES_VAR QT_PLUGIN_DEPS
                    UNRESOLVED_DEPENDENCIES_VAR QT_PLUGIN_UNRESOLVED
                    PRE_EXCLUDE_REGEXES
                        \".*libc\\.so.*\"
                        \".*ld-.*\\.so.*\"
                )
                foreach(dep IN LISTS QT_PLUGIN_DEPS)
                    get_filename_component(dep_name \${dep} NAME)
                    set(dep_dest \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/\${dep_name}\")
                    if(NOT EXISTS \${dep_dest})
                        file(INSTALL DESTINATION \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\" TYPE SHARED_LIBRARY FILES \${dep})
                        message(STATUS \"Installed plugin dependency: \${dep_name}\")
                    endif()
                endforeach()
                if(QT_PLUGIN_UNRESOLVED)
                    message(WARNING \"Unresolved Qt plugin dependencies: \${QT_PLUGIN_UNRESOLVED}\")
                endif()
            endif()
        ")
        install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/zautomate-wrapper
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            RENAME zautomate
        )
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/zautomate.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
        )
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/app_icon.svg
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
            RENAME zautomate.svg
        )
        # Defensive cleanup: ensure forbidden system libs are not present in
        # the packaging staging area (CPack/DESTDIR). This runs at install
        # time and removes any stray libc/ld loader files that made it into
        # the install tree despite the exclusion rules.
        install(CODE "
            message(STATUS \"Packaging: removing forbidden system libs from staging area\")
            file(GLOB FORBIDDEN_LIBS \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libc.so*\" \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/ld-*.so*\")
            foreach(f IN LISTS FORBIDDEN_LIBS)
                if(EXISTS \"${f}\")
                    file(REMOVE \"${f}\")
                    message(STATUS \"Removed forbidden file: ${f}\")
                endif()
            endforeach()
        ")
    endif()
else()
    message(WARNING "Qt6 Widgets not found; building zautomate as the CLI fallback.")
    add_executable(zautomate src/cli_main.cpp)
    target_link_libraries(zautomate PRIVATE zautomate_core zautomate_api)
    target_compile_definitions(zautomate PRIVATE ZAUTOMATE_VERSION="${PROJECT_VERSION}")
endif()

if(WIN32)
    target_compile_definitions(zautomate PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(zautomate_tests tests/test_main.cpp)
target_link_libraries(zautomate_tests PRIVATE zautomate_core zautomate_api)
target_compile_definitions(zautomate_tests PRIVATE ZAUTOMATE_VERSION="${PROJECT_VERSION}")

include(CTest)
enable_testing()
add_test(NAME zautomate_tests COMMAND zautomate_tests)

if(NOT (UNIX AND NOT APPLE))
    install(TARGETS zautomate
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
endif()

set(CPACK_PACKAGE_NAME "zautomate")
set(CPACK_PACKAGE_VENDOR "Michael Reimchen")
set(CPACK_PACKAGE_CONTACT "Michael Reimchen")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Unified MIE ZAutomate C++")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")

if(APPLE)
    set(CPACK_GENERATOR "TGZ;DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Michael Reimchen")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
