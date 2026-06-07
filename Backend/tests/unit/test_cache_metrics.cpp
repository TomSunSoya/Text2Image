#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "services/cache_metrics.h"

TEST(CacheMetricsClassifyKey, RecognizesMetaPrefix) {
    EXPECT_EQ(cache::classifyKey("img:meta:7:42"), cache::Namespace::Meta);
}

TEST(CacheMetricsClassifyKey, RecognizesListPrefix) {
    EXPECT_EQ(cache::classifyKey("img:list:my:7:v1:p0:s10"), cache::Namespace::List);
}

TEST(CacheMetricsClassifyKey, RecognizesUrlPrefix) {
    EXPECT_EQ(cache::classifyKey("img:url:images/7/42.png"), cache::Namespace::Url);
}

TEST(CacheMetricsClassifyKey, RecognizesVersionKeyAsList) {
    EXPECT_EQ(cache::classifyKey("ver:img:list:7"), cache::Namespace::List);
}

TEST(CacheMetricsClassifyKey, UnknownPrefixIsOther) {
    EXPECT_EQ(cache::classifyKey("sessions:7"), cache::Namespace::Other);
    EXPECT_EQ(cache::classifyKey(""), cache::Namespace::Other);
}

TEST(CacheMetrics, RecordHitIncrementsCounter) {
    cache::CacheMetrics metrics;

    metrics.recordHit(cache::Namespace::Meta);

    const auto snap = metrics.snapshotFor(cache::Namespace::Meta);
    EXPECT_EQ(snap.hits, 1);
    EXPECT_EQ(snap.misses, 0);
    EXPECT_EQ(snap.degraded, 0);
}

TEST(CacheMetrics, RecordsAreNamespaceIsolated) {
    cache::CacheMetrics metrics;

    metrics.recordHit(cache::Namespace::Meta);
    metrics.recordMiss(cache::Namespace::List);
    metrics.recordDegraded(cache::Namespace::Url);

    const auto meta = metrics.snapshotFor(cache::Namespace::Meta);
    const auto list = metrics.snapshotFor(cache::Namespace::List);
    const auto url = metrics.snapshotFor(cache::Namespace::Url);
    const auto other = metrics.snapshotFor(cache::Namespace::Other);

    EXPECT_EQ(meta.hits, 1);
    EXPECT_EQ(meta.misses, 0);
    EXPECT_EQ(meta.degraded, 0);
    EXPECT_EQ(list.hits, 0);
    EXPECT_EQ(list.misses, 1);
    EXPECT_EQ(list.degraded, 0);
    EXPECT_EQ(url.hits, 0);
    EXPECT_EQ(url.misses, 0);
    EXPECT_EQ(url.degraded, 1);
    EXPECT_EQ(other.hits, 0);
    EXPECT_EQ(other.misses, 0);
    EXPECT_EQ(other.degraded, 0);
}

TEST(CacheMetrics, RecordsAreThreadSafe) {
    cache::CacheMetrics metrics;
    constexpr int kThreads = 4;
    constexpr int kIterations = 10000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&metrics] {
            for (int j = 0; j < kIterations; ++j) {
                metrics.recordHit(cache::Namespace::Meta);
                metrics.recordMiss(cache::Namespace::Meta);
                metrics.recordDegraded(cache::Namespace::Meta);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    const auto snap = metrics.snapshotFor(cache::Namespace::Meta);
    constexpr auto kExpected = static_cast<uint64_t>(kThreads) * kIterations;
    EXPECT_EQ(snap.hits, kExpected);
    EXPECT_EQ(snap.misses, kExpected);
    EXPECT_EQ(snap.degraded, kExpected);
}

TEST(CacheMetrics, ToJsonSnapshotsAllNamespaces) {
    cache::CacheMetrics metrics;
    metrics.recordHit(cache::Namespace::Meta);
    metrics.recordMiss(cache::Namespace::List);
    metrics.recordDegraded(cache::Namespace::Url);
    metrics.recordHit(cache::Namespace::Other);
    metrics.recordMiss(cache::Namespace::Other);
    metrics.recordDegraded(cache::Namespace::Other);

    const auto json = metrics.toJson();

    ASSERT_TRUE(json.contains("namespaces"));
    const auto& namespaces = json.at("namespaces");
    EXPECT_EQ(namespaces.at("meta").at("hits").get<uint64_t>(), 1);
    EXPECT_EQ(namespaces.at("meta").at("misses").get<uint64_t>(), 0);
    EXPECT_EQ(namespaces.at("meta").at("degraded").get<uint64_t>(), 0);
    EXPECT_EQ(namespaces.at("list").at("hits").get<uint64_t>(), 0);
    EXPECT_EQ(namespaces.at("list").at("misses").get<uint64_t>(), 1);
    EXPECT_EQ(namespaces.at("list").at("degraded").get<uint64_t>(), 0);
    EXPECT_EQ(namespaces.at("url").at("hits").get<uint64_t>(), 0);
    EXPECT_EQ(namespaces.at("url").at("misses").get<uint64_t>(), 0);
    EXPECT_EQ(namespaces.at("url").at("degraded").get<uint64_t>(), 1);
    EXPECT_EQ(namespaces.at("other").at("hits").get<uint64_t>(), 1);
    EXPECT_EQ(namespaces.at("other").at("misses").get<uint64_t>(), 1);
    EXPECT_EQ(namespaces.at("other").at("degraded").get<uint64_t>(), 1);
}
