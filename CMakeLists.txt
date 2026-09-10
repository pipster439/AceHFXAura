cmake_minimum_required(VERSION 3.20)
project(aura_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 1. 核心守护进程 (aura_daemon)
set(DAEMON_SOURCES
    src/main.cpp
    src/aura/aura_adapter.cpp
    src/aura/keymap.cpp
    src/monitor/foreground_monitor.cpp
    src/config/rule_engine.cpp
    src/engine/effect_engine.cpp
    src/engine/builtin_effects.cpp
    src/supervisor/web_supervisor.cpp
    src/gsi/gsi_adapter.cpp
)

set(DAEMON_HEADERS
    include/aura/aura_adapter.h
    include/aura/aura_types.h
    include/aura/keymap.h
    include/monitor/foreground_monitor.h
    include/config/rule_engine.h
    include/engine/effect.h
    include/engine/builtin_effects.h
    include/engine/effect_engine.h
    include/supervisor/web_supervisor.h
    include/gsi/gsi_adapter.h
    include/utils/logger.h
    include/utils/system_info.h
    include/third_party/httplib.h
    include/third_party/json.hpp
)

add_executable(aura_daemon ${DAEMON_SOURCES} ${DAEMON_HEADERS})

if (MSVC)
    target_compile_options(aura_daemon PRIVATE /utf-8 /W3 /O2)
endif()

target_link_libraries(aura_daemon PRIVATE
    ole32
    oleaut32
    user32
    psapi
    ws2_32
    crypt32
)

add_executable(test_com test_com.cpp)
target_compile_options(test_com PRIVATE /utf-8 /W3 /O2)
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

if (MSVC)
    target_compile_options(aura_web_ui PRIVATE /utf-8 /W3 /O2)
endif()

target_link_libraries(aura_web_ui PRIVATE
    ws2_32
    crypt32
    user32
)

# 3. 单元测试: test_gsi_rules
add_executable(test_gsi_rules
    tests/test_gsi_rules.cpp
    src/config/rule_engine.cpp
    src/engine/builtin_effects.cpp
    src/gsi/gsi_adapter.cpp
    src/aura/keymap.cpp
)

if (MSVC)
    target_compile_options(test_gsi_rules PRIVATE /utf-8 /W3 /O2)
endif()

target_link_libraries(test_gsi_rules PRIVATE
    ws2_32
    crypt32
    user32
)

# 4. 诊断测试: test_diag_hook
add_executable(test_diag_hook tests/test_diag_hook.cpp)
if (MSVC)
    target_compile_options(test_diag_hook PRIVATE /utf-8 /W3 /O2)
endif()
target_link_libraries(test_diag_hook PRIVATE user32)

