cmake_minimum_required(VERSION 3.21)
project(OpenOfficeQtSdkCheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "This check targets the approved Windows MSVC Qt kit, not MinGW.")
endif()

find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS
    Core Gui Widgets Network Svg Multimedia MultimediaWidgets)

add_executable(qt_sdk_check main.cpp)
target_link_libraries(qt_sdk_check PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg
    Qt6::Multimedia Qt6::MultimediaWidgets)
if(MSVC)
    target_compile_options(qt_sdk_check PRIVATE /W4 /utf-8)
endif()

enable_testing()
add_test(NAME qt_sdk_runtime COMMAND qt_sdk_check
    --work-dir "${CMAKE_CURRENT_BINARY_DIR}/runtime" --timeout-ms 10000)
set_tests_properties(qt_sdk_runtime PROPERTIES TIMEOUT 20)

# Do not deploy, download, install, or alter the SDK from this test project.
message(STATUS "Qt SDK check version: ${Qt6_VERSION}")
message(STATUS "Qt SDK config: ${Qt6_DIR}")
