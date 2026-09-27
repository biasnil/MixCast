cmake_minimum_required(VERSION 3.20)
project(MixCast VERSION 0.3 LANGUAGES CXX)

option(MIXCAST_BUILD_GUI "Build the Qt 6 desktop app" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "MixCast is Windows-only (WASAPI).")
endif()

# ---- Core engine (reused by the Qt GUI in the next step) -------------------
add_library(mixcast_core STATIC
    src/common.h
    src/ring_buffer.h
    src/drift_reader.h
    src/voice_processor.h src/voice_processor.cpp
    src/audio_decoder.h  src/audio_decoder.cpp
    src/audio_encoder.h  src/audio_encoder.cpp
    src/resampler.h      src/resampler.cpp
    src/preview_player.h src/preview_player.cpp
    src/audio_edit.h     src/audio_edit.cpp
    src/soundboard.h     src/soundboard.cpp
    src/devices.h        src/devices.cpp
    src/capture_source.h src/capture_source.cpp
    src/mix_engine.h     src/mix_engine.cpp
)
target_include_directories(mixcast_core PUBLIC src)
target_compile_definitions(mixcast_core PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A00000C)   # Win10 SDK 20348+ declarations
target_link_libraries(mixcast_core PUBLIC ole32 avrt mmdevapi uuid mfplat mfreadwrite mfuuid)

if(MSVC)
    target_compile_options(mixcast_core PUBLIC /W4 /utf-8 /permissive-)
else()
    target_compile_options(mixcast_core PUBLIC -Wall -Wextra)
endif()

# ---- Console front-end -------------------------------------------------------
add_executable(mixcast-cli src/main.cpp)
target_link_libraries(mixcast-cli PRIVATE mixcast_core)
if(MINGW)
    target_link_options(mixcast-cli PRIVATE -municode)
endif()

# ---- Qt 6 desktop app --------------------------------------------------------
if(MIXCAST_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
    set(CMAKE_AUTOMOC ON)

    add_executable(mixcast WIN32
        gui/main.cpp
        gui/theme.h          gui/theme.cpp
        gui/widgets.h        gui/widgets.cpp
        gui/channel_strip.h  gui/channel_strip.cpp
        gui/app_picker.h     gui/app_picker.cpp
        gui/hotkeys.h        gui/hotkeys.cpp
        gui/app_paths.h      gui/app_paths.cpp
        gui/editor_page.h    gui/editor_page.cpp
        gui/soundboard_page.h gui/soundboard_page.cpp
        gui/main_window.h    gui/main_window.cpp
    )
    target_include_directories(mixcast PRIVATE gui)
    target_link_libraries(mixcast PRIVATE mixcast_core Qt6::Widgets)

    # Copy the Qt DLLs next to mixcast.exe so it runs by double-click.
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin}")
    if(WINDEPLOYQT)
        add_custom_command(TARGET mixcast POST_BUILD
            COMMAND "${WINDEPLOYQT}" --no-translations "$<TARGET_FILE:mixcast>"
            COMMENT "Deploying Qt runtime next to mixcast.exe")
    endif()
endif()
