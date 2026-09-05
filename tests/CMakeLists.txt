cmake_minimum_required(VERSION 3.14)
project(GP2040HostTests LANGUAGES CXX)

enable_testing()

add_executable(he_trigger_test he_trigger_test.cpp)
target_compile_features(he_trigger_test PRIVATE cxx_std_17)
target_include_directories(he_trigger_test PRIVATE ../headers)
target_compile_options(he_trigger_test PRIVATE -Wall -Wextra -Werror)

add_test(NAME he_trigger_test COMMAND he_trigger_test)
