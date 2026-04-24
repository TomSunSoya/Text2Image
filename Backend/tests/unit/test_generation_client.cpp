#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "Backend.h"
#include "models/failure_code.h"
#include "models/image_generation.h"
#include "models/task_status.h"
#include "services/client.h"
#include "services/generation_client.h"
#include "services/i_http_client.h"
#include "services/image_service_types.h"

namespace {

// MockHttpClient implements IHttpClient for unit testing
class MockHttpClient : public IHttpClient {
  public:
    HttpResult postResult;
    HttpResult getResult;
    bool throwOnPost = false;
    bool throwOnGet = false;
    mutable std::optional<long> lastGetTimeoutSeconds;
    mutable std::optional<long> lastPostTimeoutSeconds;

    HttpResult get(const std::string&, long timeoutSeconds, const std::vector<std::string>&,
                   bool) const override {
        lastGetTimeoutSeconds = timeoutSeconds;
        if (throwOnGet) {
            throw std::runtime_error("mock get exception");
        }
        return getResult;
    }

    HttpResult postJson(const std::string&, long timeoutSeconds, const std::string&,
                        const std::vector<std::string>&) const override {
        lastPostTimeoutSeconds = timeoutSeconds;
        if (throwOnPost) {
            throw std::runtime_error("mock postJson exception");
        }
        return postResult;
    }
};

// Helper to construct HttpResult with given status, body, and error
HttpResult makeResult(long status, std::string body = "", std::string error = "") {
    HttpResult r;
    r.status_code = status;
    r.body = std::move(body);
    r.error = std::move(error);
    return r;
}

// ScopedEnvVar sets an environment variable and restores original value on destruction
class ScopedEnvVar {
  public:
    ScopedEnvVar(const char* name, std::optional<std::string> value)
        : name_(name), original_(readEnvVar(name)) {
        set(value);
    }

    ~ScopedEnvVar() {
        set(original_);
    }

  private:
    static std::optional<std::string> readEnvVar(const char* name) {
#ifdef _WIN32
        char* raw = nullptr;
        size_t size = 0;
        if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
            return std::nullopt;
        }
        std::string value(raw);
        free(raw);
#else
        const char* raw = std::getenv(name);
        if (raw == nullptr) {
            return std::nullopt;
        }
        std::string value(raw);
#endif
        return value;
    }

    void set(const std::optional<std::string>& value) {
#ifdef _WIN32
        if (value.has_value()) {
            _putenv_s(name_.c_str(), value->c_str());
        } else {
            _putenv_s(name_.c_str(), "");
        }
#else
        if (value.has_value()) {
            setenv(name_.c_str(), value->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
#endif
    }

    std::string name_;
    std::optional<std::string> original_;
};

// Writes a temporary config file with the sections cachedConfig consumers need.
std::filesystem::path writeTempConfig() {
    const auto fileName =
        "backend-gen-client-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
    const auto path = std::filesystem::temp_directory_path() / fileName;

    nlohmann::json config = {
        {"jwt", {{"secret", "unit-test-secret"}, {"expiration_hours", 24}}},
        {"python_service", {{"url", "http://localhost:8081"}, {"timeout_seconds", 30}}},
    };

    std::ofstream out(path);
    out << config.dump(2);
    out.close();

    return path;
}

// Test fixture that sets up config for GenerationClient tests
class GenerationClientTest : public ::testing::Test {
  protected:
    void SetUp() override {
        configPath_ = writeTempConfig();
        configEnv_.emplace("BACKEND_CONFIG_PATH", configPath_.string());
    }

    void TearDown() override {
        std::filesystem::remove(configPath_);
    }

  private:
    std::filesystem::path configPath_;
    std::optional<ScopedEnvVar> configEnv_;
};

} // namespace

// ==================== generate() tests ====================

TEST_F(GenerationClientTest, Http500ReturnsRequestFailed) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->postResult = makeResult(500, "", "Server Error");

    GenerationClient client(mock);
    models::ImageGeneration generation;
    generation.prompt = "test";

    auto result = client.generate(generation);

    EXPECT_EQ(result.status, models::TaskStatus::Failed);
    EXPECT_EQ(result.failure_code, std::string(models::failure::kPythonServiceRequestFailed));
}

TEST_F(GenerationClientTest, EmptyResponseBodyReturnsEmptyResponse) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->postResult = makeResult(200, "");

    GenerationClient client(mock);
    models::ImageGeneration generation;
    generation.prompt = "test";

    auto result = client.generate(generation);

    EXPECT_EQ(result.status, models::TaskStatus::Failed);
    EXPECT_EQ(result.failure_code, std::string(models::failure::kPythonServiceEmptyResponse));
}

