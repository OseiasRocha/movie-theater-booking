cmake_minimum_required(VERSION 3.23)

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

project(cpp_template VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "${CPP_CONAN_BUILD_TYPE}" CACHE STRING "Build type" FORCE)
endif()

find_package(spdlog CONFIG REQUIRED)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(cpp_template src/main.cpp)
target_compile_features(cpp_template PRIVATE cxx_std_20)
set_target_properties(cpp_template PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)
target_link_libraries(cpp_template PRIVATE spdlog::spdlog)

if(MSVC)
    target_compile_options(cpp_template PRIVATE /W4 /permissive-)
else()
    target_compile_options(cpp_template PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS cpp_template RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
