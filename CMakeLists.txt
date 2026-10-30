cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures (use \"arm64;x86_64\" for universal)")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(OpenNFSPU VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(PU_WARN -Wall -Wextra -Wno-unused-parameter)

# The portable core: everything except host backends (no platform headers). The THRASH driver
# (src/render/thrash*.c) is the only GL user; the native build leaves it out (tests and tools run without
# a GPU).
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/*.c src/real/*.c src/eagl/*.c src/front/*.c src/game/*.c src/render/*.c)
file(GLOB GL_SRC CONFIGURE_DEPENDS src/render/thrash*.c)
set(HOST_SRC src/vfs_host.c src/platform_host.c)
foreach(f ${HOST_SRC} src/platform_gasm.c src/app.c)
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()
list(REMOVE_ITEM CORE_SRC ${GL_SRC})

# Platform: native (tools and headless tests) or gasm (opennfspu.wasm, a gasm guest):
#   cmake -S . -B build-gasm -DOPENNFSPU_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
# (tools/fetch-gasm-sdk.sh fetches the released SDK and wasi-sdk into .deps/)
set(OPENNFSPU_PLATFORM native CACHE STRING "Platform: native or gasm")
if(OPENNFSPU_PLATFORM STREQUAL "gasm")
  include(Gasm)
  gasm_add_game(opennfspu ${CORE_SRC} ${GL_SRC} src/app.c src/platform_gasm.c ${GASM_GL_SOURCE})
  target_include_directories(opennfspu PRIVATE src)
  target_compile_options(opennfspu PRIVATE ${PU_WARN} -O2)
  # Track and car loading keep large buffers on the stack: 1 MiB instead of 64 KiB.
  target_link_options(opennfspu PRIVATE -Wl,-z,stack-size=1048576 -Wl,--strip-debug)
  return()
endif()

add_library(pu_core STATIC ${CORE_SRC})
target_include_directories(pu_core PUBLIC src)
target_compile_options(pu_core PRIVATE ${PU_WARN})
if(NOT APPLE)
  target_link_libraries(pu_core PUBLIC m)   # libm is separate from libc on Linux
endif()

# Tests and tools read the game data through the file layer (./game, or OPENNFSPU_DATA=<dir|image>).
enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*_test.c)
foreach(src ${TEST_SRC})
  get_filename_component(t ${src} NAME_WE)
  add_executable(${t} ${src} ${HOST_SRC})
  target_link_libraries(${t} PRIVATE pu_core)
  target_compile_options(${t} PRIVATE ${PU_WARN})
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
