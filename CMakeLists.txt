cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(lis_workbench LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Enable whole-program optimization and link-time code generation for Release builds.
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

if(MSVC AND LIS_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MSVC defaults to system locale (e.g. GBK/936) for source encoding.
# Force UTF-8 so Chinese string literals survive compilation.
if(MSVC)
    add_compile_options(/utf-8 /MP /W3 /wd4996)
endif()

if(WIN32)
    set(LIS_WIN7_COMPAT_DEFINITIONS
        WINVER=0x0601
        _WIN32_WINNT=0x0601
        NTDDI_VERSION=0x06010000
    )
endif()

add_library(sqlite3_amalgamation STATIC
    external/sqlite/sqlite3.c
)
target_include_directories(sqlite3_amalgamation SYSTEM PUBLIC external/sqlite)
target_compile_definitions(sqlite3_amalgamation PRIVATE
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=1
    ${LIS_WIN7_COMPAT_DEFINITIONS}
)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sqlite3_amalgamation PRIVATE -Wno-stringop-overread)
endif()

# ── Core library ──────────────────────────────────────────────
# Platform-free business logic. Reusable across Win32 and Qt frontends.
add_library(search_core STATIC
    src/search_core.cpp
    src/search_app.cpp
    src/search_controller.cpp
    src/search_text.cpp
    src/app_settings.cpp
    src/trend_core.cpp
    src/log.cpp
)
target_include_directories(search_core PUBLIC src)
target_compile_definitions(search_core PUBLIC UNICODE _UNICODE ${LIS_WIN7_COMPAT_DEFINITIONS})

if(WIN32)
    target_link_libraries(search_core PUBLIC odbc32)
endif()

add_library(update_core STATIC
    src/update_manifest.cpp
    src/update_source.cpp
)
target_include_directories(update_core PUBLIC src)
target_compile_definitions(update_core PUBLIC UNICODE _UNICODE NOMINMAX ${LIS_WIN7_COMPAT_DEFINITIONS})
if(WIN32)
    target_link_libraries(update_core PUBLIC advapi32 urlmon)
endif()

# ── Shared UI library ──────────────────────────────────────────
# Compiled once, linked by both result_search and main_app.
add_library(search_ui_core STATIC
    src/app_settings_io.cpp
    src/search_input_view_model.cpp
    src/search_ui_events.cpp
    src/search_ui_presenter.cpp
    src/search_settings_dialog.cpp
    src/search_ui_layout.cpp
    src/trend_window.cpp
    src/trend_chart_renderer.cpp
    src/quality_control_chart_renderer.cpp
)
target_include_directories(search_ui_core PUBLIC src resource)
target_compile_definitions(search_ui_core PUBLIC UNICODE _UNICODE NOMINMAX ${LIS_WIN7_COMPAT_DEFINITIONS})
target_link_libraries(search_ui_core PUBLIC search_core comctl32 comdlg32 gdiplus shell32 ole32)
target_precompile_headers(search_ui_core PRIVATE
    <windows.h>
    <commctrl.h>
    <string>
    <vector>
)

set(LIS_LABELPRINT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../020 LabelPrint/LabelPrint"
    CACHE PATH "Fallback path to the LabelPrint source project")
set(LIS_HAS_LABELPRINT OFF)
set(LIS_LABELPRINT_TARGET "")

find_package(LabelPrint 1.2 CONFIG QUIET)
if(TARGET LabelPrint::labelprint)
    set(LIS_HAS_LABELPRINT ON)
    set(LIS_LABELPRINT_TARGET LabelPrint::labelprint)
    message(STATUS "Using LabelPrint package")
elseif(EXISTS "${LIS_LABELPRINT_DIR}/CMakeLists.txt")
    set(LABELPRINT_BUILD_DEMO OFF CACHE BOOL "Build LabelPrint demo executable" FORCE)
    set(LABELPRINT_BUILD_TESTS OFF CACHE BOOL "Build LabelPrint tests" FORCE)
    if(WIN32)
        set(LABELPRINT_WIN7_COMPAT ON CACHE BOOL "Build LabelPrint with Windows 7 compatibility" FORCE)
    endif()
    if(MSVC AND LIS_STATIC_MSVC_RUNTIME)
        set(LABELPRINT_STATIC_MSVC_RUNTIME ON CACHE BOOL "Link LabelPrint static MSVC runtime" FORCE)
    endif()
    add_subdirectory("${LIS_LABELPRINT_DIR}" "${CMAKE_BINARY_DIR}/labelprint" EXCLUDE_FROM_ALL)
    if(TARGET LabelPrint::labelprint)
        set(LIS_HAS_LABELPRINT ON)
        set(LIS_LABELPRINT_TARGET LabelPrint::labelprint)
    elseif(TARGET labelprint)
        set(LIS_HAS_LABELPRINT ON)
        set(LIS_LABELPRINT_TARGET labelprint)
    endif()
    if(LIS_HAS_LABELPRINT)
        message(STATUS "Using LabelPrint source from ${LIS_LABELPRINT_DIR}")
    endif()
else()
    message(STATUS "LabelPrint package not found and fallback source not found at ${LIS_LABELPRINT_DIR}; barcode printing is disabled.")
endif()

# ── Win32 frontend ────────────────────────────────────────────
add_executable(result_search WIN32
    src/main.cpp
    resource/app.rc
)
target_include_directories(result_search PRIVATE src resource)
target_compile_definitions(result_search PRIVATE UNICODE _UNICODE NOMINMAX ${LIS_WIN7_COMPAT_DEFINITIONS})
target_link_libraries(result_search PRIVATE search_ui_core search_core comctl32 comdlg32 gdiplus shell32 ole32)

if(MINGW)
    target_link_options(result_search PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

# ── Main application (Win32 MDI shell) ────────────────────────
add_executable(main_app WIN32
    src/main_frame.cpp
    src/menu_toolbar.cpp
    src/barcode_label_printing.cpp
    src/barcode_module.cpp
    src/blood_module.cpp
    src/emergency_statistics_module.cpp
    src/hiv_statistics_module.cpp
    src/machine_picker_popup.cpp
    src/mchc_correction_module.cpp
    src/outpatient_query_module.cpp
    src/phone_directory_module.cpp
    src/quality_control_module.cpp
    src/quality_control_settings_dialog.cpp
    src/quality_control_store.cpp
    src/query_module.cpp
    src/regular_report_module.cpp
    src/regular_report_utils.cpp
    src/regular_report_picture.cpp
    src/specimen_sign_module.cpp
    src/settings_module.cpp
    src/search_splitter.cpp
    src/tinflate.cpp
    src/crash_handler.cpp
    resource/app.rc
)
set_target_properties(main_app PROPERTIES OUTPUT_NAME lis_workbench)
target_include_directories(main_app PRIVATE src resource)
target_compile_definitions(main_app PRIVATE UNICODE _UNICODE NOMINMAX ${LIS_WIN7_COMPAT_DEFINITIONS})
target_link_libraries(main_app PRIVATE search_ui_core search_core update_core sqlite3_amalgamation comctl32 comdlg32 gdiplus shell32 ole32 uxtheme iphlpapi ws2_32 winspool dbghelp)
if(LIS_HAS_LABELPRINT)
    target_compile_definitions(main_app PRIVATE LIS_HAS_LABELPRINT=1)
    if(TARGET labelprint)
        target_compile_definitions(labelprint PRIVATE ${LIS_WIN7_COMPAT_DEFINITIONS})
    endif()
    target_link_libraries(main_app PRIVATE ${LIS_LABELPRINT_TARGET})
endif()

# MSVC auto-generates manifest; conflict with app.rc. Disable.
if(MSVC)
    target_link_options(main_app PRIVATE /MANIFEST:NO)
endif()

if(MINGW)
    target_link_options(main_app PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

# ── Standalone updater ────────────────────────────────────────
add_executable(updater
    src/updater_main.cpp
)
set_target_properties(updater PROPERTIES OUTPUT_NAME Updater)
target_include_directories(updater PRIVATE src)
target_compile_definitions(updater PRIVATE UNICODE _UNICODE NOMINMAX ${LIS_WIN7_COMPAT_DEFINITIONS})
target_link_libraries(updater PRIVATE update_core)
if(WIN32)
    target_link_libraries(updater PRIVATE shell32 ole32 oleaut32)
endif()
if(MSVC)
    target_link_options(updater PRIVATE /MANIFEST:NO)
endif()
if(MINGW)
    target_link_options(updater PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

# ── Qt 5.15 frontend (opt-in) ─────────────────────────────────
option(BUILD_QT_GUI "Build Qt 5.15 GUI" OFF)

if(BUILD_QT_GUI)
    find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent)

    add_executable(result_search_qt WIN32
        src_qt/main.cpp
        src_qt/main_window.cpp
        src_qt/settings_dialog.cpp
        src_qt/trend_window.cpp
        src_qt/resources.qrc
    )
    set_target_properties(result_search_qt PROPERTIES AUTOMOC ON)
    target_include_directories(result_search_qt PRIVATE src src_qt)
    target_link_libraries(result_search_qt PRIVATE search_core Qt5::Widgets Qt5::WinMain Qt5::Concurrent)
endif()
