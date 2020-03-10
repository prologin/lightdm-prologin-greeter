cmake_minimum_required(VERSION 3.16)
project(lightdm_prologin_greeter)

include(GNUInstallDirs REQUIRED)

find_package(Qt5 COMPONENTS Widgets Network WebEngineWidgets WebChannel REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIGHTDM REQUIRED liblightdm-qt5-3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(
        lightdm-prologin-greeter
        src/main.cc
        src/ProloGreet.cc
        src/res.qrc)

target_link_libraries(
        lightdm-prologin-greeter PRIVATE
        Qt5::Widgets
        Qt5::Network
        Qt5::WebEngineWidgets
        Qt5::WebChannel
        ${LIGHTDM_LIBRARIES})

target_include_directories(lightdm-prologin-greeter
        PRIVATE ${CMAKE_BINARY_DIR} ${LIGHTDM_INCLUDE_DIRS})

install(TARGETS lightdm-prologin-greeter
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES conf/lightdm-prologin-greeter.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/xgreeters)

install(FILES conf/lightdm-prologin-greeter.conf
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/lightdm)
