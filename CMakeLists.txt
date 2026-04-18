cmake_minimum_required(VERSION 3.21)
project(RhythmReplugged LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)

add_executable(RhythmReplugged
	src/BitmapFont.cpp
	src/BitmapFont.h
	src/main.cpp
)

target_link_libraries(RhythmReplugged PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image)
