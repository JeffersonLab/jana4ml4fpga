# gemreco - standalone MT-safe GEM reconstruction library.
# HARD RULE: no JANA, no ROOT dependencies in this directory.
# Allowed deps: STL and the header-only extmath (src/libraries/extmath).
print_header(">>>>   L I B R A R Y :   gemreco    <<<<")

add_library(gemreco STATIC
        config_csv.cpp
        config_loader.cpp
        steps/decode_apv.cpp
        steps/apv_decode.cpp
        steps/plane_merge.cpp
        steps/peak_find.cpp
        steps/cluster_match.cpp
        steps/rolling_baseline.cpp
        reconstruct.cpp
        )

target_include_directories(gemreco PUBLIC ${CMAKE_SOURCE_DIR}/src/libraries)
target_compile_features(gemreco PUBLIC cxx_std_17)
set_target_properties(gemreco PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Guard: fail the build if ROOT or JANA headers sneak in
# (grep-based check runs as a test-time script; kept simple here by convention + review)
