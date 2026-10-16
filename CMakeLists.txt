
cmake_minimum_required(VERSION 3.16)
project(Project10 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

FetchContent_MakeAvailable(json)

add_executable(project10
    src/main.cpp
)

target_link_libraries(project10
    PRIVATE nlohmann_json::nlohmann_json
)
