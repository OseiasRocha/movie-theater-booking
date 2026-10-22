cmake_minimum_required(VERSION 3.23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Load Conan's dependency paths before project() initializes the toolchain.
# Respect an explicitly supplied toolchain for other build environments.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_conan_toolchain "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")
    if(NOT EXISTS "${_conan_toolchain}")
        message(FATAL_ERROR
            "Conan dependencies are missing. From the project root, run: "
            "conan install . --build=missing")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${_conan_toolchain}" CACHE FILEPATH "Conan toolchain")
endif()

project(movie-theater-booking VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "${CPP_CONAN_BUILD_TYPE}" CACHE STRING "Build type" FORCE)
endif()

find_package(spdlog CONFIG REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(movie-theater-booking src/main.cpp)
target_compile_features(movie-theater-booking PRIVATE cxx_std_20)
set_target_properties(movie-theater-booking PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)
target_link_libraries(movie-theater-booking PRIVATE spdlog::spdlog)
target_link_libraries(movie-theater-booking PRIVATE Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(movie-theater-booking PUBLIC atomic)
endif()

if(MSVC)
    target_compile_options(movie-theater-booking PRIVATE /W4 /permissive-)
else()
    target_compile_options(movie-theater-booking PRIVATE -Wall -Wextra -Wpedantic)
endif()

# include(GNUInstallDirs)
# install(TARGETS movie-theater-booking RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_executable(unit-tests tests/booking_test.cpp src/main.cpp)
    target_include_directories(unit-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(unit-tests PRIVATE MOVIE_THEATER_BOOKING_NO_MAIN)
    target_compile_features(unit-tests PRIVATE cxx_std_20)
    target_link_libraries(unit-tests PRIVATE GTest::gtest_main Threads::Threads)
    set_target_properties(unit-tests PROPERTIES CXX_EXTENSIONS OFF)
    include(GoogleTest)
    gtest_discover_tests(unit-tests PROPERTIES TIMEOUT 15)
endif()
