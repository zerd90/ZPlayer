cmake_minimum_required(VERSION 3.20)
project(ZPlayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/submodules/imgui)

# miniaudio: header-only; do not add_subdirectory (use our own STATIC lib + one impl TU).
add_library(miniaudio STATIC
    src/media/miniaudio_impl.c
)
target_include_directories(miniaudio PUBLIC
    ${CMAKE_SOURCE_DIR}/submodules/miniaudio
)
if(APPLE)
    target_link_libraries(miniaudio PUBLIC
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework AudioUnit"
    )
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
# Windows: no extra system libraries required.

# ---------------------------------------------------------------------------
# Vendored FFmpeg 9.0.2 (3rdParty/ffmpeg) — do NOT use system pkg-config.
# macOS: static .a from scripts/build-macos.sh
# Windows: shared .lib/.dll from scripts/fetch-windows.sh (gyan full shared)
# Linux: lib/linux not provided yet (scripts/build-linux.sh is a placeholder)
# ---------------------------------------------------------------------------
set(ZPLAYER_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/3rdParty/ffmpeg")
set(ZPLAYER_FFMPEG_INCLUDE "${ZPLAYER_FFMPEG_ROOT}/include")

if(APPLE)
    set(ZPLAYER_FFMPEG_LIBDIR "${ZPLAYER_FFMPEG_ROOT}/lib/macos")
elseif(WIN32)
    set(ZPLAYER_FFMPEG_LIBDIR "${ZPLAYER_FFMPEG_ROOT}/lib/windows")
elseif(UNIX)
    set(ZPLAYER_FFMPEG_LIBDIR "${ZPLAYER_FFMPEG_ROOT}/lib/linux")
else()
    message(FATAL_ERROR "Unsupported platform for vendored FFmpeg")
endif()

if(NOT EXISTS "${ZPLAYER_FFMPEG_INCLUDE}/libavutil/avutil.h")
    message(FATAL_ERROR
        "Vendored FFmpeg headers missing under ${ZPLAYER_FFMPEG_INCLUDE}. "
        "On macOS run: 3rdParty/ffmpeg/scripts/build-macos.sh "
        "On Windows run: 3rdParty/ffmpeg/scripts/fetch-windows.sh")
endif()

set(_zplayer_ffmpeg_libs avformat avcodec avutil swscale swresample)
set(_zplayer_ffmpeg_found TRUE)
foreach(_lib IN LISTS _zplayer_ffmpeg_libs)
    if(WIN32)
        set(_cand "${ZPLAYER_FFMPEG_LIBDIR}/${_lib}.lib")
    else()
        # Prefer static on macOS/Linux vendoring; fall back to shared soname.
        set(_cand "")
        foreach(_ext IN ITEMS a dylib so)
            if(EXISTS "${ZPLAYER_FFMPEG_LIBDIR}/lib${_lib}.${_ext}")
                set(_cand "${ZPLAYER_FFMPEG_LIBDIR}/lib${_lib}.${_ext}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT _cand OR NOT EXISTS "${_cand}")
        set(_zplayer_ffmpeg_found FALSE)
        message(STATUS "Missing vendored FFmpeg library: ${_lib} under ${ZPLAYER_FFMPEG_LIBDIR}")
    else()
        set(ZPLAYER_FFMPEG_${_lib}_PATH "${_cand}")
    endif()
endforeach()

if(NOT _zplayer_ffmpeg_found)
    if(UNIX AND NOT APPLE AND NOT WIN32)
        message(FATAL_ERROR
            "Vendored FFmpeg libraries not found in ${ZPLAYER_FFMPEG_LIBDIR}. "
            "Linux builds are not provided yet (see 3rdParty/ffmpeg/scripts/build-linux.sh).")
    else()
        message(FATAL_ERROR
            "Vendored FFmpeg libraries not found in ${ZPLAYER_FFMPEG_LIBDIR}. "
            "macOS: run 3rdParty/ffmpeg/scripts/build-macos.sh ; "
            "Windows: run 3rdParty/ffmpeg/scripts/fetch-windows.sh")
    endif()
endif()

add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg INTERFACE "${ZPLAYER_FFMPEG_INCLUDE}")

# Link order matters for static FFmpeg: avformat → avcodec → swscale/swresample → avutil.
target_link_libraries(ffmpeg INTERFACE
    "${ZPLAYER_FFMPEG_avformat_PATH}"
    "${ZPLAYER_FFMPEG_avcodec_PATH}"
    "${ZPLAYER_FFMPEG_swscale_PATH}"
    "${ZPLAYER_FFMPEG_swresample_PATH}"
    "${ZPLAYER_FFMPEG_avutil_PATH}"
)

if(APPLE)
    # Static FFmpeg on macOS typically needs these (enabled in build-macos.sh).
    find_library(ZPLAYER_ICONV_LIB iconv REQUIRED)
    find_library(ZPLAYER_BZ2_LIB bz2 REQUIRED)
    find_library(ZPLAYER_Z_LIB z REQUIRED)
    target_link_libraries(ffmpeg INTERFACE
        "${ZPLAYER_ICONV_LIB}"
        "${ZPLAYER_BZ2_LIB}"
        "${ZPLAYER_Z_LIB}"
    )
elseif(WIN32)
    # Shared DLLs live next to import libs; ensure the exe can find them at runtime
    # when launched from the build tree (copy step below). Also link system deps
    # commonly required by the gyan shared build.
    target_link_libraries(ffmpeg INTERFACE ws2_32 secur32 bcrypt)
endif()

aux_source_directory(src SRC_FILES)
# ffmpeg_link_probe.c lives under src/media/ (aux_source_directory is non-recursive).
imgui_add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    MainWindow.cpp
    src/media/ffmpeg_link_probe.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE imgui miniaudio ffmpeg)

if(WIN32)
    # Copy MVP DLLs beside the executable so the loader finds them without PATH hacks.
    foreach(_dll IN ITEMS avutil-61 avcodec-63 avformat-63 swresample-7 swscale-10)
        set(_dll_path "${ZPLAYER_FFMPEG_LIBDIR}/${_dll}.dll")
        if(EXISTS "${_dll_path}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll_path}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copy ${_dll}.dll next to ${PROJECT_NAME}"
            )
        endif()
    endforeach()
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_SOURCE_DIR}/bin)
