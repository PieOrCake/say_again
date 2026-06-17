cmake_minimum_required(VERSION 3.20)

# Cross-compilation setup for Windows on Linux (must be before project())
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

project(SayAgain)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -DWIN32_LEAN_AND_MEAN")

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/backends)
include_directories(${CMAKE_SOURCE_DIR}/lib/nlohmann)

set(IMGUI_SOURCES
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
)

set(SOURCES
    src/dllmain.cpp
    src/Messages.cpp
    src/FloatingIcon.cpp
    src/ChatLinks.cpp
    src/DecoderClient.cpp
    src/LinkResolve.cpp
    ${IMGUI_SOURCES}
)

add_library(SayAgain SHARED ${SOURCES})

set_target_properties(SayAgain PROPERTIES
    SUFFIX ".dll"
    PREFIX ""
)

target_link_libraries(SayAgain
    gdi32
    user32
    comdlg32
    winhttp
)

target_link_options(SayAgain PRIVATE -Wl,--out-implib,SayAgain.lib -Wl,--strip-all)
target_sources(SayAgain PRIVATE ${CMAKE_SOURCE_DIR}/SayAgain.def)

set_target_properties(SayAgain PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
