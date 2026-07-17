# Downloads missing dependencies at configure time using FetchContent.
#
# ROOT is NOT handled here: it is a heavy dependency and must be pre-installed
# (system package, conda, or sourced thisroot.sh).
#
# JANA2 is built the same way the docker images build it (see docker/ml4fpga-pre):
#   -DUSE_ROOT=On -DUSE_PODIO=Off -DUSE_ZEROMQ=On
# Because this project consumes JANA2 through its installed JANAConfig.cmake
# (JANA_DIR, JANA_INCLUDE_DIR, JANA_LIB, ...), JANA2 is configured, built and
# installed into ${CMAKE_BINARY_DIR}/deps/jana2 at configure time, and then the
# regular find_package(JANA) works exactly as with a pre-installed JANA2.
#
# Everything is skipped for dependencies that find_package can already find,
# so pre-installed setups (e.g. gluon nodes, docker) are not affected.

include(FetchContent)

# ----------------------------------- fmt ------------------------------------
find_package(fmt QUIET)
if(fmt_FOUND)
    message(STATUS "${CMAKE_PROJECT_NAME}: fmt found: ${fmt_DIR}")
else()
    message(STATUS "${CMAKE_PROJECT_NAME}: fmt NOT found. Fetching it with FetchContent")
    FetchContent_Declare(fmt
            URL https://github.com/fmtlib/fmt/archive/refs/tags/10.2.1.tar.gz
            OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(fmt)
endif()

# ---------------------------------- spdlog ----------------------------------
find_package(spdlog QUIET)
if(spdlog_FOUND)
    message(STATUS "${CMAKE_PROJECT_NAME}: spdlog found: ${spdlog_DIR}")
else()
    message(STATUS "${CMAKE_PROJECT_NAME}: spdlog NOT found. Fetching it with FetchContent")
    set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "spdlog uses external fmt" FORCE)
    FetchContent_Declare(spdlog
            URL https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz
            OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(spdlog)
endif()

# ---------------------------------- JANA2 -----------------------------------
find_package(JANA QUIET)
if(JANA_FOUND)
    message(STATUS "${CMAKE_PROJECT_NAME}: JANA2 found: ${JANA_DIR}")
else()
    set(JANA2_VERSION "v2.1.2" CACHE STRING "JANA2 git tag to fetch and build")
    set(JANA2_SOURCE_DIR ${CMAKE_BINARY_DIR}/deps/jana2-src)
    set(JANA2_BUILD_DIR ${CMAKE_BINARY_DIR}/deps/jana2-build)
    set(JANA2_INSTALL_DIR ${CMAKE_BINARY_DIR}/deps/jana2)

    message(STATUS "${CMAKE_PROJECT_NAME}: JANA2 NOT found. Fetching ${JANA2_VERSION} and building into ${JANA2_INSTALL_DIR}")

    # Download at configure time (direct form of FetchContent_Populate: download only,
    # no add_subdirectory - JANA2 must be *installed* for its JANAConfig.cmake to work)
    FetchContent_Populate(jana2_download
            GIT_REPOSITORY https://github.com/JeffersonLab/JANA2.git
            GIT_TAG ${JANA2_VERSION}
            GIT_SHALLOW TRUE
            SOURCE_DIR ${JANA2_SOURCE_DIR}
            BINARY_DIR ${CMAKE_BINARY_DIR}/deps/jana2-populate-bin
            SUBBUILD_DIR ${CMAKE_BINARY_DIR}/deps/jana2-populate-subbuild)

    # Patch: rootcling of ROOT >= 6.32 fails to generate dictionaries of the
    # janaview (GUI debugging) and JTestRoot (test) plugins. Neither is needed
    # by this project, so drop them from the JANA2 build.
    file(READ ${JANA2_SOURCE_DIR}/src/plugins/CMakeLists.txt _jana2_plugins_cml)
    if(NOT _jana2_plugins_cml MATCHES "disabled by jana4ml4fpga")
        string(REPLACE "add_subdirectory(janaview)"
                "# add_subdirectory(janaview)  # disabled by jana4ml4fpga (rootcling incompatibility)"
                _jana2_plugins_cml "${_jana2_plugins_cml}")
        string(REPLACE "add_subdirectory(JTestRoot)"
                "# add_subdirectory(JTestRoot)  # disabled by jana4ml4fpga (rootcling incompatibility)"
                _jana2_plugins_cml "${_jana2_plugins_cml}")
        file(WRITE ${JANA2_SOURCE_DIR}/src/plugins/CMakeLists.txt "${_jana2_plugins_cml}")
    endif()

    # Configure, build and install JANA2 (only once; skipped if already installed)
    if(NOT EXISTS ${JANA2_INSTALL_DIR}/lib/JANA/cmake/JANAConfig.cmake
       AND NOT EXISTS ${JANA2_INSTALL_DIR}/lib/cmake/JANA/JANAConfig.cmake)

        execute_process(
                COMMAND ${CMAKE_COMMAND}
                    -S ${JANA2_SOURCE_DIR}
                    -B ${JANA2_BUILD_DIR}
                    -DCMAKE_INSTALL_PREFIX=${JANA2_INSTALL_DIR}
                    -DCMAKE_BUILD_TYPE=Release
                    -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
                    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
                    -DCMAKE_PROJECT_INCLUDE=${CMAKE_SOURCE_DIR}/cmake/vdt_stub.cmake
                    -DUSE_ROOT=On
                    -DUSE_PODIO=Off
                    -DUSE_ZEROMQ=On
                    -DUSE_PYTHON=Off
                RESULT_VARIABLE JANA2_CONFIGURE_RESULT)
        if(JANA2_CONFIGURE_RESULT)
            message(FATAL_ERROR "JANA2 configure step failed (see errors above)")
        endif()

        include(ProcessorCount)
        ProcessorCount(NPROC)
        if(NPROC EQUAL 0)
            set(NPROC 4)
        endif()

        execute_process(
                COMMAND ${CMAKE_COMMAND} --build ${JANA2_BUILD_DIR} --parallel ${NPROC}
                RESULT_VARIABLE JANA2_BUILD_RESULT)
        if(JANA2_BUILD_RESULT)
            message(FATAL_ERROR "JANA2 build step failed (see errors above)")
        endif()

        execute_process(
                COMMAND ${CMAKE_COMMAND} --install ${JANA2_BUILD_DIR}
                RESULT_VARIABLE JANA2_INSTALL_RESULT)
        if(JANA2_INSTALL_RESULT)
            message(FATAL_ERROR "JANA2 install step failed (see errors above)")
        endif()
    endif()

    find_package(JANA REQUIRED HINTS ${JANA2_INSTALL_DIR})
    message(STATUS "${CMAKE_PROJECT_NAME}: JANA2 built and found: ${JANA_DIR}")
endif()
