add_executable(boxmalloc_simple 1_boxmalloc_simple.c)
target_link_libraries(boxmalloc_simple boxmalloc)

add_executable(box_bench_simple 2_box_bench_simple.c)
target_link_libraries(box_bench_simple boxmalloc)

add_executable(boxmalloc_max 3_boxmalloc_max.c)
target_link_libraries(boxmalloc_max boxmalloc)


add_test(NAME boxmalloc_simple COMMAND boxmalloc_simple)
add_test(NAME box_bench_simple COMMAND box_bench_simple)
add_test(NAME boxmalloc_max COMMAND boxmalloc_max)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(boxmalloc_simple PRIVATE ENABLE_LOG)
    target_compile_definitions(box_bench_simple PRIVATE ENABLE_LOG)
    target_compile_definitions(boxmalloc_max PRIVATE ENABLE_LOG)
endif()