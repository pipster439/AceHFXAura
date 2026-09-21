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
    src/aura/native_hid_backend.cpp
    src/aura/hal_compat.cpp
    src/aura/keymap.cpp
    src/monitor/foreground_monitor.cpp
    src/config/rule_engine.cpp
    src/config/automation_evaluation.cpp
    src/engine/effect_engine.cpp
    src/engine/automation_effect_runtime.cpp
    src/engine/builtin_effects.cpp
    src/engine/plugin_manager.cpp
    src/engine/overlay_manager.cpp
    src/supervisor/web_supervisor.cpp
    src/gsi/gsi_adapter.cpp
    src/config/lighting_service.cpp
    src/config/automation_service.cpp
    src/config/automation_authoring.cpp
    src/config/config_writer_util.cpp
)

set(DAEMON_HEADERS
    include/aura/aura_adapter.h
    include/aura/native_hid_backend.h
    include/aura/aura_types.h
    include/aura/hal_compat.h
    include/aura/keymap.h
    include/monitor/foreground_monitor.h
    include/config/rule_engine.h
    include/config/lighting_service.h
    include/config/automation_service.h
    include/config/config_writer_util.h
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
    setupapi
    hid
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
    src/config/config_writer_util.cpp
)

set(WEB_HEADERS
    src/web/web_server.h
    include/config/config_writer_util.h
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
    src/config/automation_evaluation.cpp
    src/engine/builtin_effects.cpp
    src/engine/effect_engine.cpp
    src/engine/automation_effect_runtime.cpp
    src/engine/plugin_manager.cpp
    src/engine/overlay_manager.cpp
    src/gsi/gsi_adapter.cpp
    src/aura/keymap.cpp
    src/aura/aura_adapter.cpp
    src/aura/native_hid_backend.cpp
    src/aura/hal_compat.cpp
    src/web/web_server.cpp
    src/config/lighting_service.cpp
    src/config/automation_service.cpp
    src/config/automation_authoring.cpp
    src/config/config_writer_util.cpp
)

target_link_libraries(test_gsi_rules PRIVATE
    setupapi
    hid
    ws2_32
    crypt32
    user32
    ole32
    oleaut32
    shell32
    psapi
    version
)

# 4. 单元测试: test_native_hid
add_executable(test_native_hid
    tests/test_native_hid.cpp
    src/aura/native_hid_backend.cpp
    src/aura/aura_adapter.cpp
    src/aura/hal_compat.cpp
    src/aura/keymap.cpp
)

target_link_libraries(test_native_hid PRIVATE
    setupapi
    hid
    ole32
    oleaut32
    user32
    shell32
    psapi
    version
)

# 5. 诊断测试: test_diag_hook (手动交互式诊断工具，不注册到 CTest)
add_executable(test_diag_hook tests/test_diag_hook.cpp)
target_link_libraries(test_diag_hook PRIVATE user32)

# 5.5 单元测试: test_runtime_status
add_executable(test_runtime_status
    tests/test_runtime_status.cpp
    src/aura/aura_adapter.cpp
    src/aura/native_hid_backend.cpp
    src/aura/hal_compat.cpp
    src/aura/keymap.cpp
)
target_link_libraries(test_runtime_status PRIVATE
    setupapi
    hid
    ole32
    oleaut32
    user32
    shell32
    psapi
    version
)

# 5.6 单元测试: test_lighting_service
add_executable(test_lighting_service
    tests/test_lighting_service.cpp
    src/config/lighting_service.cpp
    src/config/automation_service.cpp
    src/config/automation_authoring.cpp
    src/config/config_writer_util.cpp
    src/config/rule_engine.cpp
    src/config/automation_evaluation.cpp
    src/engine/builtin_effects.cpp
    src/engine/effect_engine.cpp
    src/engine/automation_effect_runtime.cpp
    src/engine/plugin_manager.cpp
    src/engine/overlay_manager.cpp
    src/gsi/gsi_adapter.cpp
    src/aura/keymap.cpp
    src/aura/aura_adapter.cpp
    src/aura/native_hid_backend.cpp
    src/aura/hal_compat.cpp
    src/web/web_server.cpp
)
target_link_libraries(test_lighting_service PRIVATE
    setupapi
    hid
    ole32
    oleaut32
    user32
    shell32
    psapi
    version
    ws2_32
    crypt32
)

