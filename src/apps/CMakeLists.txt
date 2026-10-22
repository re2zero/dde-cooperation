add_subdirectory(dde-cooperation)

if (CMAKE_SYSTEM MATCHES "Linux")
    add_subdirectory(dde-cooperation-transfer)
endif()

add_subdirectory(data-transfer)

# 特权子网转发助手（system bus 服务）
add_subdirectory(forward-helper)

# launcher
# add_subdirectory("launcher")
