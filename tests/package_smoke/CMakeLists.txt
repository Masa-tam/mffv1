cmake_minimum_required(VERSION 3.25)

project(mffv1_package_smoke LANGUAGES CXX)

find_package(mffv1 CONFIG REQUIRED)

add_executable(mffv1_package_smoke main.cpp)
target_compile_features(mffv1_package_smoke PRIVATE cxx_std_20)
target_link_libraries(mffv1_package_smoke PRIVATE mffv1::mffv1)
