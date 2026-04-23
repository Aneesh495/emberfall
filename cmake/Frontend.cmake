include(FetchContent)
find_package(Vulkan 1.3 REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL)

set(EMB_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
function(ember_shaders target)
    set(outputs)
    foreach(shader IN LISTS ARGN)
        set(output "${EMB_SHADER_DIR}/${shader}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${EMB_SHADER_DIR}"
            COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.3
                "${PROJECT_SOURCE_DIR}/shaders/${shader}" -o "${output}"
            DEPENDS "${PROJECT_SOURCE_DIR}/shaders/${shader}" VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()
    add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()
ember_shaders(emberfall_shaders scene.vert scene.frag screen.vert tonemap.frag)

add_library(emberfall_renderer
    src/gpu/device.cpp src/gpu/resources.cpp src/gpu/pipeline.cpp src/gpu/frame.cpp)
target_link_libraries(emberfall_renderer PUBLIC emberfall_core SDL3::SDL3 Vulkan::Vulkan)
add_dependencies(emberfall_renderer emberfall_shaders)

if(BUILD_TESTING)
    add_executable(gpu_smoke tests/gpu_smoke.cpp)
    target_link_libraries(gpu_smoke PRIVATE emberfall_renderer)
endif()
