cmake_minimum_required(VERSION 3.21)

file(READ VERSION VERSION)
if(VERSION STREQUAL "")
  message(FATAL_ERROR "Failed to detect version")
else()
  message("Build version ${VERSION}")
endif()

project(
  parsegen
  VERSION ${VERSION}
  DESCRIPTION "A tool for LALR-grammar based parser generation"
  LANGUAGES CXX)

include(ExternalProject)

option(USE_SANITIZERS_FOR_DEBUG "Use Sanitizers for Debug build" ON)
option(OPTION_EXPORT_COMPILE_DEFS_AND_INCLUDE_DIRS
       "Export compile definitions and include directories" OFF)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message("Using C++${CMAKE_CXX_STANDARD} standard")

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

if(UNIX)
  set(CMAKE_SKIP_RPATH OFF)
  set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
  set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib")
endif()

foreach(
  var_name
  CMAKE_C_COMPILER
  CMAKE_C_FLAGS
  CMAKE_CXX_COMPILER
  CMAKE_CXX_STANDARD
  CMAKE_CXX_FLAGS
  CMAKE_SHARED_LINKER_FLAGS
  CMAKE_BUILD_TYPE)
  if(DEFINED ${var_name})
    list(APPEND EXT_PROJECT_CMAKE_ARGS "-D${var_name}=${${var_name}}")
  endif()
endforeach()

get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(CONFIG_SUBDIR $<$<BOOL:${IS_MULTI_CONFIG}>:$<CONFIG>>)

# ##############################################################################
# Prepare `uxs` library

set(UXS_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/uxs/${CONFIG_SUBDIR})
set(UXS_INCLUDE_DIR ${UXS_INSTALL_DIR}/include)
set(UXS_LIBRARY_NAME $<IF:$<BOOL:${WIN32}>,uxs.lib,libuxs.so>)
set(UXS_LIBRARY ${UXS_INSTALL_DIR}/lib/${UXS_LIBRARY_NAME})
ExternalProject_Add(
  uxs
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/uxs
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/uxs
  BUILD_BYPRODUCTS ${UXS_LIBRARY}
  CMAKE_CACHE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${UXS_INSTALL_DIR}
  CMAKE_ARGS -DUSE_ZLIB=Off #
             -DUSE_LIBZIP=Off #
             -DUSE_SANITIZERS_FOR_DEBUG=${USE_SANITIZERS_FOR_DEBUG} #
             ${EXT_PROJECT_CMAKE_ARGS})

if(WIN32)
  install(
    DIRECTORY ${UXS_INSTALL_DIR}/bin
    DESTINATION .
    COMPONENT binary)
elseif(UNIX)
  install(
    DIRECTORY ${UXS_INSTALL_DIR}/lib
    DESTINATION .
    COMPONENT library)
endif()

# ##############################################################################
# Compilation flags

if(MSVC)
  add_compile_options(/Zc:__cplusplus /utf-8)
else()
  add_compile_options(-Wall -Wextra -pedantic)
  add_compile_options(-Wno-missing-field-initializers)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options($<$<CONFIG:Debug>:-fstandalone-debug>)
  endif()
endif()

if(USE_SANITIZERS_FOR_DEBUG)
  message("Using sanitizers for Debug build")
  if(MSVC)
    add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
  else()
    add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>)
    add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      add_link_options($<$<CONFIG:Debug>:-shared-libsan>)
    endif()
  endif()
endif()

# ##############################################################################
# Add `parsegen` build target

file(GLOB_RECURSE sources src/*.h;src/*.cpp)

add_executable(parsegen .clang-format ${sources})

add_dependencies(parsegen uxs)

target_compile_definitions(parsegen PRIVATE VERSION=${VERSION})
target_include_directories(parsegen PRIVATE ${UXS_INCLUDE_DIR})
target_link_libraries(parsegen PRIVATE ${UXS_LIBRARY})

install(TARGETS parsegen RUNTIME DESTINATION bin COMPONENT binary)

# ##############################################################################
# Auxiliary

include("uxs/cmake/uxs/macros.cmake")
add_compile_defs_and_include_dirs_targets(parsegen ${CMAKE_CURRENT_SOURCE_DIR})
