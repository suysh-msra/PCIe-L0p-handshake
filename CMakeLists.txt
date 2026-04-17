cmake_minimum_required(VERSION 3.14)
project(pcie_l0p_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(l0p_sim
    src/pcie_port.cpp
    src/pcie_link.cpp
    src/visualizer.cpp
    src/scenarios.cpp
    src/main.cpp
)
