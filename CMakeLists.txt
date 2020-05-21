cmake_minimum_required(VERSION 3.16)
project(lightdm-prologin-greeter)

include(GNUInstallDirs REQUIRED)

find_package(Qt5 COMPONENTS Widgets Network WebEngineWidgets WebChannel REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIGHTDM REQUIRED liblightdm-qt5-3)
pkg_check_modules(XCB REQUIRED xcb xcb-xkb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(
        lightdm-prologin-greeter
        src/main.cc
        src/ProloGreet.cc
        src/KeyboardModel.cc
        src/res.qrc)

target_link_libraries(
        lightdm-prologin-greeter PRIVATE
        Qt5::Widgets
        Qt5::Network
        Qt5::WebEngineWidgets
        Qt5::WebChannel
        ${LIGHTDM_LIBRARIES}
        ${XCB_LIBRARIES})

target_include_directories(lightdm-prologin-greeter
        PRIVATE ${CMAKE_BINARY_DIR} ${LIGHTDM_INCLUDE_DIRS} ${XCB_INCLUDE_DIRS})

install(TARGETS lightdm-prologin-greeter
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES conf/lightdm-prologin-greeter.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/xgreeters)

install(FILES conf/lightdm-prologin-greeter.conf
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/lightdm)


configure_file(devel/lightdm.conf.in
        ${CMAKE_BINARY_DIR}/lightdm.conf @ONLY)
configure_file(devel/lightdm-prologin-greeter.desktop.in
        ${CMAKE_BINARY_DIR}/lightdm-prologin-greeter.desktop @ONLY)

# Test with:
# lightdm --debug --test-mode -c ${CMAKE_BINARY_DIR}/lightdm.conf
