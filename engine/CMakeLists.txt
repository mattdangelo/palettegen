# CMakeList.txt : CMake project for CMakeProject1, include source and define
# project specific logic here.

cmake_minimum_required (VERSION 3.8)

project ("PaletteGen")

find_package( OpenCV REQUIRED )
include_directories( ${OpenCV_INCLUDE_DIRS} )

# Add source to this project's executable.
add_executable( palgen palgen.cpp  "pair_hash.h")

target_link_libraries( palgen ${OpenCV_LIBS} )