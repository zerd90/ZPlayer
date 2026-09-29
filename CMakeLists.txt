cmake_minimum_required(VERSION 3.20)
project(ZPlayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/submodules/imgui)

aux_source_directory(src SRC_FILES)
imgui_add_executable(${PROJECT_NAME} ${SRC_FILES} MainWindow.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE imgui)

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_SOURCE_DIR}/bin)
