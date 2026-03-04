#include "db_manager.h"
#include <stdexcept>

namespace database {

std::unique_ptr<mysqlx::Session> DBManager::g_sess;
MysqlConfig DBManager::g_cfg;

void DBManager::init(const MysqlConfig& cfg)
{
	g_cfg = cfg;

	mysqlx::SessionSettings settings(cfg.host, cfg.port, cfg.user, cfg.password);

	g_sess = std::make_unique<mysqlx::Session>(settings);
}

mysqlx::Session& DBManager::session() {
	if (!g_sess) throw std::runtime_error("DB not initialized");
	return *g_sess;
}

mysqlx::Schema DBManager::schema() {
	if (!g_sess) throw std::runtime_error("DB not initialized");
	return g_sess->getSchema(g_cfg.database, true);
}

}