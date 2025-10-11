# Packaging + install helpers moved here so deb/cmake packaging logic can be reused.
# This file expects to be included from the project root CMakeLists.txt where
# PROJECT_VERSION and GNUInstallDirs variables are already defined.

# install library and export targets
install(TARGETS boxmalloc
    EXPORT boxmallocTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# install public headers
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Export targets and generate CMake config package
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/boxmallocConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/BoxmallocConfig.cmake.in"
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
