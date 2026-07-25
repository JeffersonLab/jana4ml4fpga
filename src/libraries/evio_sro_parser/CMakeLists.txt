project(lib_evio_sro_parser)

print_header(">>>>   L I B R A R Y :   evio_sro_parser    <<<<")

# Standalone SRO decode library: no JANA, no ROOT dependencies on purpose, so the
# sro_dump validation tool and unit checks build without the framework.
set(lib_evio_sro_parser_SRC
        SroData.h
        SroBlockReader.cc
        SroBlockReader.h
        SroFrameSetParser.cc
        SroFrameSetParser.h
        SroTranslationTables.cc
        SroTranslationTables.h
        )

set(LIBRARY_NAME evio_sro_parser)

add_library(${LIBRARY_NAME} STATIC ${lib_evio_sro_parser_SRC})
set_target_properties(${LIBRARY_NAME} PROPERTIES PREFIX "lib" OUTPUT_NAME "evio_sro_parser")
target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_SOURCE_DIR})

install(TARGETS ${LIBRARY_NAME} DESTINATION ${LIBRARY_OUTPUT_DIRECTORY})

# Decoder check tool (see sro_dump.cc header for usage)
add_executable(sro_dump sro_dump.cc)
target_link_libraries(sro_dump ${LIBRARY_NAME})
install(TARGETS sro_dump DESTINATION bin)
