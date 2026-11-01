cmake_minimum_required(VERSION 3.20)
project(OscreenSaver VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime: the .exe runs on any Windows 10/11 x64 without extra installs.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /utf-8 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # The manifest is embedded from res/app.rc; stop the linker from adding a second one.
    add_link_options(/MANIFEST:NO)
else()
    add_compile_options(-Wall -Wextra -Wno-missing-field-initializers -finput-charset=UTF-8)
    add_link_options(-static -municode -mwindows)
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

add_executable(OscreenSaver WIN32
    src/main.cpp
    src/capture.cpp
    src/overlay.cpp
    src/save.cpp
    src/annotate.cpp
    src/toolbar.cpp
    src/autostart.cpp
    src/i18n.cpp
    res/app.rc
)

target_link_libraries(OscreenSaver PRIVATE user32 gdi32 shell32 advapi32 shcore ole32 oleaut32 windowscodecs uuid gdiplus)
