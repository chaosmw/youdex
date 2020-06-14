cmake_minimum_required(VERSION 3.5)
project(test_example VERSION 1.0.0)

if(WASM_ROOT STREQUAL "" OR NOT WASM_ROOT)
    set(WASM_ROOT "/usr/local/eosio.wasmsdk") 
    #set(WASM_ROOT ${CMAKE_INSTALL_PREFIX})
endif()

message(STATUS ${WASM_ROOT})
message(STATUS ${CMAKE_MODULE_PATH})

list(APPEND CMAKE_MODULE_PATH ${WASM_ROOT}/lib/cmake)
include(EosioWasmToolchain)

add_executable( youdex.wasm youdex.cpp )
add_executable( pixiu.wasm pixiu.cpp )
add_executable( venus.wasm venus.cpp )
add_executable( wuhan.wasm wuhan.cpp )

