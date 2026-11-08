# Vendored MIT-licensed fstapi (see VENDOR.txt). Compiled without lvdsim warning flags.
add_library(fstapi STATIC fstapi.c fastlz.c lz4.c)
target_include_directories(fstapi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(fstapi PRIVATE _GNU_SOURCE)
target_link_libraries(fstapi PUBLIC zlibstatic)
target_include_directories(fstapi PUBLIC "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
add_library(lvd::fstapi ALIAS fstapi)
