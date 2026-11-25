# compat 模块单元测试 (framework/event + JSON structs)
# 仅当对应 target 存在时添加测试，缺失依赖不阻断 configure。
find_package(GTest REQUIRED)
find_package(Qt6 COMPONENTS Core Concurrent QUIET)


# ---- framework_tests: Event / EventChannel / EventSequence / EventDispatcher ----
if(TARGET dde-cooperation-framework)
    add_executable(framework_tests event_test.cpp lifecycle_test.cpp lifecycle_private_test.cpp)
    target_link_libraries(framework_tests PRIVATE
        GTest::GTest
        GTest::Main
        dde-cooperation-framework
    )
    target_include_directories(framework_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/compat/framework/lifecycle/private
    )
    if(TARGET Qt6::Core)
        target_link_libraries(framework_tests PRIVATE Qt6::Core)
    endif()
    if(TARGET Qt6::Concurrent)
        target_link_libraries(framework_tests PRIVATE Qt6::Concurrent)
    endif()
    add_test(NAME framework_tests COMMAND framework_tests)
endif()

# ---- commonstruct_tests: commonstruct.h JSON serialization round-trips ----
add_executable(commonstruct_tests commonstruct_test.cpp compatbase_test.cpp)
target_link_libraries(commonstruct_tests PRIVATE
    GTest::GTest
    GTest::Main
)
target_include_directories(commonstruct_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/compat
    ${CMAKE_SOURCE_DIR}/src/compat/common
    ${CMAKE_SOURCE_DIR}/3rdparty/include
)
add_test(NAME commonstruct_tests COMMAND commonstruct_tests)
