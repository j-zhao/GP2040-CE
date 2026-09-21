cmake_minimum_required(VERSION 3.14)
project(GP2040HostTests LANGUAGES CXX)

enable_testing()

add_executable(he_trigger_test he_trigger_test.cpp)
target_compile_features(he_trigger_test PRIVATE cxx_std_17)
target_include_directories(he_trigger_test PRIVATE ../headers)
target_compile_options(he_trigger_test PRIVATE -Wall -Wextra -Werror)

add_test(NAME he_trigger_test COMMAND he_trigger_test)

find_package(Threads REQUIRED)
add_executable(display_snapshot_test display_snapshot_test.cpp)
target_compile_features(display_snapshot_test PRIVATE cxx_std_17)
target_include_directories(display_snapshot_test PRIVATE ../headers)
target_compile_options(display_snapshot_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(display_snapshot_test PRIVATE Threads::Threads)
add_test(NAME display_snapshot_test COMMAND display_snapshot_test)
set_tests_properties(display_snapshot_test PROPERTIES TIMEOUT 30)
