cmake_minimum_required(VERSION 3.10)
project(box_malloc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

# headers are installed from include/
# find block_malloc (installed by its CMake package)
find_package(block_malloc CONFIG REQUIRED)

# library
add_library(box_malloc SHARED
    src/box_malloc.c
)

# version / soname
set_target_properties(box_malloc PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# include paths: public for users, private for build
target_include_directories(box_malloc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(box_malloc PRIVATE ENABLE_LOG)
endif()

# Link to block_malloc exported target (no pkg-config)
target_link_libraries(box_malloc PRIVATE block_malloc::block_malloc)

add_subdirectory(test)

# install
install(TARGETS box_malloc
    EXPORT box_mallocTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Export targets and generate CMake config package
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/box_mallocConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/BoxMallocConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/box_mallocConfig.cmake"
    @ONLY
)

install(EXPORT box_mallocTargets
    FILE box_mallocTargets.cmake
    NAMESPACE box_malloc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/box_malloc-${PROJECT_VERSION}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/box_mallocConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/box_mallocConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/box_malloc-${PROJECT_VERSION})