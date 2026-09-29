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

aux_source_directory(src SRC_FILES)
imgui_add_executable(${PROJECT_NAME} ${SRC_FILES} MainWindow.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE imgui miniaudio)

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_SOURCE_DIR}/bin)
