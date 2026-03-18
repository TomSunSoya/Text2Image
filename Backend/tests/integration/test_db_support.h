#pragma once

#include <string>

#include "db_manager.h"

namespace test_support {

void ensureTestDatabase();
void cleanUsers();
void cleanTables();
database::MysqlConfig testDbConfig();
std::string qualifiedTableName(const std::string& tableName);

} // namespace test_support
