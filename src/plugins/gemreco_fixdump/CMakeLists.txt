# Fixture dumper for gemreco golden-parity tests (temporary tooling, see FixtureDumpProcessor.h)
get_filename_component(PLUGIN_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
print_header(">>>>   P L U G I N :   ${PLUGIN_NAME}    <<<<")

plugin_add(${PLUGIN_NAME})
plugin_glob_all(${PLUGIN_NAME})

# Needs GemMapping symbols (legacy class, lives in the gemrecon plugin which has no
# linkable library target) - compile the implementation in directly. Only used via
# the GemMappingService pointer, so the duplicate code copy is harmless.
target_sources(${PLUGIN_NAME}_plugin PRIVATE ../gemrecon/GemMapping.cc)
