# gemrecon2 - thin JANA adapter over the standalone gemreco library (MT plan W2-F)
get_filename_component(PLUGIN_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
print_header(">>>>   P L U G I N :   ${PLUGIN_NAME}    <<<<")

plugin_add(${PLUGIN_NAME})
plugin_glob_all(${PLUGIN_NAME})

plugin_link_libraries(${PLUGIN_NAME} gemreco)