TEST_F(GenerationClientTest, InvalidJsonReturnsInvalidJson) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->postResult = makeResult(200, "not valid json {{{");

    GenerationClient client(mock);
    models::ImageGeneration generation;
    generation.prompt = "test";

    auto result = client.generate(generation);

    EXPECT_EQ(result.status, models::TaskStatus::Failed);
    EXPECT_EQ(result.failure_code, std::string(models::failure::kPythonServiceInvalidJson));
}

TEST_F(GenerationClientTest, ExceptionThrownReturnsException) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->throwOnPost = true;

    GenerationClient client(mock);
    models::ImageGeneration generation;
    generation.prompt = "test";

    auto result = client.generate(generation);

    EXPECT_EQ(result.status, models::TaskStatus::Failed);
    EXPECT_EQ(result.failure_code, std::string(models::failure::kPythonServiceException));
}

TEST_F(GenerationClientTest, ValidJsonNoImageReturnsMissingImagePayload) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->postResult = makeResult(200, R"({"status":"success"})");

    GenerationClient client(mock);
    models::ImageGeneration generation;
    generation.prompt = "test";

    auto result = client.generate(generation);

    EXPECT_EQ(result.status, models::TaskStatus::Failed);
    EXPECT_EQ(result.failure_code, std::string(models::failure::kMissingImagePayload));
}

TEST_F(GenerationClientTest, NonTerminalStatusCoercedToFailed) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->postResult = makeResult(200, R"({"status":"generating"})");

    GenerationClient client(mock);
    models::ImageGeneration generation;
    generation.prompt = "test";

    auto result = client.generate(generation);

    EXPECT_EQ(result.status, models::TaskStatus::Failed);
    EXPECT_EQ(result.failure_code, std::string(models::failure::kMissingImagePayload));
}

// ==================== checkHealth() tests ====================

TEST_F(GenerationClientTest, CheckHealthReturnsHealthy) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->getResult = makeResult(200, R"({"status":"healthy","model_loaded":true})");

    GenerationClient client(mock);
    auto result = client.checkHealth();

    EXPECT_EQ(result.status, "healthy");
    EXPECT_TRUE(result.model_loaded);
}

TEST_F(GenerationClientTest, CheckHealthReturnsUnhealthyOnHttpError) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->getResult = makeResult(500, "", "Internal Server Error");

    GenerationClient client(mock);
    auto result = client.checkHealth();

    EXPECT_EQ(result.status, "unhealthy");
}

TEST_F(GenerationClientTest, CheckHealthReturnsUnhealthyOnEmptyResponse) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->getResult = makeResult(200, "");

    GenerationClient client(mock);
    auto result = client.checkHealth();

    EXPECT_EQ(result.status, "unhealthy");
    EXPECT_EQ(result.detail, "empty response body");
}

TEST_F(GenerationClientTest, CheckHealthReturnsUnhealthyOnInvalidJson) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->getResult = makeResult(200, "not json {{{");

    GenerationClient client(mock);
    auto result = client.checkHealth();

    EXPECT_EQ(result.status, "unhealthy");
    EXPECT_EQ(result.detail, "invalid json response");
}

TEST_F(GenerationClientTest, CheckHealthReturnsUnhealthyOnException) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->throwOnGet = true;

    GenerationClient client(mock);
    auto result = client.checkHealth();

    EXPECT_EQ(result.status, "unhealthy");
}

// ==================== timeout propagation tests ====================

TEST_F(GenerationClientTest, GenerateUsesConfiguredTimeout) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->postResult = makeResult(200, R"({"status":"success"})");

    GenerationClient client(mock);
    models::ImageGeneration generation;
    generation.prompt = "test";

    (void)client.generate(generation);

    ASSERT_TRUE(mock->lastPostTimeoutSeconds.has_value());
    // writeTempConfig sets python_service.timeout_seconds = 30
    EXPECT_EQ(*mock->lastPostTimeoutSeconds, 30L);
}

TEST_F(GenerationClientTest, CheckHealthClampsTimeoutToTenSeconds) {
    auto mock = std::make_shared<MockHttpClient>();
    mock->getResult = makeResult(200, R"({"status":"healthy","model_loaded":true})");

    GenerationClient client(mock);
    (void)client.checkHealth();

    ASSERT_TRUE(mock->lastGetTimeoutSeconds.has_value());
    // checkHealth caps timeout at 10s even if config specifies 30s
    EXPECT_LE(*mock->lastGetTimeoutSeconds, 10L);
}
