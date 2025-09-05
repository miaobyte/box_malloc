add_executable(box_malloc_simple 1_box_malloc_simple.c)
target_link_libraries(box_malloc_simple box_malloc)

add_executable(box_bench_simple 2_box_bench_simple.c)
target_link_libraries(box_bench_simple box_malloc)

add_executable(box_malloc_max 3_box_malloc_max.c)
target_link_libraries(box_malloc_max box_malloc)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(box_malloc_simple PRIVATE ENABLE_LOG)
    target_compile_definitions(box_bench_simple PRIVATE ENABLE_LOG)
    target_compile_definitions(box_malloc_max PRIVATE ENABLE_LOG)
endif()