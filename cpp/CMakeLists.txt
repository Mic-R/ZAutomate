cmake_minimum_required(VERSION 3.16)
project(ZAutomateCpp VERSION 2.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(FetchContent)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(Qt6 QUIET COMPONENTS Widgets)

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

if(Qt6_FOUND)
    add_executable(zautomate
        src/main.cpp
        src/gui/main_window.cpp
        src/gui/easter_egg_dialog.cpp
        src/gui/resources.qrc
    )
    target_include_directories(zautomate PRIVATE include)
    target_link_libraries(zautomate PRIVATE zautomate_core zautomate_api Qt6::Widgets)
    target_compile_definitions(zautomate PRIVATE ZAUTOMATE_VERSION="${PROJECT_VERSION}")
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

install(TARGETS zautomate
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

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
