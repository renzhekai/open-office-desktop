cmake_minimum_required(VERSION 3.24)
project(OnlyOfficeNativeApiProbe LANGUAGES CXX)

# This project deliberately stops at an object file. It is not a runnable host,
# a native-library link check, or a substitute for an editing integration test.
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Use the Windows x64 MSVC toolchain and the matching Qt MSVC kit.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native API probe requires an x64 build.")
endif()

get_filename_component(_workspace_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ONLYOFFICE_SOURCE_ROOT "${_workspace_root}/upstream/DesktopEditors"
    CACHE PATH "Existing, pinned DesktopEditors checkout; never downloaded by this project")
set(_native_lib "${ONLYOFFICE_SOURCE_ROOT}/desktop-sdk/ChromiumBasedEditors/lib")

foreach(_header IN ITEMS
        "${_native_lib}/include/applicationmanager.h"
        "${_native_lib}/include/applicationmanager_events.h"
        "${_native_lib}/include/cefapplication.h"
        "${_native_lib}/include/cefview.h"
        "${_native_lib}/qt_wrapper/include/qascapplicationmanager.h"
        "${_native_lib}/qt_wrapper/include/qcefview.h"
        "${ONLYOFFICE_SOURCE_ROOT}/core/DesktopEditor/common/base_export.h")
    if(NOT EXISTS "${_header}")
        message(FATAL_ERROR "Required real upstream header is missing: ${_header}")
    endif()
endforeach()

find_package(Qt6 6.11 CONFIG REQUIRED COMPONENTS Widgets)

add_library(onlyoffice_native_api_probe OBJECT native_api_probe.cpp)
set_target_properties(onlyoffice_native_api_probe PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF)
target_include_directories(onlyoffice_native_api_probe PRIVATE
    "${_native_lib}/include"
    "${_native_lib}/qt_wrapper/include")
# Mirror core/Common/base.pri's Windows consumer defines. _WIN32/_WIN64 are
# provided by MSVC. Do not define the upstream DLL-building or no-DLL switches.
target_compile_definitions(onlyoffice_native_api_probe PRIVATE WIN32 WIN64 NOMINMAX)
target_compile_options(onlyoffice_native_api_probe PRIVATE /utf-8 /W4)
# An OBJECT target uses Qt's include/compile properties without a link step.
target_link_libraries(onlyoffice_native_api_probe PRIVATE Qt6::Widgets)

message(STATUS "Probe source: ${ONLYOFFICE_SOURCE_ROOT}")
message(STATUS "Probe Qt: ${Qt6_VERSION} (${Qt6_DIR})")
message(STATUS "Compile only: no native libraries, executable, downloads, or runtime tests.")
