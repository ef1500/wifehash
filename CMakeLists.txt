cmake_minimum_required(VERSION 3.14)

project(wifehash
    VERSION      1.0.0
    DESCRIPTION  "128-bit non-cryptographic hash with AVX2 Support"
    HOMEPAGE_URL "https://github.com/ef1500/wifehash"
    LANGUAGES    C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(wifehash INTERFACE)
add_library(wifehash::wifehash ALIAS wifehash)

target_include_directories(wifehash INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(FILES wifehash.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS wifehash
        EXPORT wifehashTargets)

install(EXPORT wifehashTargets
        FILE      wifehashTargets.cmake
        NAMESPACE wifehash::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wifehash)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/wifehashConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/wifehashConfig.cmake"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/wifehashTargets.cmake\")\n")

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/wifehashConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/wifehashConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wifehash)
