cmake_minimum_required(VERSION 3.25)

project(mffv1_package_smoke LANGUAGES CXX)

find_package(mffv1 CONFIG REQUIRED)

set(MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES "1" CACHE STRING
    "Expected MFFV1_ENABLE_STATUS_MESSAGES value from installed mffv1 headers")

add_executable(mffv1_package_smoke main.cpp)
target_compile_features(mffv1_package_smoke PRIVATE cxx_std_20)
target_compile_definitions(mffv1_package_smoke
    PRIVATE
        MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES=${MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES}
)
target_link_libraries(mffv1_package_smoke PRIVATE mffv1::mffv1)

add_executable(mffv1_package_header_smoke header_main.cpp)
target_compile_features(mffv1_package_header_smoke PRIVATE cxx_std_20)
target_compile_definitions(mffv1_package_header_smoke
    PRIVATE
        MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES=${MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES}
)
target_link_libraries(mffv1_package_header_smoke PRIVATE mffv1::mffv1)
