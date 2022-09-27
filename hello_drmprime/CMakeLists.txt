cmake_minimum_required(VERSION 3.16.3)
set(CMAKE_CXX_STANDARD 17)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR};/usr/local")

project(hello_drmprime)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -Wextra -g -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall  -g -O2")

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mfpu=neon" COMPILER_SUPPORTS_NEON)
if(COMPILER_SUPPORTS_NEON)
	message(STATUS "Compiler supports NEON")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=neon")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon")
endif()

find_package(PkgConfig REQUIRED)

#message("${AVCODEC_LDFLAGS}")

include_directories(
	/usr/include/libdrm)

add_executable(hello_drmprime hello_drmprime.cpp drm_kms/drmprime_out.cpp drm_kms/drmprime_out.h
		opengl/egl_out.cpp opengl/egl_out.h opengl/gl_shaders.cpp opengl/gl_shaders.h  opengl/gl_videorenderer.cpp opengl/gl_videorenderer.h #CUDAGLInteropHelper.cpp CUDAGLInteropHelper.h
		)
	
target_link_libraries(hello_drmprime
		#
		-lavcodec -lavutil -lavformat -ldrm -lpthread -lGLESv2 -lglfw -lEGL #-lcuda
		)

add_executable(test_util_decoder_latency test_util_decoder_latency.cpp)
target_link_libraries(test_util_decoder_latency
		#
		-lavcodec -lavutil -lavformat -lpthread
		)


#find_package(CUDA 9.0 REQUIRED)
#target_link_libraries(hello_drmprime ${CUDA_LIBRARIES})

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
target_link_libraries(hello_drmprime ${SDL2_LIBRARIES})
