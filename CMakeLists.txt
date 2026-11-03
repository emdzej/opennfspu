cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures (use \"arm64;x86_64\" for universal)")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(OpenNFSPU VERSION 0.1.5 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(PU_WARN -Wall -Wextra -Wno-unused-parameter)
# GCC's guesses at snprintf truncation: the port bounds its strings with snprintf on purpose (the
# original's fixed buffers).
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  list(APPEND PU_WARN -Wno-format-truncation)
endif()

# The portable core: everything except host backends (no platform headers). The THRASH driver
# (src/render/thrash*.c) is the only GL user; the native build leaves it out (tests and tools run without
# a GPU).
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/*.c src/real/*.c src/support/*.c src/eagl/*.c src/front/*.c src/game/*.c src/render/*.c src/snd/*.c)
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
  # The game's frames from its own loop (gasm_loop.c, Asyncify by Binaryen's wasm-opt) with gasm's
  # cooperative threads: the network library runs its threads and waits as the original does.
  option(OPENNFSPU_LOOP_THREADS "gasm_add_game LOOP THREADS (needs wasm-opt)" ON)
  if(NOT GASM_WASM_OPT AND EXISTS "${CMAKE_SOURCE_DIR}/.deps/binaryen/bin/wasm-opt")
    set(GASM_WASM_OPT "${CMAKE_SOURCE_DIR}/.deps/binaryen/bin/wasm-opt")
  endif()
  if(OPENNFSPU_LOOP_THREADS)
    gasm_add_game(opennfspu LOOP THREADS ${CORE_SRC} ${GL_SRC} src/app.c src/platform_gasm.c ${GASM_GL_SOURCE})
    target_compile_definitions(opennfspu PRIVATE PU_LOOP_THREADS)
  else()
    gasm_add_game(opennfspu ${CORE_SRC} ${GL_SRC} src/app.c src/platform_gasm.c ${GASM_GL_SOURCE})
  endif()
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
# audio_test again on castle (its sound triggers) with an opponent (its sounds, a siren)
add_test(NAME audio_test_castle COMMAND audio_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(audio_test_castle PROPERTIES ENVIRONMENT "AUDIO_TRACK=castle;AUDIO_OPP=1")
