#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "services/metrics_cache_client.h"

namespace {

struct SetexCall {
    std::string key;
    std::string value;
    std::chrono::seconds ttl;
};

class RecordingCacheClient : public cache::ICacheClient {
  public:
    bool available{true};
    bool setex_result{true};
    bool del_result{true};
    std::optional<int64_t> bump_result{12};
    int64_t version_result{34};
    std::unordered_map<std::string, std::string> values;
    mutable std::vector<std::string> get_keys;
    std::vector<SetexCall> setex_calls;
    std::vector<std::string> del_keys;
    std::vector<std::pair<std::string, std::string>> bump_calls;
    mutable std::vector<std::pair<std::string, std::string>> get_version_calls;

    [[nodiscard]] bool isAvailable() const noexcept override {
        return available;
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const override {
        get_keys.emplace_back(key);
        if (!available) {
            return std::nullopt;
        }

        const auto it = values.find(std::string{key});
        if (it == values.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool setex(std::string_view key, std::string_view value, std::chrono::seconds ttl) override {
        setex_calls.push_back({std::string{key}, std::string{value}, ttl});
        return setex_result;
    }

    bool del(std::string_view key) override {
        del_keys.emplace_back(key);
        return del_result;
    }

    std::optional<int64_t> bumpVersion(std::string_view ns, std::string_view identifier) override {
        bump_calls.emplace_back(std::string{ns}, std::string{identifier});
        return bump_result;
    }

    int64_t getVersion(std::string_view ns, std::string_view identifier) const override {
        get_version_calls.emplace_back(std::string{ns}, std::string{identifier});
        return version_result;
    }
};

} // namespace

TEST(MetricsCacheClient, ConstructorRejectsNullDependencies) {
    auto inner = std::make_shared<RecordingCacheClient>();
    auto metrics = std::make_shared<cache::CacheMetrics>();

    EXPECT_THROW(cache::MetricsCacheClient(nullptr, metrics), std::invalid_argument);
    EXPECT_THROW(cache::MetricsCacheClient(inner, nullptr), std::invalid_argument);
}

TEST(MetricsCacheClient, GetHitIncrementsHitsAndReturnsValue) {
    auto inner = std::make_shared<RecordingCacheClient>();
    auto metrics = std::make_shared<cache::CacheMetrics>();
    inner->values["img:meta:7:42"] = "cached";
    cache::MetricsCacheClient client(inner, metrics);

    const auto result = client.get("img:meta:7:42");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "cached");
    EXPECT_EQ(inner->get_keys, std::vector<std::string>{"img:meta:7:42"});
    const auto snap = metrics->snapshotFor(cache::Namespace::Meta);
    EXPECT_EQ(snap.hits, 1);
    EXPECT_EQ(snap.misses, 0);
    EXPECT_EQ(snap.degraded, 0);
}

TEST(MetricsCacheClient, GetMissIncrementsMissesWhenAvailable) {
    auto inner = std::make_shared<RecordingCacheClient>();
    auto metrics = std::make_shared<cache::CacheMetrics>();
    cache::MetricsCacheClient client(inner, metrics);

    const auto result = client.get("img:list:my:7:v0:p0:s10");

    EXPECT_FALSE(result.has_value());
    const auto snap = metrics->snapshotFor(cache::Namespace::List);
    EXPECT_EQ(snap.hits, 0);
    EXPECT_EQ(snap.misses, 1);
    EXPECT_EQ(snap.degraded, 0);
}

TEST(MetricsCacheClient, GetDegradedIncrementsDegradedWhenUnavailable) {
    auto inner = std::make_shared<RecordingCacheClient>();
    auto metrics = std::make_shared<cache::CacheMetrics>();
    inner->available = false;
    cache::MetricsCacheClient client(inner, metrics);

    const auto result = client.get("img:url:images/7/42.png");

    EXPECT_FALSE(result.has_value());
    const auto snap = metrics->snapshotFor(cache::Namespace::Url);
    EXPECT_EQ(snap.hits, 0);
    EXPECT_EQ(snap.misses, 0);
    EXPECT_EQ(snap.degraded, 1);
}

TEST(MetricsCacheClient, GetClassifiesByKeyPrefix) {
    auto inner = std::make_shared<RecordingCacheClient>();
    auto metrics = std::make_shared<cache::CacheMetrics>();
    inner->values["img:meta:7:42"] = "meta";
    inner->values["img:list:my:7:v0:p0:s10"] = "list";
    inner->values["img:url:images/7/42.png"] = "url";
    cache::MetricsCacheClient client(inner, metrics);

    EXPECT_EQ(client.get("img:meta:7:42"), std::optional<std::string>{"meta"});
    EXPECT_EQ(client.get("img:list:my:7:v0:p0:s10"), std::optional<std::string>{"list"});
    EXPECT_EQ(client.get("img:url:images/7/42.png"), std::optional<std::string>{"url"});

    EXPECT_EQ(metrics->snapshotFor(cache::Namespace::Meta).hits, 1);
    EXPECT_EQ(metrics->snapshotFor(cache::Namespace::List).hits, 1);
    EXPECT_EQ(metrics->snapshotFor(cache::Namespace::Url).hits, 1);
    EXPECT_EQ(metrics->snapshotFor(cache::Namespace::Other).hits, 0);
}

TEST(MetricsCacheClient, ForwardsNonGetOperationsWithoutIncrementingCounters) {
    auto inner = std::make_shared<RecordingCacheClient>();
    auto metrics = std::make_shared<cache::CacheMetrics>();
    cache::MetricsCacheClient client(inner, metrics);

    EXPECT_TRUE(client.isAvailable());
    EXPECT_TRUE(client.setex("img:meta:7:42", "value", std::chrono::seconds{15}));
    EXPECT_TRUE(client.del("img:meta:7:42"));
    EXPECT_EQ(client.bumpVersion("img:list", "7"), std::optional<int64_t>{12});
    EXPECT_EQ(client.getVersion("img:list", "7"), 34);

    ASSERT_EQ(inner->setex_calls.size(), 1);
    EXPECT_EQ(inner->setex_calls[0].key, "img:meta:7:42");
    EXPECT_EQ(inner->setex_calls[0].value, "value");
    EXPECT_EQ(inner->setex_calls[0].ttl, std::chrono::seconds{15});
    EXPECT_EQ(inner->del_keys, std::vector<std::string>{"img:meta:7:42"});
    EXPECT_EQ(inner->bump_calls,
              (std::vector<std::pair<std::string, std::string>>{{"img:list", "7"}}));
    EXPECT_EQ(inner->get_version_calls,
              (std::vector<std::pair<std::string, std::string>>{{"img:list", "7"}}));

    for (const auto ns : {cache::Namespace::Meta, cache::Namespace::List, cache::Namespace::Url,
                          cache::Namespace::Other}) {
        const auto snap = metrics->snapshotFor(ns);
        EXPECT_EQ(snap.hits, 0);
        EXPECT_EQ(snap.misses, 0);
        EXPECT_EQ(snap.degraded, 0);
    }
}
