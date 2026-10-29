cmake_minimum_required(VERSION 3.21)
project(DeskArk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)

set(SPINE_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/spine-cpp/spine-cpp)
if(EXISTS ${SPINE_CPP_DIR}/include/spine/Skeleton.h)
    file(GLOB_RECURSE SPINE_SOURCES ${SPINE_CPP_DIR}/src/*.cpp)
    add_library(spine-cpp STATIC ${SPINE_SOURCES})
    target_include_directories(spine-cpp PUBLIC ${SPINE_CPP_DIR}/include)
else()
    message(FATAL_ERROR
        "未找到 spine-cpp。\n"
        "请将 spine-cpp (与 .skel 导出版本匹配的分支) 放到: ${SPINE_CPP_DIR}\n"
        "使得 ${SPINE_CPP_DIR}/include/spine/Skeleton.h 存在。")
endif()

add_executable(DeskArk
    src/main.cpp
    src/PetWindow.cpp
    src/PetWindow.h
    src/SpineWidget.cpp
    src/SpineWidget.h
)

target_link_libraries(DeskArk PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    spine-cpp
)

# 构建后把 models、Qt 依赖（windeployqt）和 MinGW 运行时复制到 exe 旁边，方便直接运行
get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt6_DIR}/../../../bin")
add_custom_command(TARGET DeskArk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/models
        $<TARGET_FILE_DIR:DeskArk>/models
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/RoleSelect.json
        $<TARGET_FILE_DIR:DeskArk>/RoleSelect.json
)
if(WINDEPLOYQT_EXE)
    add_custom_command(TARGET DeskArk POST_BUILD
        COMMAND ${WINDEPLOYQT_EXE} --no-translations $<TARGET_FILE:DeskArk>
    )
endif()
# MinGW 运行时最后复制并覆盖：windeployqt 可能从 PATH 拿到错误版本的 libstdc++ 等
add_custom_command(TARGET DeskArk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MINGW_BIN_DIR}/libstdc++-6.dll
        ${MINGW_BIN_DIR}/libgcc_s_seh-1.dll
        ${MINGW_BIN_DIR}/libwinpthread-1.dll
        $<TARGET_FILE_DIR:DeskArk>
)
