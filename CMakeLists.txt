# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes")
project(esp_layout_controller)

if(DEFINED ENV{IDF_PYTHON_ENV_PATH})
	set(IDF_PYTHON_EXECUTABLE "$ENV{IDF_PYTHON_ENV_PATH}/Scripts/python.exe")
	if(EXISTS "${IDF_PYTHON_EXECUTABLE}")
		set(Python3_EXECUTABLE "${IDF_PYTHON_EXECUTABLE}")
	endif()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Serial port for flashing — override with ESP_PORT environment variable
if(NOT DEFINED ENV{ESP_PORT})
	set(ENV{ESP_PORT} "COM4")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/test_app.py")
	add_custom_target(flash_test
		COMMAND ${Python3_EXECUTABLE} $ENV{IDF_PATH}/tools/idf.py -B ${CMAKE_BINARY_DIR} -p $ENV{ESP_PORT} flash
		COMMAND ${Python3_EXECUTABLE} -m pytest -s --port $ENV{ESP_PORT}
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		COMMENT "Running pytest-embedded tests"
	)
endif()
