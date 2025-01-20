# TODO: Switch to GCC
# FROM gcc:11 as build-stage

# WORKDIR /app
# COPY image-clusterer/ .
# RUN g++ -shared -fPIC -o my_extension.so my_extension.cpp

# RUN g++ -shared -fPIC -o ruby_test.bundle ruby_test.cpp \
# -I$(ruby -e 'puts RbConfig::CONFIG["rubyhdrdir"]') \
# -I$(ruby -e 'puts RbConfig::CONFIG["rubyarchhdrdir"]') -L$(ruby -e 'puts RbConfig::CONFIG["libdir"]') \
# -l$(ruby -e 'puts RbConfig::CONFIG["LIBRUBY"].sub(/^lib/, "").sub(/\..*$/, "")')


# Build the Ruby C++ extension

FROM ruby:3.4.1-slim AS builder

# Install dependencies (build tools, OpenCV, Ruby development headers)
RUN apt-get update && apt-get install -y \
  build-essential \
  cmake \
  libopencv-dev \
  ruby-dev \
  git \
  && rm -rf /var/lib/apt/lists/*

# Set up working directory
WORKDIR /app

# Copy the rest of the app
COPY . /app

# Clean any previous build files
RUN rm -rf build

# Run cmake and make to build the C++ extension
RUN cmake -S . -B build && make -C build

# Copy the .so extension to a location we can use in the final container
RUN mkdir -p /app/lib && cp /app/build/image_clusterer.so /app/lib/image_clusterer.so