cmake_minimum_required(VERSION 3.12)
project(azure_uamqp_cpp_wrapper)

include(CheckCXXCompilerFlag)
set(CMAKE_CXX_STANDARD 17)

find_library(AzureSharedUtility libaziotsharedutil.a)
find_library(AzureUAmqp libuamqp.a)
find_library(cryptopp_path libcrypto++.a)

include_directories("/usr/include")
include_directories("/usr/local/include")
include_directories("/usr/local/include/azureiot")

add_executable(azure_uamqp_cpp_wrapper main.cpp
        AzureAmqpWsWrapper.cc
        AzureAmqpWsWrapper.h)

target_link_libraries(azure_uamqp_cpp_wrapper
        "-lssl3" "-lcrypto" "-lcurl"
        ${AzureSharedUtility}
        ${AzureUAmqp})
