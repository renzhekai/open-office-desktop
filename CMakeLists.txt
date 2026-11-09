cmake_minimum_required(VERSION 3.24)
project(OpenOfficeNative VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

option(OPEN_OFFICE_WITH_ONLYOFFICE "Build the real ONLYOFFICE native adapter" OFF)
set(ONLYOFFICE_SOURCE_ROOT "${CMAKE_SOURCE_DIR}/upstream/DesktopEditors" CACHE PATH "Pinned upstream source")
set(ONLYOFFICE_RUNTIME_ROOT "${CMAKE_SOURCE_DIR}/dependencies/onlyoffice-v10.0.0/runtime" CACHE PATH "Verified official native runtime")
set(ONLYOFFICE_NATIVE_IMPORT_DIR "${CMAKE_SOURCE_DIR}/build/native-qt-wrapper-imports" CACHE PATH "Import libraries generated from verified DLL exports")
set(ONLYOFFICE_CORE_IMPORT_LIBRARY "" CACHE FILEPATH "Explicit locally built ascdocumentscore import library; empty preserves the existing import layout")
set(ONLYOFFICE_QT_WRAPPER_BUILD "${CMAKE_SOURCE_DIR}/build/native-onlyoffice-qt6" CACHE PATH "Locally built Qt6 wrapper directory")

set(office_adapter_source app/engine/onlyoffice_engine_adapter.cpp)
if(OPEN_OFFICE_WITH_ONLYOFFICE)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "The native ONLYOFFICE build requires Windows x64 MSVC.")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The verified native runtime requires x64.")
    endif()
    set(_asc_import "${ONLYOFFICE_NATIVE_IMPORT_DIR}/ascdocumentscore/ascdocumentscore.lib")
    if(ONLYOFFICE_CORE_IMPORT_LIBRARY)
        set(_asc_import "${ONLYOFFICE_CORE_IMPORT_LIBRARY}")
    endif()
    set(_qt_import "${ONLYOFFICE_QT_WRAPPER_BUILD}/lib/openoffice_qt6_documentscore.lib")
    foreach(_required IN ITEMS "${_asc_import}" "${_qt_import}"
            "${ONLYOFFICE_RUNTIME_ROOT}/ascdocumentscore.dll"
            "${ONLYOFFICE_RUNTIME_ROOT}/editors_helper.exe"
            "${ONLYOFFICE_RUNTIME_ROOT}/converter/x2t.exe"
            "${ONLYOFFICE_QT_WRAPPER_BUILD}/bin/openoffice_qt6_documentscore.dll"
            "${ONLYOFFICE_QT_WRAPPER_BUILD}/bin/openoffice_qt6_videoplayer.dll")
        if(NOT EXISTS "${_required}")
            message(FATAL_ERROR "Required real native build input is missing: ${_required}")
        endif()
    endforeach()
    add_library(onlyoffice_core SHARED IMPORTED)
    set_target_properties(onlyoffice_core PROPERTIES
        IMPORTED_IMPLIB "${_asc_import}"
        IMPORTED_LOCATION "${ONLYOFFICE_RUNTIME_ROOT}/ascdocumentscore.dll")
    add_library(onlyoffice_qt6 SHARED IMPORTED)
    set_target_properties(onlyoffice_qt6 PROPERTIES
        IMPORTED_IMPLIB "${_qt_import}"
        IMPORTED_LOCATION "${ONLYOFFICE_QT_WRAPPER_BUILD}/bin/openoffice_qt6_documentscore.dll")
    set(office_adapter_source app/engine/onlyoffice_engine_adapter_native.cpp)
endif()

add_library(office_engine STATIC
    app/engine/office_engine.h
    app/engine/office_engine.cpp
    app/engine/onlyoffice_engine_adapter.h
    ${office_adapter_source})
target_include_directories(office_engine PUBLIC app)
target_link_libraries(office_engine PUBLIC Qt6::Widgets)
if(OPEN_OFFICE_WITH_ONLYOFFICE)
    target_compile_definitions(office_engine PUBLIC OPEN_OFFICE_WITH_ONLYOFFICE=1)
    target_compile_definitions(office_engine PRIVATE WIN32 WIN64 NOMINMAX)
    target_include_directories(office_engine PRIVATE
        "${ONLYOFFICE_SOURCE_ROOT}/core/Common"
        "${ONLYOFFICE_SOURCE_ROOT}/desktop-sdk/ChromiumBasedEditors/lib/include"
        "${ONLYOFFICE_SOURCE_ROOT}/desktop-sdk/ChromiumBasedEditors/lib/qt_wrapper/include")
    target_link_libraries(office_engine PRIVATE onlyoffice_core onlyoffice_qt6)
endif()

find_package(Qt6 REQUIRED COMPONENTS Network)
add_library(office_assistant STATIC app/assistant/assistant_commands.h app/assistant/assistant_commands.cpp
    app/assistant/ollama_client.h app/assistant/ollama_client.cpp
    app/assistant/local_assistant.h app/assistant/local_assistant.cpp
    app/assistant/markdown_html.h app/assistant/markdown_html.cpp
    app/assistant/assistant_tasks.h app/assistant/assistant_tasks.cpp
    app/assistant/chat_client.h app/assistant/engine_client.h app/assistant/engine_client.cpp
    app/assistant/local_engine.h app/assistant/local_engine.cpp
    app/assistant/model_catalog.h app/assistant/model_catalog.cpp
    app/assistant/model_downloader.h app/assistant/model_downloader.cpp)
target_include_directories(office_assistant PUBLIC app)
target_link_libraries(office_assistant PUBLIC Qt6::Core Qt6::Network)
if(WIN32)
    target_link_libraries(office_assistant PRIVATE dxgi)
endif()

add_library(office_ui STATIC app/ui/main_window.h app/ui/main_window.cpp
    app/ui/assistant_art.h app/ui/assistant_art.cpp app/ui/assistant_ball.h app/ui/assistant_ball.cpp
    app/ui/ai_settings_dialog.h app/ui/ai_settings_dialog.cpp
    app/ui/relocation.h app/ui/relocation.cpp)
target_include_directories(office_ui PUBLIC app)
target_link_libraries(office_ui PUBLIC office_engine office_assistant Qt6::Widgets)

add_executable(OpenOffice WIN32 app/main.cpp)
target_link_libraries(OpenOffice PRIVATE office_ui)
target_compile_definitions(OpenOffice PRIVATE OPEN_OFFICE_VERSION="${PROJECT_VERSION}")

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(engine_contract_test tests/native/engine_contract_test.cpp)
    target_link_libraries(engine_contract_test PRIVATE office_engine Qt6::Test)
    add_test(NAME engine_contract COMMAND engine_contract_test -o engine-contract-results.txt,txt)
    add_executable(native_ui_test tests/native/native_ui_test.cpp)
    target_link_libraries(native_ui_test PRIVATE office_ui Qt6::Test)
    target_compile_definitions(native_ui_test PRIVATE PROJECT_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures/generated")
    add_test(NAME native_ui COMMAND native_ui_test -o native-ui-results.txt,txt)
    add_executable(assistant_test tests/native/assistant_test.cpp)
    target_link_libraries(assistant_test PRIVATE office_assistant Qt6::Test)
    add_test(NAME assistant COMMAND assistant_test -o assistant-results.txt,txt)
    # Manual end-to-end check of the built-in AI with a real engine and model; not run by CTest.
    add_executable(local_ai_smoke tests/manual/local_ai_smoke.cpp)
    target_link_libraries(local_ai_smoke PRIVATE office_assistant)
    set_tests_properties(engine_contract native_ui assistant PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
