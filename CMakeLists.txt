cmake_minimum_required(VERSION 3.16)
project(notify-fullscreen VERSION 1.3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(NF_SOURCES
    src/main.c
    src/app.c
    src/text.c
    src/layout.c
    src/preview.c
    src/tmux.c
    src/util.c
)

# Platform backend selection. Only X11 exists for now; the Windows and macOS
# backends (src/backend/win32.c, src/backend/cocoa.m) slot in here.
if(UNIX AND NOT APPLE)
    list(APPEND NF_SOURCES src/backend/x11.c)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO REQUIRED cairo)
    pkg_check_modules(X11 REQUIRED x11)
else()
    message(FATAL_ERROR
        "no backend for this platform yet (phase 1: macOS, phase 2: Windows)")
endif()

add_executable(notify-fullscreen ${NF_SOURCES})
target_include_directories(notify-fullscreen PRIVATE src)
target_compile_options(notify-fullscreen PRIVATE
    -Wall -Wextra -Wpedantic
    ${CAIRO_CFLAGS} ${X11_CFLAGS}
)
target_link_libraries(notify-fullscreen PRIVATE
    ${CAIRO_LIBRARIES} ${X11_LIBRARIES}
)

install(TARGETS notify-fullscreen RUNTIME DESTINATION bin)