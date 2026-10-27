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
find_package(Drogon CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Embed the specification and UI so documentation works from any working directory.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/openapi.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/swagger.html")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/docs/openapi.json" OPENAPI_JSON)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/docs/swagger.html" SWAGGER_HTML)
configure_file(docs/assets.h.in generated/api_docs_assets.h @ONLY)

# Compile the implementation once, then reuse it in the server and tests.
add_library(booking-core STATIC
    src/api/screenings.cpp
    src/DB/queries.cpp
    src/DB/DB.cpp
    src/DataModel/Screening.cpp
    src/config/config.cpp
)
target_include_directories(booking-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
target_compile_features(booking-core PUBLIC cxx_std_20)
target_link_libraries(booking-core PUBLIC
    spdlog::spdlog
    JsonCpp::JsonCpp
    Threads::Threads
)

add_executable(movie-theater-booking src/main.cpp src/api/docs.cpp)
add_custom_target(copy-config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CMAKE_CURRENT_BINARY_DIR}/config"
    COMMENT "Copying runtime configuration to the build directory"
    VERBATIM
)
add_dependencies(movie-theater-booking copy-config)
target_include_directories(movie-theater-booking PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_target_properties(booking-core movie-theater-booking PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)
target_link_libraries(movie-theater-booking PRIVATE booking-core Drogon::Drogon)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(booking-core PUBLIC atomic)
endif()

foreach(_target IN ITEMS booking-core movie-theater-booking)
    if(MSVC)
        target_compile_options(${_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

include(GNUInstallDirs)
install(TARGETS movie-theater-booking RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    include(InstallRequiredSystemLibraries)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_executable(unit-tests tests/booking_test.cpp)
    target_link_libraries(unit-tests PRIVATE booking-core GTest::gtest_main Drogon::Drogon)
    set_target_properties(unit-tests PROPERTIES CXX_EXTENSIONS OFF)
    include(GoogleTest)
    gtest_discover_tests(unit-tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 15)
endif()
