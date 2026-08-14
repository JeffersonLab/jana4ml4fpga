# SRO stream test tools. Also buildable without CMake via the Makefile in
# this directory (the deployment target is a RHEL machine with gcc 4.8 where
# only these two tools are needed, not the full project).

add_executable(sro_send sro_send.cc sro_stream_common.h)
add_executable(sro_recv sro_recv.cc sro_stream_common.h)

foreach(tool sro_send sro_recv)
    # Keep the tools honest about the gcc 4.8 deployment target
    set_target_properties(${tool} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(${tool} pthread)
    set_target_properties(${tool} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    install(TARGETS ${tool} DESTINATION bin)
endforeach()
