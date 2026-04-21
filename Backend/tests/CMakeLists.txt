find_package(GTest CONFIG REQUIRED)

# Collect all library sources (exclude main.cpp to avoid duplicate main)
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

# Common include dirs and link libs shared by all test targets
set(TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

set(TEST_LIBS
    GTest::gtest_main
    Drogon::Drogon
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    jwt-cpp::jwt-cpp
    unofficial::mysql-connector-cpp::connector
    ${MYSQL_CONNECTOR_EXTRA_LIBS}
    miniocpp::miniocpp
    ${REDIS_PP_TARGET}
    ${HIREDIS_TARGET}
)

# ---------- Unit tests (no database needed) ----------
file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp)
if (UNIT_TEST_SOURCES)
    add_executable(unit_tests ${UNIT_TEST_SOURCES} ${LIB_SOURCES})
    target_include_directories(unit_tests PRIVATE ${TEST_INCLUDES})
    target_link_libraries(unit_tests PRIVATE ${TEST_LIBS})
    add_test(NAME UnitTests COMMAND unit_tests)
endif()

# ---------- Integration tests (require test database) ----------
file(GLOB INTEG_TEST_SOURCES CONFIGURE_DEPENDS integration/*.cpp)
set(TASK_ENGINE_INTEG_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_task_engine_lazy_bootstrap.cpp)
list(FILTER INTEG_TEST_SOURCES EXCLUDE REGEX "integration/test_task_engine_lazy_bootstrap\\.cpp$")
if (INTEG_TEST_SOURCES)
    add_executable(integration_tests ${INTEG_TEST_SOURCES} ${LIB_SOURCES})
    target_include_directories(integration_tests PRIVATE ${TEST_INCLUDES})
    target_link_libraries(integration_tests PRIVATE ${TEST_LIBS})
    add_test(NAME IntegrationTests COMMAND integration_tests)
endif()

if (EXISTS ${TASK_ENGINE_INTEG_SOURCE})
    add_executable(task_engine_integration_tests
        ${TASK_ENGINE_INTEG_SOURCE}
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_db_support.cpp
        ${LIB_SOURCES}
    )
    target_include_directories(task_engine_integration_tests PRIVATE ${TEST_INCLUDES})
    target_link_libraries(task_engine_integration_tests PRIVATE
        GTest::gtest
        Drogon::Drogon
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        jwt-cpp::jwt-cpp
        unofficial::mysql-connector-cpp::connector
        ${MYSQL_CONNECTOR_EXTRA_LIBS}
        miniocpp::miniocpp
        ${REDIS_PP_TARGET}
        ${HIREDIS_TARGET}
    )
    add_test(NAME TaskEngineIntegrationTests COMMAND task_engine_integration_tests)
endif()
