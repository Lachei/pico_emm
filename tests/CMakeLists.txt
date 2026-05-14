cmake_minimum_required(VERSION 3.16)

project(tests)

add_executable(emm-test emm_test.cpp ../src/emm.cpp)
target_include_directories(emm-test PRIVATE ../configs ../include include)
set_property(TARGET emm-test PROPERTY CXX_STANDARD 23)

