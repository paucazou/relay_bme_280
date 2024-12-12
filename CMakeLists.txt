# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(BMX)
# Add the BMX280 library as a subdirectory
set(EXTRA_COMPONENTS_DIR components/esp-idf-bmx280)

