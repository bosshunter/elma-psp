# PSP build of the Elasto Mania (2000) source code.
# Requires the PSP toolchain (https://github.com/pspdev/pspdev). Build:
#   mkdir build && cd build && psp-cmake .. && make
# (psp-cmake is cmake with -DCMAKE_TOOLCHAIN_FILE=$PSPDEV/psp/share/pspdev.cmake)
# Output: build/EBOOT.PBP
cmake_minimum_required(VERSION 3.16)

# Release by default: the EBOOT is stripped (optimisation stays at -O2 below)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(elma C CXX)
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")

if(NOT PSP)
  message(FATAL_ERROR "Configure with psp-cmake (or the pspdev.cmake toolchain file).")
endif()

# The code was written for Watcom/MSVC on x86:
#  -fsigned-char       : plain 'char' is signed there
#  -fno-strict-aliasing: the code reads parts of longs through short pointers
#  -fpermissive        : 1990s C++ (implicit int, old casts)
set(COMMON_FLAGS "-O2 -G0 -fsigned-char -fno-strict-aliasing -w")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} -std=gnu++98 -fpermissive")

# Game translation units (the Windows build minus its platform layer):
file(STRINGS build_units.txt ELMA_UNITS REGEX "^[a-z0-9_]+$")
set(ELMA_SOURCES "")
foreach(unit ${ELMA_UNITS})
  list(APPEND ELMA_SOURCES src/${unit}.cpp)
endforeach()

add_executable(elma
  ${ELMA_SOURCES}
  platform/psp_platform.cpp
  platform/psp_hhigh.cpp
  compat/compat.c
)

target_include_directories(elma PRIVATE compat src platform)

# Wide level editor: 848x480 (the PSP's shape). OFF gives the classic 640x480
# editor stretched to the screen:   psp-cmake -DELMA_WIDE_EDITOR=OFF ..
option(ELMA_WIDE_EDITOR "Level editor on a 480-wide (848x480) screen" ON)
if(ELMA_WIDE_EDITOR)
  target_compile_definitions(elma PRIVATE PORT_SZELESEDITOR)
endif()

target_link_libraries(elma
  pspgu
  pspge
  pspdisplay
  pspctrl
  pspaudio
  psppower
  psputility
  pspdebug
  m
)

create_pbp_file(
  TARGET elma
  TITLE "Elasto Mania"
  ICON_PATH ${CMAKE_SOURCE_DIR}/psp_assets/ICON0.PNG
  BACKGROUND_PATH ${CMAKE_SOURCE_DIR}/psp_assets/PIC1.PNG
  VERSION 01.00
)
