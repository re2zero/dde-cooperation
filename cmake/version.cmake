set (APP_VERSION_STAGE "release")

if (NOT DEFINED APP_VERSION)
    # 读取 debian/changelog 文件的第一行
    file(READ ${CMAKE_SOURCE_DIR}/debian/changelog CHANGELOG_CONTENT)

    # 将内容按行分割
    string(REPLACE "\n" ";" CHANGELOG_LINES "${CHANGELOG_CONTENT}")

    # 提取第一行
    list(GET CHANGELOG_LINES 0 FIRST_LINE)
    # 使用正则表达式提取版本号
    if (FIRST_LINE MATCHES "([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(APP_VERSION "${CMAKE_MATCH_1}")
        string(REPLACE "." ";" VERSION_PARTS "${APP_VERSION}")
        list(GET VERSION_PARTS 0 APP_VERSION_MAJOR)
        list(GET VERSION_PARTS 1 APP_VERSION_MINOR)
        list(GET VERSION_PARTS 2 APP_VERSION_PATCH)
    else()
        message(STATUS "Cannot find version in changelog")
        set (APP_VERSION_MAJOR 1)
        set (APP_VERSION_MINOR 0)
        set (APP_VERSION_PATCH 5)
    endif()
endif()

#
# Version
#
if (NOT DEFINED APP_VERSION_MAJOR)
    if (DEFINED ENV{APP_VERSION_MAJOR})
        set (APP_VERSION_MAJOR $ENV{APP_VERSION_MAJOR})
    else()
        set (APP_VERSION_MAJOR 1)
    endif()
endif()

if (NOT DEFINED APP_VERSION_MINOR)
    if (DEFINED ENV{APP_VERSION_MINOR})
        set (APP_VERSION_MINOR $ENV{APP_VERSION_MINOR})
    else()
        set (APP_VERSION_MINOR 0)
    endif()
endif()

if (NOT DEFINED APP_VERSION_PATCH)
    if (DEFINED ENV{APP_VERSION_PATCH})
        set (APP_VERSION_PATCH $ENV{APP_VERSION_PATCH})
    else()
        set (APP_VERSION_PATCH 0)
        message (WARNING "version wasn't set. Set to ${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}")
    endif()
endif()

if (DEFINED ENV{BUILD_NUMBER})
    set (APP_BUILD_NUMBER $ENV{BUILD_NUMBER})
else()
    set (APP_BUILD_NUMBER 1)
endif()

if(NOT DEFINED APP_VERSION)
    set (APP_VERSION "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}-${APP_VERSION_STAGE}")
endif()
message (STATUS "Full version string is '" ${APP_VERSION} "'")

add_definitions (-DAPP_VERSION="${APP_VERSION}")
add_definitions (-DAPP_VERSION_MAJOR=${APP_VERSION_MAJOR})
add_definitions (-DAPP_VERSION_MINOR=${APP_VERSION_MINOR})
add_definitions (-DAPP_VERSION_PATCH=${APP_VERSION_PATCH})
add_definitions (-DAPP_BUILD_NUMBER=${APP_BUILD_NUMBER})
