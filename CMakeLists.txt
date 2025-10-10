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
set_target_properties(boxmalloc PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# include paths: public for users, private for build
target_include_directories(boxmalloc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(boxmalloc PRIVATE ENABLE_LOG)
endif()

## Link to blockmalloc: prefer the imported namespaced target, fall back to in-tree target.
if(TARGET blockmalloc::blockmalloc)
    target_link_libraries(boxmalloc PRIVATE blockmalloc::blockmalloc)
elseif(TARGET blockmalloc)
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

# install
install(TARGETS boxmalloc
    EXPORT boxmallocTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Export targets and generate CMake config package
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/boxmallocConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/BoxmallocConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/boxmallocConfig.cmake"
    @ONLY
)

install(EXPORT boxmallocTargets
    FILE boxmallocTargets.cmake
    NAMESPACE boxmalloc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/boxmalloc-${PROJECT_VERSION}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/boxmallocConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/boxmallocConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/boxmalloc-${PROJECT_VERSION})