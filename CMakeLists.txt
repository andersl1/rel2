cmake_minimum_required(VERSION 3.24)
project(REL2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# OpenMP
find_package(OpenMP REQUIRED)

# Fix DOWNLOAD_EXTRACT_TIMESTAMP warning
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

# 1. nlohmann_json
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
    FetchContent_Populate(json)
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE ${json_SOURCE_DIR}/include)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

# 2. GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# 3. Zstd
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd
    GIT_TAG v1.5.5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(zstd)
if(NOT zstd_POPULATED)
    FetchContent_Populate(zstd)
    
    file(GLOB ZSTD_COMMON_SOURCES "${zstd_SOURCE_DIR}/lib/common/*.c")
    file(GLOB ZSTD_COMPRESS_SOURCES "${zstd_SOURCE_DIR}/lib/compress/*.c")
    file(GLOB ZSTD_DECOMPRESS_SOURCES "${zstd_SOURCE_DIR}/lib/decompress/*.c")
    
    add_library(libzstd_static STATIC 
        ${ZSTD_COMMON_SOURCES} 
        ${ZSTD_COMPRESS_SOURCES} 
        ${ZSTD_DECOMPRESS_SOURCES}
    )
    
    target_include_directories(libzstd_static PUBLIC ${zstd_SOURCE_DIR}/lib)
    target_compile_definitions(libzstd_static PUBLIC ZSTD_STATIC_LINKING_ONLY)
endif()
set(ZSTD_TARGET libzstd_static)

# 4. CPR (HTTP Client)
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(CPR_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_GENERATE_COVERAGE OFF CACHE BOOL "" FORCE)
set(CPR_USE_SYSTEM_CURL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpr)

# 5. ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG v1.89.9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# 6. ImPlot
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot
    GIT_TAG v0.16
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(implot)
if(NOT implot_POPULATED)
    FetchContent_Populate(implot)
endif()

# Create executable
file(GLOB SOURCES "src/*.cpp")
add_executable(REL2 ${SOURCES})

# Include directories
target_include_directories(REL2 PRIVATE 
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
    src
)

# ImGui Sources
target_sources(REL2 PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
)

target_link_libraries(REL2 PRIVATE 
    nlohmann_json::nlohmann_json
    glfw
    ${ZSTD_TARGET}
    cpr::cpr
    OpenMP::OpenMP_CXX
    opengl32
)
