cmake_minimum_required(VERSION 3.22.1)

project("lazyplayer" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/prober
    ${CMAKE_CURRENT_SOURCE_DIR}/decoder
    ${CMAKE_CURRENT_SOURCE_DIR}/record
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
)

add_library(lazyplayer SHARED
    native_player_jni.cpp
    player_engine.cpp
    common/signal_handler.cpp
    transport/socket_transport.cpp
    transport/rtp_depacketizer.cpp
    transport/rtsp_client.cpp
    transport/direct_udp_receiver.cpp
    prober/codec_prober.cpp
    decoder/hw_decoder.cpp
    decoder/sw_decoder.cpp
    record/mp4_muxer.cpp
    record/jpeg_writer.cpp
    telemetry/telemetry_collector.cpp
)

find_library(log-lib log)
find_library(android-lib android)
find_library(mediandk-lib mediandk)
find_library(jnigraphics-lib jnigraphics)

target_link_libraries(lazyplayer
    ${log-lib}
    ${android-lib}
    ${mediandk-lib}
    ${jnigraphics-lib}
    m
)
