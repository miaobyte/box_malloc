cmake_minimum_required(VERSION 3.10)
project(box_malloc)

set(CMAKE_C_STANDARD 11)

# 添加头文件路径
include_directories(include)
include_directories(src)

# 使用 FetchContent 下载和引入 block_malloc
include(FetchContent)
FetchContent_Declare(
    block_malloc
    GIT_REPOSITORY https://github.com/miaobyte/block_malloc.git
    GIT_TAG main # 替换为具体的分支或标签
)
FetchContent_MakeAvailable(block_malloc)

# 添加源文件
add_library(box_malloc SHARED
    src/box_malloc.c
)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(box_malloc PRIVATE ENABLE_LOG) # 在 Debug 模式下定义 ENABLE_LOG
endif()
target_link_libraries(box_malloc block_malloc)

add_subdirectory(test)

# 安装目标和头文件
install(TARGETS box_malloc DESTINATION lib/box_malloc)
install(DIRECTORY include/ DESTINATION include/box_malloc)