cmake_minimum_required(VERSION 3.19)

project(JANA4ML4FPGA VERSION 0.0.0)

# Make C++17 a default
if(NOT "${CMAKE_CXX_STANDARD}")
    set(CMAKE_CXX_STANDARD 20)  # ROOT 6.40 and JANA2 2026.x are C++20
endif()
message(STATUS "CMAKE_CXX_STANDARD = ${CMAKE_CXX_STANDARD}")

# Enable -fPIC for all targets
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_BUILD_TYPE Debug)

# Find packages
# ROOT must be pre-installed (system package, conda or thisroot.sh)

include(cmake/vdt_stub.cmake)      # Stub for ROOT installations built with vdt when vdt is absent

find_package(ROOT REQUIRED)

# Fetch and build missing small dependencies (JANA2, spdlog, fmt) at configure time.
# Does nothing for dependencies that are already installed.
option(FETCH_DEPENDENCIES "Download and build missing dependencies (JANA2, spdlog, fmt) with FetchContent" ON)
if(FETCH_DEPENDENCIES)
    include(cmake/fetch_dependencies.cmake)
endif()

find_package(JANA REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# Install to the top directory by default
if( ${CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT} )
    set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install CACHE PATH "Install in top directory by default" FORCE)
endif()

set(PLUGIN_OUTPUT_DIRECTORY "${CMAKE_INSTALL_PREFIX}/plugins")
set(INCLUDE_OUTPUT_DIRECTORY "${CMAKE_INSTALL_PREFIX}/include")
set(LIBRARY_OUTPUT_DIRECTORY "${CMAKE_INSTALL_PREFIX}/lib")
set(PROGRAM_OUTPUT_DIRECTORY "${CMAKE_INSTALL_PREFIX}/bin")

message(STATUS "${CMAKE_PROJECT_NAME}: CMAKE_INSTALL_PREFIX    : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "${CMAKE_PROJECT_NAME}: PLUGIN_OUTPUT_DIRECTORY : ${PLUGIN_OUTPUT_DIRECTORY}")
message(STATUS "${CMAKE_PROJECT_NAME}: INCLUDE_OUTPUT_DIRECTORY: ${INCLUDE_OUTPUT_DIRECTORY}")
message(STATUS "${CMAKE_PROJECT_NAME}: LIBRARY_OUTPUT_DIRECTORY: ${LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "${CMAKE_PROJECT_NAME}: PROGRAM_OUTPUT_DIRECTORY: ${PROGRAM_OUTPUT_DIRECTORY}")

# JANA2
message(STATUS "${CMAKE_PROJECT_NAME}: JANA2 cmake   : ${JANA_DIR}")
message(STATUS "${CMAKE_PROJECT_NAME}: JANA2 includes: ${JANA_INCLUDE_DIR}")
message(STATUS "${CMAKE_PROJECT_NAME}: JANA2 library : ${JANA_LIBRARY}")

# CERN ROOT
message(STATUS "${CMAKE_PROJECT_NAME}: ROOT version  : ${ROOT_VERSION}")
message(STATUS "${CMAKE_PROJECT_NAME}: ROOT libraries: ${ROOT_LIBRARIES}")


# Add CMake additional functionality:
include(cmake/print_functions.cmake)                        # Helpers to print fancy headers, file names, etc
include(cmake/print_subdirectory_tree.cmake)                # Prints processed subdirectories
include(cmake/jana_plugin.cmake)                            # Add common settings for plugins
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)   # Find Find<Modules>.cmake


# Add src as a global include directory
include_directories(${CMAKE_SOURCE_DIR}/src)

# Libraries are a separated objects.
# We add this include directory so that in code we could use
# #include <tcp_daq/tcp_event.h> instead of <libraries/tcp_daq/tcp_event.h>
# For peer plugins the later is OK, but for libraries <tcp_daq/tcp_event.h> is better
include_directories(${CMAKE_SOURCE_DIR}/src/libraries)

#Add clang sanitizers
#if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#    # Enable sanitizers
#    message(STATUS "Clang sanitizers are enabled")
#    set(SANITIZE_FLAGS "-fsanitize=address,undefined")
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZE_FLAGS}")
#    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZE_FLAGS}")
#endif()

# set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -latomic")

# ---------------------------------------------------------------------------
print_grand_header("    B U I L D   J A N A 4 M L 4 F P G A   P A R T S    ")
# ---------------------------------------------------------------------------

add_subdirectory(src/libraries)
add_subdirectory(src/services)
add_subdirectory(src/plugins)
add_subdirectory(src/executables)
add_subdirectory(src/tests)

# Print what we had built
print_header("CMake processed subdirectories:")
print_subdirectory_tree()
message(STATUS "\n-------------------------------")


# Install all cmake helpers
# include(CMakePackageConfigHelpers)
# configure_package_config_file(cmake/EICreconConfig.cmake.in cmake/EICreconConfig.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/EICrecon)
# install(FILES ${CMAKE_BINARY_DIR}/cmake/EICreconConfig.cmake DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/EICrecon)  # why is this needed?
# file(GLOB EICRECON_CMAKE_FILES cmake/*.cmake)
# install(FILES ${EICRECON_CMAKE_FILES} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/EICrecon)

#---------------------------------------------------------------------------
# Install ml4fpga script
#---------------------------------------------------------------------------
# Set the path for your script
set(SCRIPT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/python/ml4fpga.py")

# Set the path where the script will be copied
set(SCRIPT_DST "${TARGET_BINARY_DIR}/ml4fpga")


# Set the target binary directory

# Copy the script during installation
install(FILES ${SCRIPT_SRC} DESTINATION ${PROGRAM_OUTPUT_DIRECTORY} RENAME ml4fpga )

# Set the permission to executable during installation
install(CODE "execute_process(COMMAND chmod +x ${PROGRAM_OUTPUT_DIRECTORY}/ml4fpga)")