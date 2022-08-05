cmake_minimum_required(VERSION 3.12)
project(parsegen)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG_REDUCED_BUFFERS")
if(MSVC)
  if(${MSVC_VERSION} GREATER_EQUAL "1920")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")
  endif()
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STRICT_ANSI__ -fno-rtti -Wall -pedantic")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_ITERATOR_DEBUG_LEVEL=2 -fsanitize=address")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fstandalone-debug")
  endif()
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
endif()

###############################################################################
## file globbing ##############################################################
###############################################################################

# these instructions search the directory tree when cmake is
# invoked and put all files that match the pattern in the variables 
# `sources` and `data`
if(WIN32)
  set(uxs_platform_dir    uxs/platform/win)
elseif(UNIX)
  set(uxs_platform_dir    uxs/platform/posix)
endif()
file(GLOB_RECURSE includes   include/*.h)
file(GLOB_RECURSE sources    src/*.h;src/*.cpp)
file(GLOB_RECURSE uxs_includes    uxs/include/*.h)
file(GLOB_RECURSE uxs_sources     uxs/src/*.h;uxs/src/*.cpp)
file(GLOB_RECURSE uxs_platform_sources    ${uxs_platform_dir}/src/*.h;${uxs_platform_dir}/src/*.cpp)
# you can use set(sources src/main.cpp) etc if you don't want to
# use globing to find files automatically

###############################################################################
## target definitions #########################################################
###############################################################################

# add the data to the target, so it becomes visible in some IDE
add_executable(parsegen
  .clang-format
  ${uxs_includes}
  ${uxs_sources}
  ${uxs_platform_sources}
  ${includes}
  ${sources}
)

# link with these libraries
if(MSVC)
else()
  target_link_libraries(parsegen stdc++ m)
endif()

# this lets me include files relative to the root src dir with a <> pair
target_include_directories(parsegen PUBLIC uxs/include include)
