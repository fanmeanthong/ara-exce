cmake_minimum_required(VERSION 3.10)
project(RadarSomeipDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== Dependencies =====
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(Threads REQUIRED)
# Header-only JSON (tự include), nếu có config package thì link mục tiêu tiện lợi
find_package(nlohmann_json CONFIG QUIET)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    /usr/local/include
)

link_directories(/usr/local/lib)

set(SOURCES_COMMON
    AraCom_Skeleton.h
    SomeipBinding.h
    AraExec.h
)

add_executable(client client.cpp ${SOURCES_COMMON})
target_link_libraries(client
    vsomeip3
    ${Boost_LIBRARIES}
    Threads::Threads
)
if (nlohmann_json_FOUND)
  target_link_libraries(client nlohmann_json::nlohmann_json)
endif()

add_executable(server server.cpp ${SOURCES_COMMON})
target_link_libraries(server
    vsomeip3
    ${Boost_LIBRARIES}
    Threads::Threads
)
option(EXCEV_SIMULATE_CRASH "Simulate a periodic crash every 5s for demo" OFF)

if (EXCEV_SIMULATE_CRASH)
  target_compile_definitions(server PRIVATE EXCEV_SIMULATE_CRASH)
endif()

if (nlohmann_json_FOUND)
  target_link_libraries(server nlohmann_json::nlohmann_json)
endif()
