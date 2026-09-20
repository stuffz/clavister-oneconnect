# One executable per tst_*.cpp, each its own CTest case, laid out to mirror the
# headers they cover: tests/core/tst_redactor.cpp covers src/core/redactor.hpp.
#
# Everything under src/ is header-only, so a test compiles the header it covers
# and nothing else. A standalone project rather than a subdirectory: the
# binaries build with the Makefile, and this must not suggest otherwise.
#
# AUTOMOC is what QTest needs: every test declares a Q_OBJECT class and includes
# the moc output at the end of its own file.

cmake_minimum_required(VERSION 3.22)
project(clavister-oneconnect-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Test)
find_package(PkgConfig REQUIRED)
# vpnc_environment.hpp reads struct oc_ip_info; the header is all it needs.
pkg_check_modules(OPENCONNECT REQUIRED IMPORTED_TARGET openconnect)

enable_testing()

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

set(CORE_TESTS dtls_watchdog ip4 redactor vpnc_environment)
set(PLATFORM_TESTS tun_protocol)

function(add_unit_test directory name)
    add_executable(tst_${name} "${directory}/tst_${name}.cpp")
    target_include_directories(tst_${name} PRIVATE "${SRC}" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_${name} PRIVATE Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wshadow)
    # Named for where it sits, so `ctest -R core` runs one layer of them.
    add_test(NAME ${directory}/${name} COMMAND tst_${name})
endfunction()

foreach(test IN LISTS CORE_TESTS)
    add_unit_test(core ${test})
endforeach()

foreach(test IN LISTS PLATFORM_TESTS)
    add_unit_test(platform ${test})
endforeach()

target_link_libraries(tst_vpnc_environment PRIVATE PkgConfig::OPENCONNECT)
