cmake_minimum_required(VERSION 3.10)
project(boxmalloc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)
include(FetchContent)
find_package(blockmalloc CONFIG QUIET)

# library
add_library(boxmalloc SHARED
    src/boxmalloc.c
)

# version / soname
# 说明：
# - VERSION 指定库的完整版本号（例如 1.2.3），用于生成安装文件名如 libboxmalloc.so.1.2.3。
# - SOVERSION 指定“SONAME”的主版本（ABI 版本）。链接器会把 SONAME 设为 lib<name>.so.<SOVERSION>（例如 libboxmalloc.so.1）。
# - 当库的向后兼容性（公共 ABI）发生不兼容变化时，应递增 SOVERSION；小的修复/新增不改变 SOVERSION。
# 这样系统上可以同时保留不同 SOVERSION 的库，并通过 SONAME 做运行时兼容性匹配。
set_target_properties(boxmalloc PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# include paths: public for users, private for build
target_include_directories(boxmalloc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(boxmalloc PRIVATE ENABLE_LOG)
endif()

## Link to blockmalloc: prefer the imported namespaced target, fall back to in-tree target.
if(TARGET blockmalloc::blockmalloc)
    message(STATUS "Found blockmalloc via find_package; linking to blockmalloc::blockmalloc")
    target_link_libraries(boxmalloc PRIVATE blockmalloc::blockmalloc)
elseif(TARGET blockmalloc)
    message(STATUS "Found blockmalloc in-tree; linking to blockmalloc")
    target_link_libraries(boxmalloc PRIVATE blockmalloc)
else()
    message(STATUS "blockmalloc not found via find_package or in-tree; attempting FetchContent download")
    FetchContent_Declare(
        blockmalloc
        GIT_REPOSITORY https://github.com/miaobyte/blockmalloc.git
        GIT_TAG        main
    )
    FetchContent_MakeAvailable(blockmalloc)
    if(TARGET blockmalloc::blockmalloc)
        target_link_libraries(boxmalloc PRIVATE blockmalloc::blockmalloc)
    elseif(TARGET blockmalloc)
        target_link_libraries(boxmalloc PRIVATE blockmalloc)
    else()
        message(FATAL_ERROR "Could not locate blockmalloc target after FetchContent. Please install blockmalloc or adjust the dependency.")
    endif()
endif()

add_subdirectory(test)

# 将安装/打包相关配置委托到 cmake/packagex.cmake 以便复用和打包脚本共享
include("${CMAKE_CURRENT_LIST_DIR}/cmake/packagex.cmake")