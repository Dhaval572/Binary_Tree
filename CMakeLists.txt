cmake_minimum_required(VERSION 3.25)

project(BinaryTree
    VERSION 0.1.0
    DESCRIPTION "A modular C++20 binary tree library"
    LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

option(BINARY_TREE_BUILD_TESTS
    "Build unit tests"
    ON
)

option(BINARY_TREE_BUILD_BENCHMARKS
    "Build benchmarks"
    ON
)

option(BINARY_TREE_ENABLE_SANITIZERS
    "Enable sanitizers in Debug builds"
    ON
)

option(BINARY_TREE_BUILD_DOCS
    "Build API documentation target"
    ON
)

# Main library (header-only)
add_library(binary_tree INTERFACE)
add_library(BinaryTree::binary_tree ALIAS binary_tree)

target_compile_features(binary_tree
    INTERFACE
        cxx_std_20
)

target_include_directories(binary_tree
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Compiler warnings
function(binary_tree_enable_warnings target)
  if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(${target} PRIVATE
            /W4
            /WX
            /permissive-
        )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        )
  endif()
endfunction()

# Sanitizers (Debug only)
function(binary_tree_enable_sanitizers target)

  if(NOT BINARY_TREE_ENABLE_SANITIZERS)
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"
       AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )

    target_link_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
  elseif(MSVC OR
           CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")

    # MSVC supports AddressSanitizer, but not UBSan.
    target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:/fsanitize=address>
        )
  endif()

endfunction()

# Tests - doctest
if(BINARY_TREE_BUILD_TESTS)

  find_package(doctest CONFIG REQUIRED)

  add_executable(binary_tree_tests
        tests/test_main.cpp
    )

  target_link_libraries(binary_tree_tests
        PRIVATE
            BinaryTree::binary_tree
            doctest::doctest
    )

  binary_tree_enable_warnings(binary_tree_tests)
  binary_tree_enable_sanitizers(binary_tree_tests)

  add_test(
        NAME binary_tree_tests
        COMMAND binary_tree_tests
    )

endif()

# Benchmarks - Google Benchmark
if(BINARY_TREE_BUILD_BENCHMARKS)

  find_package(benchmark CONFIG REQUIRED)

  add_executable(binary_tree_benchmarks
        benchmarks/tree_benchmarks.cpp
    )

  target_link_libraries(binary_tree_benchmarks
        PRIVATE
            BinaryTree::binary_tree
            benchmark::benchmark
            benchmark::benchmark_main
    )

  binary_tree_enable_warnings(binary_tree_benchmarks)
  binary_tree_enable_sanitizers(binary_tree_benchmarks)

  # Benchmark execution target
  add_custom_target(run_benchmarks
      COMMAND
          $<TARGET_FILE:binary_tree_benchmarks>
          --benchmark_min_time=0.01s
      DEPENDS
          binary_tree_benchmarks
      USES_TERMINAL
    )

endif()

# Documentation - Doxygen (installed automatically by vcpkg overlay port)
if(BINARY_TREE_BUILD_DOCS)

  # vcpkg manifest installs doxygen into <binaryDir>/vcpkg_installed.
  # Make it visible to FindDoxygen before searching the system PATH.
  file(GLOB _binary_tree_vcpkg_doxygen
      "${CMAKE_BINARY_DIR}/vcpkg_installed/*/tools/doxygen"
  )
  if(_binary_tree_vcpkg_doxygen)
    list(APPEND CMAKE_PROGRAM_PATH ${_binary_tree_vcpkg_doxygen})
  endif()

  find_package(Doxygen REQUIRED)

  if(DOXYGEN_DOT_FOUND)
    set(BINARY_TREE_DOXYGEN_HAVE_DOT "YES")
  else()
    set(BINARY_TREE_DOXYGEN_HAVE_DOT "NO")
  endif()

  set(BINARY_TREE_DOCS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs")

  configure_file(
      ${PROJECT_SOURCE_DIR}/Doxyfile.in
      ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
      @ONLY
  )

  add_custom_target(docs
      COMMAND Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM
      USES_TERMINAL
  )

endif()

# Installation
install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    TARGETS binary_tree
    EXPORT BinaryTreeTargets
)

install(
    EXPORT BinaryTreeTargets
    FILE BinaryTreeTargets.cmake
    NAMESPACE BinaryTree::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BinaryTree
)
