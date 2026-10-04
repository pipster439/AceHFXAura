cmake_minimum_required(VERSION 3.20)
project(AuraEnumerationCharacterizer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_readonly_abi.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/AuraWorker/CanonicalAuraAbi.h"
  "${CMAKE_CURRENT_BINARY_DIR}/ReadOnlyAuraAbi.h" COMMAND_ERROR_IS_FATAL ANY)
add_executable(AuraEnumerationCharacterizer Characterizer.cpp)
target_include_directories(AuraEnumerationCharacterizer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" ../../include)
target_compile_definitions(AuraEnumerationCharacterizer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(AuraEnumerationCharacterizer PRIVATE /W4 /WX /utf-8)
target_link_libraries(AuraEnumerationCharacterizer PRIVATE ole32 oleaut32 advapi32 bcrypt version runtimeobject)
