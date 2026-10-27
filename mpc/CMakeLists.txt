cmake_minimum_required(VERSION 3.22)

# BOOM8 for MPC OS standalone (Open MPC VST2 wrapper). Usually driven by
# mpc/build.sh, which cross-compiles for the MPC Live III (aarch64 Linux) with
# zig and generates params.h and the touchscreen skin with mpc-vst-plugins.
#
#   MPC_VST_DIR         checkout of https://github.com/sd88me/mpc-vst-plugins
#   MPC_PARAMS_DIR      folder holding the params.h gen_vst.py wrote from vst.json
#   MPC_WRAPPER_SOURCE  vst2_wrap.c to build (build.sh passes a copy that also
#                       tells the engine about MPC's STOP)

project(boom8_mpc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(MPC_VST_DIR "" CACHE PATH "mpc-vst-plugins checkout (wrapper/vst2_wrap.c)")
set(MPC_PARAMS_DIR "" CACHE PATH "Folder with the generated params.h")
set(MPC_WRAPPER_SOURCE "" CACHE FILEPATH "vst2_wrap.c to build (default: the checkout's)")

if(NOT MPC_VST_DIR OR NOT EXISTS "${MPC_VST_DIR}/wrapper/engine.h")
    message(FATAL_ERROR "Set MPC_VST_DIR to a mpc-vst-plugins checkout (mpc/build.sh does).")
endif()

set(BOOM8_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../engine")

add_library(boom8_engine STATIC
    ${BOOM8_ENGINE_DIR}/Boom8.cpp
    Boom8Mpc.cpp
)
target_include_directories(boom8_engine PUBLIC ${BOOM8_ENGINE_DIR} ${MPC_VST_DIR}/wrapper)
target_compile_options(boom8_engine PRIVATE -Wall -Wextra)
set_target_properties(boom8_engine PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
find_package(Threads REQUIRED)
target_link_libraries(boom8_engine PUBLIC Threads::Threads)

add_executable(boom8-mpc-test ../test/EngineTest.cpp)
target_link_libraries(boom8-mpc-test PRIVATE boom8_engine)
add_executable(boom8-demo ../test/Demo.cpp)
target_link_libraries(boom8-demo PRIVATE boom8_engine)

if(MPC_PARAMS_DIR)
    if(NOT MPC_WRAPPER_SOURCE)
        set(MPC_WRAPPER_SOURCE ${MPC_VST_DIR}/wrapper/vst2_wrap.c)
    endif()
    add_library(boom8_mpc SHARED ${MPC_WRAPPER_SOURCE})
    set_source_files_properties(${MPC_WRAPPER_SOURCE} PROPERTIES COMPILE_OPTIONS "-std=gnu11")
    target_include_directories(boom8_mpc PRIVATE ${MPC_PARAMS_DIR} ${MPC_VST_DIR}/wrapper)
    target_link_libraries(boom8_mpc PRIVATE boom8_engine)
    set_target_properties(boom8_mpc PROPERTIES
        PREFIX ""
        OUTPUT_NAME "boom8"
        SUFFIX ".so"
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Only VSTPluginMain is exported; an unresolved symbol must fail
        # here, not crash MPC when it loads the plug-in.
        target_link_options(boom8_mpc PRIVATE
            -Wl,--no-undefined
            -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map
            -s)
    endif()
endif()
