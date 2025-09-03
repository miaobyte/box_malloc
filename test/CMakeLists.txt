add_executable(test_box_alloc box_malloc_simple.c)
target_link_libraries(test_box_alloc box_malloc)

add_executable(test_box_alloc_bench box_bench_simple.c)
target_link_libraries(test_box_alloc_bench box_malloc)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(test_box_alloc PRIVATE ENABLE_LOG)
    target_compile_definitions(test_box_alloc_bench PRIVATE ENABLE_LOG)
endif()