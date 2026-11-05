cmake_minimum_required(VERSION 3.19)
project(DeskArk-Lite LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets)

qt_standard_project_setup()

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party
)
file(GLOB SPINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/spine-cpp/spine-cpp/src/spine/*.cpp
)
file(GLOB ASSET_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/*
)
add_library(spine-cpp STATIC ${SPINE_SOURCES})
target_include_directories(spine-cpp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/spine-cpp/spine-cpp/include
)
target_compile_features(spine-cpp PUBLIC cxx_std_17)

qt_add_executable(DeskArk-Lite
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/PetWindow.cpp
    src/PetWindow.h
    src/Common.cpp
    src/Common.h
    src/SpineCharacter.cpp
    src/SpineCharacter.h
    src/PetView.cpp
    src/PetView.h
    src/SpineRenderer.cpp
    src/SpineRenderer.h
)

qt_add_resources(DeskArk-Lite "assets"
    PREFIX "/"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${ASSET_FILES}
)

target_link_libraries(DeskArk-Lite
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        spine-cpp
        nlohmann_json
)

include(GNUInstallDirs)
set(CMAKE_INSTALL_BINDIR ".")   # exe/DLL/plugins 全部平铺在安装根目录

install(TARGETS DeskArk-Lite
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 运行时数据
install(FILES RoleSelect.json DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY models DESTINATION ${CMAKE_INSTALL_BINDIR})

if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    add_custom_target(dist
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                --prefix "${CMAKE_SOURCE_DIR}/dist"
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations --compiler-runtime
                --dir "${CMAKE_SOURCE_DIR}/dist"
                "${CMAKE_SOURCE_DIR}/dist/DeskArk-Lite.exe"
        DEPENDS DeskArk-Lite
        USES_TERMINAL
        COMMENT "打包到 dist/"
    )
endif()
