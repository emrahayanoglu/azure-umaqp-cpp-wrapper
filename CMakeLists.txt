cmake_minimum_required(VERSION 3.12)
project(azure_uamqp_cpp_wrapper)

include(CheckCXXCompilerFlag)
set(CMAKE_CXX_STANDARD 17)

find_package(azure_c_shared_utility REQUIRED CONFIG)
find_package(azure_macro_utils_c REQUIRED CONFIG)
find_package(uamqp REQUIRED CONFIG)

include_directories("/usr/include")
include_directories("/usr/include")
include_directories("/usr/include/azureiot")

add_executable(azure_uamqp_cpp_wrapper main.cc
        AzureAmqpWsWrapper.cc
        AzureAmqpWsWrapper.h)

target_link_libraries(azure_uamqp_cpp_wrapper
        uamqp aziotsharedutil)
