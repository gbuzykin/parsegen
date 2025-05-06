cmake_minimum_required(VERSION 3.12)
project(parsegen)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions($<$<CONFIG:Debug>:UXS_DEBUG_REDUCED_BUFFERS>)

message(CMAKE_CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID})

if(MSVC)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
  endif()
  add_compile_options(/Zc:__cplusplus /utf-8 /WX)
else()
  add_compile_definitions($<$<CONFIG:Debug>:UXS_ITERATOR_DEBUG_LEVEL=2>)
  add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>)
  add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>)
  add_compile_options(-Wall -pedantic -Werror)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options($<$<CONFIG:Debug>:-fstandalone-debug>)
    add_link_options($<$<CONFIG:Debug>:-shared-libsan>)
  endif()
endif()

file(READ "VERSION" VERSION)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVERSION=${VERSION}")

# ##############################################################################
# file globbing ##############################################################
# ##############################################################################

if(WIN32)
  set(uxs_platform_dir uxs/platform/win)
elseif(UNIX)
  set(uxs_platform_dir uxs/platform/posix)
endif()
file(GLOB_RECURSE includes include/*.h)
file(GLOB_RECURSE sources src/*.h;src/*.cpp)
file(GLOB_RECURSE uxs_includes uxs/include/*.h)
file(GLOB_RECURSE uxs_sources uxs/src/*.h;uxs/src/*.cpp)
file(GLOB_RECURSE uxs_platform_sources
     ${uxs_platform_dir}/src/*.h;${uxs_platform_dir}/src/*.cpp)

# ##############################################################################
# target definitions #########################################################
# ##############################################################################

add_executable(parsegen .clang-format ${uxs_includes} ${uxs_sources}
                        ${uxs_platform_sources} ${includes} ${sources})

target_include_directories(parsegen PUBLIC uxs/include include)

add_custom_target(
  compile_defs
  COMMAND echo "-D$<JOIN:$<TARGET_PROPERTY:parsegen,COMPILE_DEFINITIONS>, -D>"
          >${CMAKE_CURRENT_SOURCE_DIR}/compile-defs.list)
add_custom_target(
  include_dirs
  COMMAND echo "-I$<JOIN:$<TARGET_PROPERTY:parsegen,INCLUDE_DIRECTORIES>, -I>"
          >${CMAKE_CURRENT_SOURCE_DIR}/include-dirs.list)
add_dependencies(parsegen compile_defs include_dirs)
