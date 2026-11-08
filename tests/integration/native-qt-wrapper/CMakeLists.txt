cmake_minimum_required(VERSION 3.24)
project(OpenOfficeNativeQt6Wrappers LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This integration build requires Windows x64 MSVC.")
endif()
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Use Release to match the fixed native engine binaries.")
endif()

get_filename_component(_workspace_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ONLYOFFICE_SOURCE_ROOT "${_workspace_root}/upstream/DesktopEditors"
    CACHE PATH "Existing fixed ONLYOFFICE source checkout")
set(ONLYOFFICE_NATIVE_IMPORT_DIR "" CACHE PATH
    "Directory of real kernel.lib, graphics.lib and ascdocumentscore.lib; empty builds objects only")
option(ONLYOFFICE_VIDEO_ONLY "Link only the video DLL while the native document core is still building" OFF)
set(_sdk "${ONLYOFFICE_SOURCE_ROOT}/desktop-sdk/ChromiumBasedEditors")
set(_video "${_sdk}/videoplayerlib")
set(_wrapper "${_sdk}/lib/qt_wrapper")

find_package(Qt6 6.11 CONFIG REQUIRED COMPONENTS
    Core Gui Widgets PrintSupport Multimedia MultimediaWidgets)

# Lists and definitions mirror the reviewed fixed upstream .pro/.pri files.
# We do not execute qmake/base.pri or alter upstream source/build directories.
set(_video_sources
    "${_video}/src/lib_base.cpp"
    "${_video}/src/qiconpushbutton.cpp"
    "${_video}/src/qtimelabel.cpp"
    "${_video}/src/qfooterpanel.cpp"
    "${_video}/src/qascvideoview.cpp"
    "${_video}/src/qvideoslider.cpp"
    "${_video}/src/qvideoplaylist.cpp"
    "${_video}/src/qascvideowidget.cpp"
    "${_video}/src/qwidgetutils.cpp"
    "${_video}/src/style/skins_footer.cpp")
set(_video_headers
    "${_video}/src/lib_base.h"
    "${_video}/src/style/style_options.h"
    "${_video}/src/videoplayerlib_export.h"
    "${_video}/src/qiconpushbutton.h"
    "${_video}/src/qtimelabel.h"
    "${_video}/src/qfooterpanel_private.h"
    "${_video}/src/qvideoslider.h"
    "${_video}/src/qvideoplaylist.h"
    "${_video}/src/qascvideowidget.h"
    "${_video}/src/qmultimedia.h"
    "${_video}/src/style/skins_footer.h"
    "${_video}/src/qascmediaplayer.h"
    "${_video}/qascvideoview.h"
    "${_video}/qfooterpanel.h"
    "${_video}/qwidgetutils.h")
set(_wrapper_sources
    "${_wrapper}/src/qcefview.cpp"
    "${_wrapper}/src/qcefview_media.cpp"
    "${_wrapper}/src/qascprinter.cpp"
    "${_wrapper}/qrenderer/src/qrenderer.cpp"
    "${_wrapper}/src/qexternalmessageloop.cpp"
    "${_wrapper}/src/qascapplicationmanager.cpp"
    "${_wrapper}/src/qdpichecker.cpp")
set(_wrapper_headers
    "${_wrapper}/include/qcefview.h"
    "${_wrapper}/include/qcefview_media.h"
    "${_wrapper}/include/qascprinter.h"
    "${_wrapper}/qrenderer/include/qrenderer.h"
    "${_wrapper}/include/qexternalmessageloop.h"
    "${_wrapper}/include/qascapplicationmanager.h"
    "${_wrapper}/include/qdpichecker.h")

foreach(_input IN LISTS _video_sources _video_headers _wrapper_sources _wrapper_headers)
    if(NOT EXISTS "${_input}")
        message(FATAL_ERROR "Required real upstream source is missing: ${_input}")
    endif()
endforeach()

add_library(videoplayer_objects OBJECT ${_video_sources} ${_video_headers}
    "${_video}/IconsVideoPlayerLib.qrc")
# CMake's source scan sees a Q_OBJECT in an inactive USE_VLC_LIBRARY_VIDEO
# branch of this .cpp. The selected Qt Multimedia path has no local QObject
# there. Its real public header remains listed above and is processed by MOC.
set_source_files_properties("${_video}/src/qascvideoview.cpp" PROPERTIES SKIP_AUTOMOC ON)
add_library(qtascdocumentscore_objects OBJECT ${_wrapper_sources} ${_wrapper_headers})

set(_qt_libraries Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport
    Qt6::Multimedia Qt6::MultimediaWidgets)
foreach(_target IN ITEMS videoplayer_objects qtascdocumentscore_objects)
    set_target_properties(${_target} PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
        AUTOMOC ON AUTORCC ON AUTOUIC OFF
        MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    target_include_directories(${_target} PRIVATE
        "${_sdk}/lib/include"
        "${_wrapper}/include"
        "${_video}"
        "${ONLYOFFICE_SOURCE_ROOT}/core/DesktopEditor"
        "${ONLYOFFICE_SOURCE_ROOT}/core/Common")
    target_compile_definitions(${_target} PRIVATE
        WIN32 WIN64 NOMINMAX WINDOWS_IGNORE_PACKING_MISMATCH QT_VERSION_6)
    target_compile_options(${_target} PRIVATE /utf-8 /Zc:strictStrings-)
    target_link_libraries(${_target} PRIVATE ${_qt_libraries})
endforeach()
target_compile_definitions(videoplayer_objects PRIVATE
    BUILD_VIDEO_LIBRARY QT_DEPRECATED_WARNINGS)
target_compile_definitions(qtascdocumentscore_objects PRIVATE
    DESKTOP_USE_DYNAMIC_LIBRARY_BUILDING)
# A localized compatibility preinclude for the pinned legacy AGG header. The
# actual upstream files and declarations are used unchanged; only its obsolete
# storage hint is removed while this one header is parsed in these two TUs.
set_source_files_properties(
    "${_wrapper}/src/qascprinter.cpp"
    "${_wrapper}/qrenderer/src/qrenderer.cpp"
    PROPERTIES COMPILE_OPTIONS "/FI${CMAKE_CURRENT_LIST_DIR}/qt6_legacy_agg_compat.h")

# DLL linkage is enabled only after real native import libraries exist.
# No dummy implementations, fake import symbols or link-error suppression.
if(ONLYOFFICE_NATIVE_IMPORT_DIR)
    set(_native_libraries kernel)
    if(NOT ONLYOFFICE_VIDEO_ONLY)
        list(APPEND _native_libraries graphics ascdocumentscore)
    endif()
    foreach(_library IN LISTS _native_libraries)
        set(_import_path "${ONLYOFFICE_NATIVE_IMPORT_DIR}/${_library}.lib")
        if(NOT EXISTS "${_import_path}")
            set(_import_path "${ONLYOFFICE_NATIVE_IMPORT_DIR}/${_library}/${_library}.lib")
        endif()
        if(NOT EXISTS "${_import_path}")
            message(FATAL_ERROR "Missing real import library: ${_import_path}")
        endif()
        add_library(native_${_library} UNKNOWN IMPORTED)
        set_target_properties(native_${_library} PROPERTIES IMPORTED_LOCATION "${_import_path}")
    endforeach()

    add_library(openoffice_qt6_videoplayer SHARED $<TARGET_OBJECTS:videoplayer_objects>)
    target_link_libraries(openoffice_qt6_videoplayer PRIVATE ${_qt_libraries} native_kernel)
    set(_dll_targets openoffice_qt6_videoplayer)
    if(NOT ONLYOFFICE_VIDEO_ONLY)
        add_library(openoffice_qt6_documentscore SHARED $<TARGET_OBJECTS:qtascdocumentscore_objects>)
        target_link_libraries(openoffice_qt6_documentscore PRIVATE
            ${_qt_libraries} native_kernel native_graphics native_ascdocumentscore
            openoffice_qt6_videoplayer user32)
        list(APPEND _dll_targets openoffice_qt6_documentscore)
    endif()
    foreach(_target IN LISTS _dll_targets)
        set_target_properties(${_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endforeach()
    message(STATUS "Real native import directory: ${ONLYOFFICE_NATIVE_IMPORT_DIR}")
else()
    message(STATUS "Object compilation only; DLL targets require ONLYOFFICE_NATIVE_IMPORT_DIR.")
endif()
message(STATUS "Upstream sources: ${ONLYOFFICE_SOURCE_ROOT}; Qt: ${Qt6_VERSION}")
