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

include(FetchContent)
include(GNUInstallDirs)

option(OPTION_DEBUG_USE_SANITIZERS "Use Sanitizers for Debug" ON)

set(INSTALL_WIN32_SYMBOLS_DIR ${CMAKE_INSTALL_BINDIR})

set(CMAKE_CXX_STANDARD
    20
    CACHE STRING "Default C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message("Using C++${CMAKE_CXX_STANDARD} standard")

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

if(UNIX)
  set(CMAKE_SKIP_RPATH OFF)
  set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
  set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
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

if(OPTION_DEBUG_USE_SANITIZERS)
  message("Using sanitizers for Debug")
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
# Prepare `uxs` library

block()
set(UXS_USE_ZLIB OFF)
set(UXS_USE_LIBZIP OFF)
set(UXS_DO_INSTALL_SDK OFF)
FetchContent_Declare(uxs SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/uxs)
FetchContent_MakeAvailable(uxs)
endblock()

# ##############################################################################
# Add `parsegen` build target

file(GLOB_RECURSE sources src/*.h;src/*.cpp)

add_executable(parsegen ${sources})

target_compile_definitions(parsegen PRIVATE VERSION=${VERSION})
target_link_libraries(parsegen PRIVATE UXS::UXS)

install(TARGETS parsegen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                 COMPONENT binaries)

# ##############################################################################
# Auxiliary

include("uxs/cmake/macros.cmake")
add_compile_defs_and_include_dirs_targets(parsegen ${CMAKE_CURRENT_SOURCE_DIR})
