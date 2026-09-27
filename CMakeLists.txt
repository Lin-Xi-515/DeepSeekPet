cmake_minimum_required(VERSION 3.16)

project(DeepSeekPet
        VERSION 1.0.0
        DESCRIPTION "DeepSeek 桌宠：悬浮显示余额 / 更换素材 / 一键启动 DeepSeek Harness"
        LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR
        "这个项目只支持 Windows + MSVC（用到了分层窗口、GDI+、WIC）。\n"
        "在 CLion 里请把 Toolchain 选成 Visual Studio。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源码：单文件主程序 + WIC 解码头（截图/日志用 UTF-8 源码，需要 /utf-8）
set(PET_SOURCES
    src/main.cpp
    src/wicdecoder.h)

add_executable(DeepSeekPet WIN32 ${PET_SOURCES})

# ---------------------------------------------------------------- 编译选项
target_compile_definitions(DeepSeekPet PRIVATE
    UNICODE _UNICODE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601)

target_compile_options(DeepSeekPet PRIVATE
    /utf-8                  # 源码是 UTF-8（含中文），必须显式指定
    /W3
    /EHsc)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(DeepSeekPet PRIVATE /Od /Zi)
else()
    target_compile_options(DeepSeekPet PRIVATE /O2)
endif()

# 静态链接 CRT：编出来的 exe 不依赖 MSVC 运行库，方便发给别人
set_property(TARGET DeepSeekPet PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
# 注意：也可以用 MSVC_RUNTIME_LIBRARY 策略替代，见文末说明

# 链接库（其实 #pragma comment(lib, ...) 已经在源码里声明了，
# 这里再写一遍，方便在 IDE 里看清楚依赖）
target_link_libraries(DeepSeekPet PRIVATE
    user32 gdi32 gdiplus winhttp shell32 ole32 advapi32 windowscodecs shlwapi)

# ---------------------------------------------------------------- 资源（图标 + 清单）
# pet.rc 里用相对路径引用 ..\assets\pet.ico 和 pet.manifest，
# 所以把 rc 的工作目录设成 src/，相对路径才对得上。
set(PET_RC "${CMAKE_CURRENT_SOURCE_DIR}/src/pet.rc")
if(EXISTS "${PET_RC}")
    set(PET_RC_OUT "${CMAKE_CURRENT_BINARY_DIR}/pet.res")
    add_custom_command(
        OUTPUT "${PET_RC_OUT}"
        COMMAND "${CMAKE_RC_COMPILER}" /nologo /fo "${PET_RC_OUT}" "${PET_RC}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
        DEPENDS "${PET_RC}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/pet.manifest"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/pet.ico"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/pet_normal.png"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/pet_low.png"
        COMMENT "编译资源 pet.rc")
    target_sources(DeepSeekPet PRIVATE "${PET_RC_OUT}")
    set_source_files_properties("${PET_RC_OUT}" PROPERTIES GENERATED TRUE)
endif()

# ---------------------------------------------------------------- 素材
# 程序运行时会从 exe 同目录的 assets/ 读取动画帧，所以构建后自动复制一份。
add_custom_command(TARGET DeepSeekPet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:DeepSeekPet>/assets"
    COMMENT "复制 assets 到输出目录")

# ---------------------------------------------------------------- 工程内说明
message(STATUS "DeepSeekPet: 生成器=${CMAKE_GENERATOR}, 编译器=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ---------------------------------------------------------------------------
# 备注
#  1) 如果 CMake 报 MSVC_RUNTIME_LIBRARY 相关错误，说明 CMake 版本较老，
#     把上面那行换成：
#         set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
#     或者直接删掉，默认动态 CRT 也能编译，只是发给别人时要带 MSVC 运行库。
#  2) CLion 里 Toolchain 选 "Visual Studio"，它会自动找到
#     D:\my\DEVELOPER\VisualStudio 下的 cl.exe / rc.exe。
#  3) 中文在 CLion 的 Run 控制台可能乱码，可在
#     Help | Edit Custom VM Options 里加 -Dfile.encoding=UTF-8。
# ---------------------------------------------------------------------------
