cmake_minimum_required(VERSION 3.16)
project(FileFind LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(FileFind
    backend/main.cpp
    backend/explorer/FileExplorer.cpp
    backend/explorer/FolderScanner.cpp
)

target_include_directories(FileFind PRIVATE backend)

if (WIN32)
    target_link_libraries(FileFind PRIVATE ws2_32 ole32 shell32 uuid)
endif()

install(TARGETS FileFind RUNTIME DESTINATION .)
install(DIRECTORY frontend DESTINATION .)
install(DIRECTORY backend DESTINATION .)
install(DIRECTORY samples DESTINATION .)
