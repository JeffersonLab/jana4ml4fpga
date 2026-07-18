# gemreco unit + golden-parity tests (Catch2, amalgamated copy reused from algorithms_test).
# Run:  GEMRECO_FIXTURES=<dir with fixdump CSVs> ./gemreco_test
get_filename_component(DIRECTORY_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
print_header(">>>>   U N I T   T E S T S :   ${DIRECTORY_NAME}    <<<<")

add_executable(${DIRECTORY_NAME}
        ../algorithms_test/catch_amalgamated.cpp
        decode_apv_tests.cpp
        pedestal_tests.cpp
        apv_decode_tests.cpp
        plane_merge_tests.cpp
        peak_find_tests.cpp
        cluster_match_tests.cpp
        rolling_baseline_tests.cpp
        determinism_tests.cpp
        config_loader_tests.cpp
        )

target_include_directories(${DIRECTORY_NAME} PRIVATE ../algorithms_test)
target_link_libraries(${DIRECTORY_NAME} PRIVATE gemreco)

# MT scaling benchmark (separate binary, not part of the Catch2 suite)
add_executable(gemreco_bench gemreco_bench.cpp)
target_link_libraries(gemreco_bench PRIVATE gemreco)
find_package(Threads REQUIRED)
target_link_libraries(${DIRECTORY_NAME} PRIVATE Threads::Threads)
target_link_libraries(gemreco_bench PRIVATE Threads::Threads)
