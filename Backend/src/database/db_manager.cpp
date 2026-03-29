#include "db_manager.h"

#include <chrono>
#include <stdexcept>

namespace database {

MysqlConfig parseMysqlConfig(const nlohmann::json& dbConfig) {
    MysqlConfig cfg;
    cfg.host = dbConfig.value("host", std::string("127.0.0.1"));
    cfg.port = dbConfig.value("port", 33060);
    cfg.user = dbConfig.value("username", std::string());
    cfg.password = dbConfig.value("password", std::string());
    cfg.database = dbConfig.value("database", std::string());
    if (dbConfig.contains("ssl") && dbConfig.at("ssl").is_boolean()) {
        cfg.ssl = dbConfig.at("ssl").get<bool>();
    }
    return cfg;
}

mysqlx::SessionSettings buildSessionSettings(const MysqlConfig& cfg) {
    if (cfg.host.empty()) {
        throw std::runtime_error("DB not initialized");
    }

    mysqlx::SessionSettings settings(cfg.host, cfg.port, cfg.user, cfg.password);
    if (cfg.ssl.has_value()) {
        settings.erase(mysqlx::SessionOption::SSL_MODE);
        settings.set(mysqlx::SessionOption::SSL_MODE,
                     *cfg.ssl ? mysqlx::SSLMode::REQUIRED : mysqlx::SSLMode::DISABLED);
    }
    return settings;
}

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kThreadSessionPingInterval = std::chrono::seconds(30);

struct ThreadSessionState {
    std::unique_ptr<mysqlx::Session> session;
    Clock::time_point last_verified_at{};
};

ThreadSessionState& currentThreadSessionState() {
    thread_local ThreadSessionState state;
    return state;
}

std::unique_ptr<mysqlx::Session> createSession(const MysqlConfig& cfg) {
    return std::make_unique<mysqlx::Session>(buildSessionSettings(cfg));
}

void markThreadSessionVerified(ThreadSessionState& state) {
    state.last_verified_at = Clock::now();
}

bool shouldPingThreadSession(const ThreadSessionState& state, Clock::time_point now) {
    return state.last_verified_at.time_since_epoch().count() == 0 ||
           (now - state.last_verified_at) >= kThreadSessionPingInterval;
}

} // namespace

std::unique_ptr<mysqlx::Session> DBManager::g_sess;
MysqlConfig DBManager::g_cfg;

void DBManager::init(const MysqlConfig& cfg) {
    g_cfg = cfg;
    g_sess = createSession(cfg);
}

const MysqlConfig& DBManager::config() {
    if (g_cfg.host.empty()) {
        throw std::runtime_error("DB not initialized");
    }

    return g_cfg;
}

bool DBManager::isHealthy()
{
	if (g_cfg.host.empty() || g_cfg.database.empty()) {
		return false;
	}

	try {
		auto& sess = threadSession();
		sess.sql("SELECT 1").execute();
		sess.getSchema(g_cfg.database, true);
		return true;
	} catch (const mysqlx::Error&) {
		resetThreadSession();
		try {
			auto& sess = threadSession();
			sess.sql("SELECT 1").execute();
			sess.getSchema(g_cfg.database, true);
			return true;
		} catch (const mysqlx::Error&) {
			return false;
		}
	}
}

mysqlx::Session& DBManager::session() {
    if (!g_sess)
        throw std::runtime_error("DB not initialized");
    return *g_sess;
}

mysqlx::Session& DBManager::threadSession() {
    auto& state = currentThreadSessionState();
    if (!state.session) {
        state.session = createSession(g_cfg);
        markThreadSessionVerified(state);
        return *state.session;
    }

    const auto now = Clock::now();
    if (!shouldPingThreadSession(state, now)) {
        return *state.session;
    }

    try {
        state.session->sql("SELECT 1").execute();
        state.last_verified_at = now;
    } catch (const mysqlx::Error&) {
        resetThreadSession();
        state.session = createSession(g_cfg);
        markThreadSessionVerified(state);
    }

    return *state.session;
}

mysqlx::Schema DBManager::schema() {
    if (!g_sess)
        throw std::runtime_error("DB not initialized");
    return g_sess->getSchema(g_cfg.database, true);
}

mysqlx::Schema DBManager::threadSchema() {
    return threadSession().getSchema(g_cfg.database, true);
}

void DBManager::resetThreadSession() {
    auto& state = currentThreadSessionState();
    if (!state.session) {
        return;
    }

    try {
        state.session->close();
    } catch (...) {
    }

    state.session.reset();
    state.last_verified_at = Clock::time_point{};
}

} // namespace database
