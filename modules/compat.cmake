if(NOT TARGET compat)

if (NOT DEFINED DDE_COOPERATION_PLUGIN_ROOT_DIR)
    set(DDE_COOPERATION_PLUGIN_ROOT_DIR ${LIB_INSTALL_DIR}/dde-cooperation/plugins)
endif()

if (NOT DEFINED DEEPIN_DAEMON_PLUGIN_DIR)
    set(DEEPIN_DAEMON_PLUGIN_DIR ${DDE_COOPERATION_PLUGIN_ROOT_DIR}/daemon)
endif()

# build plugins dir
if(NOT DEFINED DDE_COOPERATION_PLUGIN_ROOT_DEBUG_DIR)
    set(DDE_COOPERATION_PLUGIN_ROOT_DEBUG_DIR ${CMAKE_CURRENT_BINARY_DIR}/plugins)
endif()

if (WIN32)
    set(DEPLOY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output")
    message("   >>> DEPLOY_OUTPUT_DIRECTORY  ${DEPLOY_OUTPUT_DIRECTORY}")

    # windows runtime output defined
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DEPLOY_OUTPUT_DIRECTORY})
endif()

# The protoc tool not for all platforms, so use the generate out sources.
set(USE_PROTOBUF_FILES ON)
# 检查protobuf目录是否存在
if(EXISTS "${PROJECT_SOURCE_DIR}/3rdparty/protobuf")
    set(PROTOBUF_DIR "${PROJECT_SOURCE_DIR}/3rdparty/protobuf")
    include_directories(${PROTOBUF_DIR}/src)

    if(MSVC)
        # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /std:c++17")
    else()
        # protobuf.a 需要加"-fPIC"，否则无法连接到.so
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    endif()
    add_subdirectory("${PROTOBUF_DIR}" protobuf)
endif()

set(BUILD_SHARED_LIBS ON)

endif()