# 5.7 单元测试: test_automation_service
add_executable(test_automation_service
    tests/test_automation_service.cpp
    src/config/automation_service.cpp
    src/config/automation_authoring.cpp
    src/config/config_writer_util.cpp
    src/config/lighting_service.cpp
    src/config/rule_engine.cpp
    src/config/automation_evaluation.cpp
    src/engine/builtin_effects.cpp
    src/engine/effect_engine.cpp
    src/engine/automation_effect_runtime.cpp
    src/engine/plugin_manager.cpp
    src/engine/overlay_manager.cpp
    src/gsi/gsi_adapter.cpp
    src/aura/keymap.cpp
    src/aura/aura_adapter.cpp
    src/aura/native_hid_backend.cpp
    src/aura/hal_compat.cpp
    src/web/web_server.cpp
)
target_link_libraries(test_automation_service PRIVATE
    setupapi
    hid
    ole32
    oleaut32
    user32
    shell32
    psapi
    version
    ws2_32
    crypt32
)

# 6. CTest 测试体系集成
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

# Stage 1 generated regression DLLs are separate from frozen Stage 0 evidence.
function(add_abi_fixture name)
    add_library(${name} SHARED tests/fixtures/plugin_abi_fixture.cpp)
    target_compile_definitions(${name} PRIVATE ${ARGN})
endfunction()
add_abi_fixture(abi_full_old FIXTURE_VALUE=17)
add_abi_fixture(abi_full_new FIXTURE_VALUE=93 FIXTURE_ABI=0x00010000)
add_abi_fixture(abi_unprefixed FIXTURE_UNPREFIXED)
add_abi_fixture(abi_absent FIXTURE_NO_VERSION)
add_abi_fixture(abi_finished_only FIXTURE_NO_OPACITY)
add_abi_fixture(abi_opacity_only FIXTURE_NO_FINISHED)
add_abi_fixture(abi_no_lifecycle FIXTURE_NO_LIFECYCLE_VERSION FIXTURE_NO_FINISHED FIXTURE_NO_OPACITY)
add_abi_fixture(abi_lifecycle_unknown FIXTURE_LIFECYCLE=2)
add_abi_fixture(abi_lifecycle_no_revision FIXTURE_NO_LIFECYCLE_VERSION)
add_abi_fixture(abi_zero FIXTURE_ABI=0)
add_abi_fixture(abi_two FIXTURE_ABI=2)
add_abi_fixture(abi_minor FIXTURE_ABI=0x00010001)
add_abi_fixture(abi_missing_destroy FIXTURE_NO_DESTROY)
add_abi_fixture(abi_factory_null FIXTURE_FACTORY_NULL)
add_abi_fixture(abi_factory_throw FIXTURE_FACTORY_THROWS)
add_abi_fixture(abi_version_throw FIXTURE_VERSION_THROWS)
add_abi_fixture(abi_destroy_throw FIXTURE_DESTROY_THROWS)
add_abi_fixture(abi_renamed FIXTURE_NAME="renamed_fixture" FIXTURE_VALUE=93)
add_abi_fixture(abi_other FIXTURE_NAME="other_fixture")
add_executable(test_plugin_abi tests/test_plugin_abi.cpp src/engine/plugin_manager.cpp)
add_dependencies(test_plugin_abi
    abi_full_old abi_full_new abi_unprefixed abi_absent abi_finished_only abi_opacity_only
    abi_no_lifecycle abi_lifecycle_unknown abi_lifecycle_no_revision abi_zero abi_two abi_minor
    abi_missing_destroy abi_factory_null abi_factory_throw abi_version_throw abi_destroy_throw abi_renamed abi_other)
