# 特权子网转发助手：system bus 服务，管理 OUTPUT redirect 规则
cmake_minimum_required(VERSION 3.13)

set(CMAKE_AUTOMOC ON)

project(dde-cooperation-forward-helper VERSION 1.0.0)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core DBus Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus Network)

add_executable(dde-cooperation-forward-helper
    main.cpp
    forwardhelperservice.cpp
)

target_include_directories(dde-cooperation-forward-helper PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/lib/common/manager
)

target_link_libraries(dde-cooperation-forward-helper PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Qt${QT_VERSION_MAJOR}::Network
)

install(TARGETS dde-cooperation-forward-helper DESTINATION ${EXECUTABLE_INSTALL_DIR})

# D-Bus activation: the helper starts on demand as root.
set(FORWARD_HELPER_EXEC "${EXECUTABLE_INSTALL_DIR}/dde-cooperation-forward-helper")
configure_file(data/cn.uniontech.cooperation.forward.service.in
               ${CMAKE_CURRENT_BINARY_DIR}/cn.uniontech.cooperation.forward.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cn.uniontech.cooperation.forward.service
        DESTINATION share/dbus-1/system-services)
# dbus-daemon only reads /etc: this must not follow the install prefix.
install(FILES data/cn.uniontech.cooperation.forward.conf
        DESTINATION /etc/dbus-1/system.d)
install(FILES data/cn.uniontech.cooperation.forward.policy
        DESTINATION share/polkit-1/actions)
