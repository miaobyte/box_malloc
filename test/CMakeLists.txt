add_executable(test_box_alloc box.c)
target_link_libraries(test_box_alloc box_malloc)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(test_box_alloc PRIVATE ENABLE_LOG)
endif()