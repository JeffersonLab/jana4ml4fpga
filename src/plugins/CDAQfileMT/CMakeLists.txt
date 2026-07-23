# CDAQfileMT - EVIO file source with runtime-switchable parallel parsing
# (see CDAQfileMT.cc header comment; serial CDAQfile plugin remains the fallback)
get_filename_component(PLUGIN_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
print_header(">>>>   P L U G I N :   ${PLUGIN_NAME}    <<<<")

plugin_add(${PLUGIN_NAME})
plugin_glob_all(${PLUGIN_NAME})

# swap_bank + parser symbols
plugin_link_libraries(${PLUGIN_NAME} rawdataparser)
