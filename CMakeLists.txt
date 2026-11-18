cmake_minimum_required(VERSION 3.24)
project(Dota2_First LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)


FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_aternos STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(imgui_aternos PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

if(WIN32)
    target_link_libraries(imgui_aternos PUBLIC d3d11 dxgi d3dcompiler)
endif()


set(SOURCEPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SOURCEPP_BUILD_TESTS_EXTRA OFF CACHE BOOL "" FORCE)
set(SOURCEPP_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SOURCEPP_BUILD_C_WRAPPERS OFF CACHE BOOL "" FORCE)
set(SOURCEPP_BUILD_PYTHON_WRAPPERS OFF CACHE BOOL "" FORCE)

set(MZ_BZIP2 OFF CACHE BOOL "" FORCE)
set(MZ_LZMA  OFF CACHE BOOL "" FORCE)

set(JPEGXL_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_MANPAGES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_JNI OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_SJPEG OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_OPENEXR OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_SKCMS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_VIEWERS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    sourcepp
    GIT_REPOSITORY https://github.com/craftablescience/sourcepp.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)

set(FETCHCONTENT_SOURCE_DIR_ZLIB "G:/Dota2_First/build/_deps/zlib-src" CACHE PATH "" FORCE)
FetchContent_MakeAvailable(sourcepp)

add_executable(dota2PyScriptPlus
    src/main.cpp
    src/steam_detector.cpp
    src/vpk_manager.cpp
    src/item_database.cpp
    src/spell_database.cpp
    src/diag_log.cpp
    src/vtex_decoder.cpp
    src/icon_manager.cpp
    src/mod_generator.cpp
    src/remote_config.cpp
)

target_compile_definitions(dota2PyScriptPlus PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(dota2PyScriptPlus PRIVATE
    "${sourcepp_SOURCE_DIR}/include"
#    "${sourcepp_SOURCE_DIR}/build/_deps/tsl_hat_trie-src/include"
    "${CMAKE_BINARY_DIR}/_deps/tsl_hat_trie-src/include"
    "${sourcepp_SOURCE_DIR}/ext/half/include"
    "${CMAKE_BINARY_DIR}/_deps/bcdec-src/include"
)

#target_link_libraries(dota2PyScriptPlus PRIVATE
 #   imgui_aternos
  #  vpkpp
   # kvpp
    #steampp
#)
target_link_libraries(dota2PyScriptPlus PRIVATE
    imgui_aternos
    sourcepp::vpkpp
    sourcepp::kvpp
    sourcepp::steampp
)

if(WIN32)
    target_link_libraries(dota2PyScriptPlus PRIVATE advapi32)
endif()