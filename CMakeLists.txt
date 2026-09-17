cmake_minimum_required(VERSION 3.20)
project(aura_project LANGUAGES CXX)

# CMAKE_BUILD_TYPE 未设时显式设置默认值
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# AddressSanitizer 选项（仅 MSVC Debug 下启用）
option(AURA_ENABLE_ASAN "Enable AddressSanitizer (MSVC Debug build only)" OFF)
if (AURA_ENABLE_ASAN)
    if (MSVC)
        message(STATUS "AddressSanitizer enabled for MSVC Debug builds")
        add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
        add_link_options($<$<CONFIG:Debug>:/INCREMENTAL:NO>)
    endif()
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)

if (MSVC)
    add_compile_options(
        /utf-8
        /W4
        /external:W0
        /external:anglebrackets
        "/external:I${CMAKE_CURRENT_SOURCE_DIR}/include/third_party"
        $<$<CONFIG:Release>:/O2>
    )
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
include_directories(SYSTEM
    ${CMAKE_CURRENT_SOURCE_DIR}/include/third_party
)

# 1. 核心守护进程 (aura_daemon)
set(DAEMON_SOURCES
    src/main.cpp
    src/aura/aura_adapter.cpp
    src/aura/hal_compat.cpp
    src/aura/keymap.cpp
    src/monitor/foreground_monitor.cpp
    src/config/rule_engine.cpp
    src/engine/effect_engine.cpp
    src/engine/builtin_effects.cpp
    src/engine/plugin_manager.cpp
    src/engine/overlay_manager.cpp
    src/supervisor/web_supervisor.cpp
    src/gsi/gsi_adapter.cpp
)

set(DAEMON_HEADERS
    include/aura/aura_adapter.h
    include/aura/aura_types.h
    include/aura/hal_compat.h
    include/aura/keymap.h
    include/monitor/foreground_monitor.h
    include/config/rule_engine.h
    include/engine/effect.h
    include/engine/builtin_effects.h
    include/engine/effect_engine.h
    include/engine/plugin_interface.h
    include/engine/plugin_manager.h
    include/engine/overlay_manager.h
    include/supervisor/web_supervisor.h
    include/gsi/gsi_adapter.h
    include/utils/logger.h
    include/utils/system_info.h
    include/third_party/httplib.h
    include/third_party/json.hpp
)

add_executable(aura_daemon ${DAEMON_SOURCES} ${DAEMON_HEADERS})

if (MSVC)
    target_compile_options(aura_daemon PRIVATE /permissive-)
endif()

target_link_libraries(aura_daemon PRIVATE
    ole32
    oleaut32
    user32
    shell32
    psapi
    ws2_32
    crypt32
    version
)

add_custom_command(TARGET aura_daemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:aura_daemon>
        "${CMAKE_CURRENT_SOURCE_DIR}/aura_daemon.exe"
    COMMENT "Copying aura_daemon.exe to workspace root"
)

add_executable(test_com tests/test_com.cpp)
target_link_libraries(test_com PRIVATE ole32 oleaut32)

# 2. 轻量网页配置服务 (aura_web_ui)
set(WEB_SOURCES
    src/web/main_web.cpp
    src/web/web_server.cpp
)

set(WEB_HEADERS
    src/web/web_server.h
    include/third_party/httplib.h
    include/third_party/json.hpp
)

add_executable(aura_web_ui ${WEB_SOURCES} ${WEB_HEADERS})

target_link_libraries(aura_web_ui PRIVATE
    ws2_32
    crypt32
    user32
)

add_custom_command(TARGET aura_web_ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:aura_web_ui>
        "${CMAKE_CURRENT_SOURCE_DIR}/aura_web_ui.exe"
    COMMENT "Copying aura_web_ui.exe to workspace root"
)

# 3. 单元测试: test_gsi_rules
add_executable(test_gsi_rules
    tests/test_gsi_rules.cpp
    src/config/rule_engine.cpp
    src/engine/builtin_effects.cpp
    src/engine/effect_engine.cpp
    src/engine/plugin_manager.cpp
    src/engine/overlay_manager.cpp
    src/gsi/gsi_adapter.cpp
    src/aura/keymap.cpp
    src/aura/aura_adapter.cpp
    src/aura/hal_compat.cpp
    src/web/web_server.cpp
)

target_link_libraries(test_gsi_rules PRIVATE
    ws2_32
    crypt32
    user32
    ole32
    oleaut32
    shell32
    psapi
    version
)


# 4. 诊断测试: test_diag_hook (手动交互式诊断工具，不注册到 CTest)
add_executable(test_diag_hook tests/test_diag_hook.cpp)
target_link_libraries(test_diag_hook PRIVATE user32)

# 5. CTest 测试体系集成
enable_testing()
add_library(plugin_fixture_v1 SHARED tests/fixtures/plugin_reload_fixture.cpp)
target_compile_definitions(plugin_fixture_v1 PRIVATE FIXTURE_VALUE=17)
add_library(plugin_fixture_v2 SHARED tests/fixtures/plugin_reload_fixture.cpp)
target_compile_definitions(plugin_fixture_v2 PRIVATE FIXTURE_VALUE=93)
add_executable(test_plugin_runtime tests/test_plugin_runtime.cpp src/engine/plugin_manager.cpp)
add_dependencies(test_plugin_runtime plugin_fixture_v1 plugin_fixture_v2)
add_test(NAME plugin_runtime COMMAND test_plugin_runtime
    $<TARGET_FILE:plugin_fixture_v1> $<TARGET_FILE:plugin_fixture_v2>)
set_tests_properties(plugin_runtime PROPERTIES TIMEOUT 30)
add_test(NAME gsi_rules COMMAND test_gsi_rules)
set_tests_properties(gsi_rules PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME aura_hal_py COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aura_hal.py")
    set_tests_properties(aura_hal_py PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()


