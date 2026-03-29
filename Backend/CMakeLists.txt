# CMakeList.txt: 顶层 CMake 项目文件，在此处执行全局配置
# 并包含子项目。
#
cmake_minimum_required(VERSION 3.21)

# 如果支持，请为 MSVC 编译器启用热重载。
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("Backend")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4)
    # 启用多核并行编译
    add_compile_options(/MP)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
find_package(Drogon CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
# vcpkg: mysql-connector-cpp，经典 C++ API 用 mysql::concpp-jdbc，X DevAPI 用 mysql::concpp
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)

# redis-plus-plus exports redis++::redis++ (shared) or redis++::redis++_static
# depending on the vcpkg triplet; pick whichever is available.
if(TARGET redis++::redis++)
    set(REDIS_PP_TARGET redis++::redis++)
elseif(TARGET redis++::redis++_static)
    set(REDIS_PP_TARGET redis++::redis++_static)
else()
    message(FATAL_ERROR "redis-plus-plus found but no usable CMake target")
endif()

if(TARGET hiredis::hiredis)
    set(HIREDIS_TARGET hiredis::hiredis)
elseif(TARGET hiredis::hiredis_static)
    set(HIREDIS_TARGET hiredis::hiredis_static)
else()
    message(FATAL_ERROR "hiredis found but no usable CMake target")
endif()

find_package(miniocpp REQUIRED)

# 编译 ./src 下所有 .cpp（含子目录）；.h 通过 #include 参与，无需单独列出
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.h src/*.h)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES} ${HEADERS})

# 头文件搜索路径：include/ 放对外头文件，src/ 放实现用的 .h 便于 #include
target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Drogon::Drogon
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    jwt-cpp::jwt-cpp
    unofficial::mysql-connector-cpp::connector
    miniocpp::miniocpp
    ${REDIS_PP_TARGET}
    ${HIREDIS_TARGET}
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES config.json.example DESTINATION etc)

# ---------- tests ----------
option(BUILD_TESTS "Build test targets" ON)
if (BUILD_TESTS)
    add_subdirectory(tests)
endif()
