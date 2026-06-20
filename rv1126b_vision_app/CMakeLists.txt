cmake_minimum_required(VERSION 3.16)
project(rv1126b_vision_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RV1126B_ENABLE_RGA "Enable Rockchip RGA image preprocess when headers and library are available" ON)
option(RV1126B_ENABLE_OPENCV "Enable OpenCV image preprocess fallback when OpenCV is available" ON)
option(RV1126B_ENABLE_RKNN "Enable RKNN model inference when headers and library are available" ON)
option(RV1126B_ENABLE_MQTT "Enable libmosquitto MQTT client when headers and library are available" ON)
option(RV1126B_ENABLE_MPP "Enable Rockchip MPP H264 encoder when headers and library are available" ON)

set(RV1126B_LOCAL_RKNN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/include")
set(RV1126B_LOCAL_RKNN_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/lib")

add_executable(rv1126b_vision_app
    src/main.cpp
    src/App.cpp
    src/CameraDevice.cpp
    src/DisplayDevice.cpp
    src/GestureModel.cpp
    src/PerceptionModules.cpp
    src/PostureDrinkModel.cpp
    src/RknnModel.cpp
    src/MqttClient.cpp
    src/VideoPipeline.cpp
    src/FlvMuxer.cpp
    src/WebRtcTransport.cpp
    src/WebStreamer.cpp
)

target_include_directories(rv1126b_vision_app PRIVATE include)

if (RV1126B_ENABLE_RGA)
    find_path(RGA_INCLUDE_DIR NAMES im2d.h PATH_SUFFIXES rga)
    find_library(RGA_LIBRARY NAMES rga)
    if (RGA_INCLUDE_DIR AND RGA_LIBRARY)
        target_include_directories(rv1126b_vision_app PRIVATE ${RGA_INCLUDE_DIR})
        target_link_libraries(rv1126b_vision_app PRIVATE ${RGA_LIBRARY})
        target_compile_definitions(rv1126b_vision_app PRIVATE RV1126B_HAS_RGA=1)
        message(STATUS "RGA enabled: ${RGA_LIBRARY}")
    else()
        message(STATUS "RGA not found, software/OpenCV preprocess will be used")
    endif()
endif()

if (RV1126B_ENABLE_RKNN)
    find_path(RKNN_INCLUDE_DIR
        NAMES rknn_api.h
        HINTS ${RV1126B_LOCAL_RKNN_INCLUDE_DIR}
        PATH_SUFFIXES rknn)
    find_library(RKNN_LIBRARY
        NAMES rknnrt
        HINTS ${RV1126B_LOCAL_RKNN_LIBRARY_DIR})
    if (RKNN_INCLUDE_DIR AND RKNN_LIBRARY)
        target_include_directories(rv1126b_vision_app PRIVATE ${RKNN_INCLUDE_DIR})
        target_link_libraries(rv1126b_vision_app PRIVATE ${RKNN_LIBRARY})
        target_compile_definitions(rv1126b_vision_app PRIVATE RV1126B_HAS_RKNN=1)
        message(STATUS "RKNN enabled: ${RKNN_LIBRARY}")
    else()
        message(STATUS "RKNN not found, model fallback parser will be used")
    endif()
endif()

if (RV1126B_ENABLE_OPENCV)
    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
    if (OpenCV_FOUND)
        target_include_directories(rv1126b_vision_app PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(rv1126b_vision_app PRIVATE ${OpenCV_LIBS})
        target_compile_definitions(rv1126b_vision_app PRIVATE RV1126B_HAS_OPENCV=1)
        message(STATUS "OpenCV enabled: ${OpenCV_VERSION}")
    else()
        message(STATUS "OpenCV not found, software preprocess fallback will be used")
    endif()
endif()

if (RV1126B_ENABLE_MQTT)
    find_path(MOSQUITTO_INCLUDE_DIR NAMES mosquitto.h)
    find_library(MOSQUITTO_LIBRARY NAMES mosquitto)
    if (MOSQUITTO_INCLUDE_DIR AND MOSQUITTO_LIBRARY)
        target_include_directories(rv1126b_vision_app PRIVATE ${MOSQUITTO_INCLUDE_DIR})
        target_link_libraries(rv1126b_vision_app PRIVATE ${MOSQUITTO_LIBRARY})
        target_compile_definitions(rv1126b_vision_app PRIVATE RV1126B_HAS_MOSQUITTO=1)
        message(STATUS "Mosquitto MQTT enabled: ${MOSQUITTO_LIBRARY}")
    else()
        message(STATUS "Mosquitto not found, MQTT log fallback will be used")
    endif()
endif()

if (RV1126B_ENABLE_MPP)
    find_path(MPP_INCLUDE_DIR NAMES rk_mpi.h PATH_SUFFIXES mpp rockchip)
    find_library(MPP_LIBRARY NAMES rockchip_mpp mpp)
    if (MPP_INCLUDE_DIR AND MPP_LIBRARY)
        target_include_directories(rv1126b_vision_app PRIVATE ${MPP_INCLUDE_DIR})
        target_link_libraries(rv1126b_vision_app PRIVATE ${MPP_LIBRARY})
        target_compile_definitions(rv1126b_vision_app PRIVATE RV1126B_HAS_MPP=1)
        message(STATUS "Rockchip MPP enabled: ${MPP_LIBRARY}")
    else()
        message(STATUS "Rockchip MPP not found, H264 encoder will be disabled")
    endif()
endif()

if (MSVC)
    target_compile_options(rv1126b_vision_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(rv1126b_vision_app PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(rv1126b_vision_app PRIVATE pthread)
endif()




# Single image gesture model test tool.
# Usage:
#   ./build-rknn/gesture_image_test /path/to/image.jpg
add_executable(gesture_image_test
    tools/gesture_image_test.cpp
    src/GestureModel.cpp
    src/RknnModel.cpp
)

target_include_directories(gesture_image_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/include
)

target_compile_features(gesture_image_test PRIVATE cxx_std_17)

if(RV1126B_ENABLE_RKNN)
    target_compile_definitions(gesture_image_test PRIVATE RV1126B_HAS_RKNN)
    target_link_libraries(gesture_image_test PRIVATE /usr/lib/librknnrt.so)
endif()

if(RV1126B_ENABLE_OPENCV)
    target_compile_definitions(gesture_image_test PRIVATE RV1126B_HAS_OPENCV)

    target_link_libraries(gesture_image_test PRIVATE
        ${OpenCV_LIBRARIES}
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
    )
endif()
