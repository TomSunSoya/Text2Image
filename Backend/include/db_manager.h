#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <mysqlx/xdevapi.h>
#include <nlohmann/json.hpp>

#include "user.h"
#include "image_generation.h"

namespace database {

struct MysqlConfig {
    std::string host;
    int port{};
    std::string database;
    std::string user;
    std::string password;
    std::optional<bool> ssl;
};

MysqlConfig parseMysqlConfig(const nlohmann::json& dbConfig);
mysqlx::SessionSettings buildSessionSettings(const MysqlConfig& cfg);

class DBManager {
  public:
    static void init(const MysqlConfig& cfg);
    static const MysqlConfig& config();
    static mysqlx::Session& session();
    static mysqlx::Session& threadSession();
    static mysqlx::Schema schema();
    static mysqlx::Schema threadSchema();

  private:
    static void resetThreadSession();
    static std::unique_ptr<mysqlx::Session> g_sess;
    static MysqlConfig g_cfg;
};
} // namespace database
