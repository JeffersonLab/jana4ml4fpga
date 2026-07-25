get_filename_component(PLUGIN_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
print_header(">>>>   P L U G I N :   ${PLUGIN_NAME}    <<<<")

plugin_add(${PLUGIN_NAME})
plugin_glob_all(${PLUGIN_NAME})

# decode library + RNTuple output
plugin_link_libraries(${PLUGIN_NAME} evio_sro_parser ROOT::ROOTNTuple ROOT::RIO ROOT::Core)
