cmake_minimum_required (VERSION 3.25.1)

project (image-clusterer)

# Find OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Get Ruby include and library paths
execute_process(COMMAND ruby -rrbconfig -e "print RbConfig::CONFIG['rubyhdrdir']"
                OUTPUT_VARIABLE RUBY_INCLUDE_DIR)
execute_process(COMMAND ruby -rrbconfig -e "print RbConfig::CONFIG['rubyarchhdrdir']"
                OUTPUT_VARIABLE RUBY_ARCH_INCLUDE_DIR)

include_directories(${RUBY_INCLUDE_DIR} ${RUBY_ARCH_INCLUDE_DIR})

execute_process(COMMAND ruby -rrbconfig -e "print RbConfig::CONFIG['libdir']"
                OUTPUT_VARIABLE RUBY_LIB_DIR)
link_directories(${RUBY_LIB_DIR})

# Define the shared library
add_library(image_clusterer SHARED image-clusterer.cpp)

# Link OpenCV and Ruby libraries
target_link_libraries(image_clusterer ${OpenCV_LIBS} ruby)

# Set the properties of the shared library
set_target_properties(image_clusterer PROPERTIES PREFIX "" SUFFIX ".so")