add_test(NAME plugin_abi COMMAND test_plugin_abi $<TARGET_FILE_DIR:abi_full_old>
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/automation_v2_stage0")
set_tests_properties(plugin_abi PROPERTIES TIMEOUT 30)
target_compile_definitions(test_gsi_rules PRIVATE
    AURA_STAGE0_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/automation_v2_stage0/configs")
add_test(NAME runtime_status COMMAND test_runtime_status)
set_tests_properties(runtime_status PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME lighting_service COMMAND test_lighting_service)
set_tests_properties(lighting_service PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME automation_service COMMAND test_automation_service)
set_tests_properties(automation_service PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME gsi_rules COMMAND test_gsi_rules)
set_tests_properties(gsi_rules PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME native_hid COMMAND test_native_hid)
set_tests_properties(native_hid PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME stage0_fixture_integrity COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_stage0_fixtures.py")
    set_tests_properties(stage0_fixture_integrity PROPERTIES FIXTURES_SETUP stage0_evidence)
    set_tests_properties(plugin_abi PROPERTIES FIXTURES_REQUIRED stage0_evidence)
    add_test(NAME aura_hal_py COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aura_hal.py")
    set_tests_properties(aura_hal_py PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()



# Same production sources/libraries as the legacy rules suite, independent conformance test.
get_target_property(AUTOMATION_RULE_TEST_SOURCES test_gsi_rules SOURCES)
list(REMOVE_ITEM AUTOMATION_RULE_TEST_SOURCES tests/test_gsi_rules.cpp)
add_executable(test_automation_v2 tests/test_automation_v2.cpp ${AUTOMATION_RULE_TEST_SOURCES})
get_target_property(AUTOMATION_RULE_TEST_LIBRARIES test_gsi_rules LINK_LIBRARIES)
target_link_libraries(test_automation_v2 PRIVATE ${AUTOMATION_RULE_TEST_LIBRARIES})
add_test(NAME automation_v2 COMMAND test_automation_v2)
set_tests_properties(automation_v2 PROPERTIES TIMEOUT 30)
add_executable(test_automation_authoring tests/test_automation_authoring.cpp ${AUTOMATION_RULE_TEST_SOURCES})
target_link_libraries(test_automation_authoring PRIVATE ${AUTOMATION_RULE_TEST_LIBRARIES})
add_test(NAME automation_authoring COMMAND test_automation_authoring)
set_tests_properties(automation_authoring PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30)

# Synthetic lifecycle DLLs are test-only targets, never deployment inputs.
add_library(automation_lifecycle_old SHARED tests/fixtures/automation_lifecycle_fixture.cpp)
add_library(automation_lifecycle_new SHARED tests/fixtures/automation_lifecycle_fixture.cpp)
target_compile_definitions(automation_lifecycle_new PRIVATE LIFECYCLE_MARKER=100)
add_executable(test_automation_effect_runtime tests/test_automation_effect_runtime.cpp ${AUTOMATION_RULE_TEST_SOURCES})
target_link_libraries(test_automation_effect_runtime PRIVATE ${AUTOMATION_RULE_TEST_LIBRARIES})
add_dependencies(test_automation_effect_runtime automation_lifecycle_old automation_lifecycle_new abi_finished_only
    abi_opacity_only abi_no_lifecycle abi_lifecycle_unknown abi_lifecycle_no_revision)
add_test(NAME automation_effect_runtime COMMAND test_automation_effect_runtime $<TARGET_FILE_DIR:automation_lifecycle_old>)
set_tests_properties(automation_effect_runtime PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30)

find_program(AURA_NODE_EXECUTABLE node)
# Native production targets must not acquire a Node dependency from test fixtures.
if(AURA_NODE_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/frontend/node_modules/blockly")
set(STUDIO_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/studio_stage4")
add_custom_command(OUTPUT "${STUDIO_FIXTURE_DIR}/studio_old.cpp" "${STUDIO_FIXTURE_DIR}/studio_continuous.cpp" "${STUDIO_FIXTURE_DIR}/studio_one_shot.cpp"
    COMMAND "${AURA_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/frontend/tests/generate-publication-fixtures.mjs" "${STUDIO_FIXTURE_DIR}"
    DEPENDS frontend/tests/generate-publication-fixtures.mjs frontend/src/blockly/cppTranspiler.js frontend/src/blockly/sequenceCompiler.js frontend/src/blockly/publication.js)
foreach(studio_fixture studio_old studio_continuous studio_one_shot)
    add_library(${studio_fixture} SHARED "${STUDIO_FIXTURE_DIR}/${studio_fixture}.cpp")
endforeach()
add_library(automation_reload_old SHARED tests/fixtures/automation_lifecycle_fixture.cpp)
target_compile_definitions(automation_reload_old PRIVATE LIFECYCLE_FINISH=10000)
add_library(automation_reload_new SHARED tests/fixtures/automation_lifecycle_fixture.cpp)
target_compile_definitions(automation_reload_new PRIVATE LIFECYCLE_FINISH=10000 LIFECYCLE_MARKER=100)
add_executable(test_automation_reconciliation tests/test_automation_reconciliation.cpp ${AUTOMATION_RULE_TEST_SOURCES})
target_link_libraries(test_automation_reconciliation PRIVATE ${AUTOMATION_RULE_TEST_LIBRARIES})
add_dependencies(test_automation_reconciliation studio_old studio_continuous studio_one_shot automation_lifecycle_old automation_lifecycle_new abi_two abi_no_lifecycle automation_reload_old automation_reload_new)
add_test(NAME automation_reconciliation COMMAND test_automation_reconciliation $<TARGET_FILE_DIR:studio_old>)
set_tests_properties(automation_reconciliation PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30)
else()
    message(STATUS "Studio publication fixture tests unavailable: install Node and frontend dependencies to enable")
endif()
