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

mysqlx::Session& DBManager::threadSession() {
	thread_local std::unique_ptr<mysqlx::Session> tls;
	if (!tls) {
		if (g_cfg.host.empty()) throw std::runtime_error("DB not initialized");
		mysqlx::SessionSettings settings(g_cfg.host, g_cfg.port, g_cfg.user, g_cfg.password);
		tls = std::make_unique<mysqlx::Session>(settings);
	}
	return *tls;
}

mysqlx::Schema DBManager::schema() {
	if (!g_sess) throw std::runtime_error("DB not initialized");
	return g_sess->getSchema(g_cfg.database, true);
}

mysqlx::Schema DBManager::threadSchema() {
	return threadSession().getSchema(g_cfg.database, true);
}

}