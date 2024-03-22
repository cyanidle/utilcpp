cmake_minimum_required(VERSION 3.16)
project(utilcpp LANGUAGES CXX)

add_library(utilcpp INTERFACE)
add_library(utilcpp::utilcpp ALIAS utilcpp)
target_include_directories(utilcpp INTERFACE include)
