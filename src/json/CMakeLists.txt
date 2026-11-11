# Strict, bounded JSON over the vendored jsmn tokenizer (MIT, third_party/jsmn/LICENSE): the
# server directory and plugin manifests.
add_library(proteus_json STATIC boat_json.c boat_json.h)
target_include_directories(proteus_json PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(proteus_json PRIVATE ${PROTEUS_WARNINGS})
