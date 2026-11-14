cmake_minimum_required(VERSION 3.16...3.31)
project(Astra VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Required native backends for HTTP/TLS and compression standard modules.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(CURL_PC QUIET IMPORTED_TARGET libcurl)
endif()
if(TARGET PkgConfig::CURL_PC)
  set(ASTRA_CURL_TARGET PkgConfig::CURL_PC)
else()
  find_package(CURL REQUIRED)
  set(ASTRA_CURL_TARGET CURL::libcurl)
endif()
find_package(ZLIB REQUIRED)
add_executable(astra
  src/cli/main.cpp
  src/cli/updater.cpp
  src/compiler/lexer.cpp
  src/compiler/checker.cpp
  src/runtime/modules.cpp
  src/runtime/value.cpp
  src/runtime/bigint.cpp
  src/runtime/crypto.cpp
  src/runtime/interpreter.cpp
  src/runtime/stdlib/stdlib.cpp
  src/runtime/stdlib/launch.cpp
  src/runtime/stdlib/events.cpp
  src/runtime/stdlib/text_tools.cpp
  src/runtime/stdlib/compress.cpp
  src/runtime/stdlib/web_tools.cpp
  src/runtime/stdlib/collections.cpp)
target_sources(astra PRIVATE src/runtime/stdlib/server.cpp src/runtime/stdlib/network.cpp)
target_include_directories(astra PRIVATE include)
target_compile_definitions(astra PRIVATE ASTRA_VERSION=\"${PROJECT_VERSION}\")
target_link_libraries(astra PRIVATE ${ASTRA_CURL_TARGET} ZLIB::ZLIB)
if(WIN32)
  target_link_libraries(astra PRIVATE ws2_32)
endif()
if(MSVC)
  target_compile_options(astra PRIVATE /W4 /permissive-)
else()
  target_compile_options(astra PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
add_test(NAME version COMMAND astra --version)
add_test(NAME version_command COMMAND astra version)
add_test(NAME help_has_install COMMAND astra --help)
add_test(NAME modules COMMAND astra modules)
add_test(NAME check_hello COMMAND astra check ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.astra)
add_test(NAME run_hello COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.astra)
add_test(NAME run_stdlib COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/stdlib.astra)
add_test(NAME run_data_tools_example COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/examples/data_tools.astra)
add_test(NAME web_standard_modules COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/web-tools.astra)
add_test(NAME json_csv_hardening COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/json-csv-hardening.astra)
add_test(NAME collections_module COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/collections.astra)
add_test(NAME all_builtin_modules COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/all-modules.astra)
add_test(NAME dns_network_module COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/network.astra)
add_test(NAME run_catch COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/catch.astra)
add_test(NAME run_language COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/language.astra)
add_test(NAME run_closures COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/closures.astra)
add_test(NAME arrow_global_shadowing COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/arrows-global.astra)
add_test(NAME arrow_const_override COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/arrow-const-override.astra)
add_test(NAME global_builtin_static_check COMMAND astra check ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/global-good.astra)
add_test(NAME global_builtin_unknown_member COMMAND astra check ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/global-bad-member.astra)
set_tests_properties(global_builtin_unknown_member PROPERTIES WILL_FAIL TRUE)
add_test(NAME global_module_readonly COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/global-module-readonly.astra)
set_tests_properties(global_module_readonly PROPERTIES WILL_FAIL TRUE)
add_test(NAME global_module_index_readonly COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/global-module-index-readonly.astra)
set_tests_properties(global_module_index_readonly PROPERTIES WILL_FAIL TRUE)
add_test(NAME async_not_silently_sync COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/async-explicit-error.astra)
set_tests_properties(async_not_silently_sync PROPERTIES WILL_FAIL TRUE)
add_test(NAME await_top_level_requires_sync COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/await-outside-sync.astra)
set_tests_properties(await_top_level_requires_sync PROPERTIES WILL_FAIL TRUE)
add_test(NAME await_normal_function_requires_sync COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/await-inside-normal.astra)
set_tests_properties(await_normal_function_requires_sync PROPERTIES WILL_FAIL TRUE)
add_test(NAME sync_function_await COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/sync-await.astra)
add_test(NAME runtime_overflow COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/errors.astra)
set_tests_properties(runtime_overflow PROPERTIES WILL_FAIL TRUE)
add_test(NAME unknown_module_member COMMAND astra run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/bad-module.astra)
set_tests_properties(unknown_module_member PROPERTIES WILL_FAIL TRUE)
add_test(NAME run_project_manifest COMMAND astra run)
set_tests_properties(run_project_manifest PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME local_pack_manifest_cli
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_pack_versions_test.py
      $<TARGET_FILE:astra>
      ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME local_http_integration
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_launch_test.py
      $<TARGET_FILE:astra>
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/launch.astra)
  add_test(NAME local_http_server_integration
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_server_test.py
      $<TARGET_FILE:astra>
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/server.astra)
  add_test(NAME local_tcp_client_integration
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tcp_test.py
      $<TARGET_FILE:astra>)
  add_test(NAME global_init_installation
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_global_init_test.py
      $<TARGET_FILE:astra>
      ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(Python3_Interpreter_FOUND)
  find_program(ASTRA_OPENSSL_EXECUTABLE openssl)
  if(ASTRA_OPENSSL_EXECUTABLE)
    add_test(NAME local_tls_verification
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tls_test.py
        $<TARGET_FILE:astra>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/tls.astra
        ${ASTRA_OPENSSL_EXECUTABLE})
  endif()
endif()

if(Python3_Interpreter_FOUND)
  add_test(NAME minimal_project_init COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_init_test.py $<TARGET_FILE:astra>)
  add_test(NAME root_pack_manifest COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/validate-pack.py)
endif()
