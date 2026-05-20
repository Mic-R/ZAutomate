cmake_minimum_required(VERSION 3.16)
project(ZAutomateCpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(CURL REQUIRED)

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
target_compile_options(zautomate_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(zautomate_core PUBLIC pthread)
target_compile_definitions(zautomate_core PRIVATE ZAUTOMATE_VERSION="${PROJECT_VERSION}")

add_library(zautomate_api
    src/database_client.cpp
    src/update_manager.cpp
)
target_include_directories(zautomate_api PUBLIC include)
target_compile_options(zautomate_api PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(zautomate_api PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)

add_executable(zautomate src/main.cpp)
target_link_libraries(zautomate PRIVATE zautomate_core zautomate_api)
target_compile_definitions(zautomate PRIVATE ZAUTOMATE_VERSION="${PROJECT_VERSION}")

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
    RUNTIME DESTINATION bin
)

set(CPACK_PACKAGE_NAME "zautomate")
set(CPACK_PACKAGE_VENDOR "Michael Reimchen")
set(CPACK_PACKAGE_CONTACT "Michael Reimchen")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Unified MIE ZAutomate C++")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Michael Reimchen")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()

include(CPack)
