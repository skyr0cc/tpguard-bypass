cmake_minimum_required(VERSION 3.20)
project(skyro_wtf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(skyro_wtf WIN32 src/skyro_wtf.cpp)

target_compile_definitions(skyro_wtf PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(skyro_wtf PRIVATE /W4 /permissive- /MT)
else()
    target_compile_options(skyro_wtf PRIVATE -Wall -Wextra -Wpedantic -municode)
    target_link_options(skyro_wtf PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

target_link_libraries(skyro_wtf PRIVATE
    ws2_32
    user32
    gdi32
    shell32
    gdiplus
)

add_custom_command(TARGET skyro_wtf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:skyro_wtf>/assets"
    COMMENT "Copying assets next to skyro_wtf"
)
