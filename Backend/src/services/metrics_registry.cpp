#include "services/metrics_registry.h"

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <sstream>

namespace metrics {

namespace {

constexpr std::array<double, 8> kDurationBuckets{0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0};

std::string escapeLabelValue(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string statusLabel(int statusCode) {
    return std::to_string(statusCode <= 0 ? 0 : statusCode);
}

void renderHistogram(std::ostringstream& out, std::string_view name, const auto& histograms) {
    for (const auto& [labels, histogram] : histograms) {
        uint64_t cumulative = 0;
        for (size_t i = 0; i < kDurationBuckets.size(); ++i) {
            cumulative += histogram.buckets[i];
            out << name << "_bucket{" << labels << ",le=\"" << kDurationBuckets[i] << "\"} "
                << cumulative << '\n';
        }
        out << name << "_bucket{" << labels << ",le=\"+Inf\"} " << histogram.count << '\n';
        out << name << "_sum{" << labels << "} " << histogram.sum << '\n';
        out << name << "_count{" << labels << "} " << histogram.count << '\n';
    }
}

} // namespace

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry registry;
    return registry;
}

std::string requestLabels(std::string_view endpoint, int statusCode) {
    return std::format("endpoint=\"{}\",status=\"{}\"", escapeLabelValue(endpoint),
                       statusLabel(statusCode));
}

std::string modelServiceLabels(std::string_view endpoint, std::string_view status) {
    return std::format("endpoint=\"{}\",status=\"{}\"", escapeLabelValue(endpoint),
                       escapeLabelValue(status));
}

void MetricsRegistry::resetForTests() {
    std::lock_guard lock(mutex_);
    request_durations_.clear();
    model_service_durations_.clear();
    task_totals_.clear();
    worker_queue_depth_ = 0;
    db_pool_active_connections_ = 0;
    db_pool_idle_connections_ = 0;
    db_pool_configured_connections_ = 0;
}

void MetricsRegistry::observeHistogram(std::map<std::string, Histogram>& histograms,
                                       std::string key, double seconds) {
    auto& histogram = histograms[std::move(key)];
    if (histogram.buckets.empty()) {
        histogram.buckets.resize(kDurationBuckets.size(), 0);
    }

    const auto safeSeconds = (std::max)(0.0, seconds);
    const auto bucketIt =
        std::ranges::lower_bound(kDurationBuckets, safeSeconds, std::less<double>{});
    if (bucketIt != kDurationBuckets.end()) {
        const auto index = static_cast<size_t>(std::distance(kDurationBuckets.begin(), bucketIt));
        ++histogram.buckets[index];
    }
    ++histogram.count;
    histogram.sum += safeSeconds;
}

void MetricsRegistry::observeRequest(std::string_view endpoint, int statusCode, double seconds) {
    std::lock_guard lock(mutex_);
    observeHistogram(request_durations_, requestLabels(endpoint, statusCode), seconds);
}

void MetricsRegistry::observeModelServiceCall(std::string_view endpoint, std::string_view status,
                                              double seconds) {
    std::lock_guard lock(mutex_);
    observeHistogram(model_service_durations_, modelServiceLabels(endpoint, status), seconds);
}

void MetricsRegistry::recordTaskStatus(models::TaskStatus status) {
    std::lock_guard lock(mutex_);
    ++task_totals_[std::string(models::statusToString(status))];
}

void MetricsRegistry::incrementWorkerQueueDepth(int64_t delta) {
    std::lock_guard lock(mutex_);
    worker_queue_depth_ =
        (std::max)(int64_t{0}, worker_queue_depth_ + (std::max)(int64_t{0}, delta));
}

void MetricsRegistry::decrementWorkerQueueDepth(int64_t delta) {
    std::lock_guard lock(mutex_);
    worker_queue_depth_ =
        (std::max)(int64_t{0}, worker_queue_depth_ - (std::max)(int64_t{0}, delta));
}

void MetricsRegistry::setWorkerQueueDepth(int64_t depth) {
    std::lock_guard lock(mutex_);
    worker_queue_depth_ = (std::max)(int64_t{0}, depth);
}

void MetricsRegistry::setDbPoolStats(int64_t activeConnections, int64_t idleConnections) {
    std::lock_guard lock(mutex_);
    db_pool_active_connections_ = (std::max)(int64_t{0}, activeConnections);
    db_pool_idle_connections_ = (std::max)(int64_t{0}, idleConnections);
}

void MetricsRegistry::setDbPoolCapacity(int64_t configuredConnections) {
    std::lock_guard lock(mutex_);
    db_pool_configured_connections_ = (std::max)(int64_t{0}, configuredConnections);
}

std::string MetricsRegistry::renderPrometheus() const {
    std::lock_guard lock(mutex_);
    std::ostringstream out;

    out << "# HELP image_request_duration_seconds Backend request latency by handler and status.\n";
    out << "# TYPE image_request_duration_seconds histogram\n";
    renderHistogram(out, "image_request_duration_seconds", request_durations_);

    out << "# HELP image_task_total Image task state transition counter.\n";
    out << "# TYPE image_task_total counter\n";
    for (const auto& [status, count] : task_totals_) {
        out << "image_task_total{status=\"" << escapeLabelValue(status) << "\"} " << count << '\n';
    }

    out << "# HELP worker_queue_depth Approximate image worker queue depth.\n";
    out << "# TYPE worker_queue_depth gauge\n";
    out << "worker_queue_depth " << worker_queue_depth_ << '\n';

    out << "# HELP db_pool_active_connections Active database connections known to Backend.\n";
    out << "# TYPE db_pool_active_connections gauge\n";
    out << "db_pool_active_connections " << db_pool_active_connections_ << '\n';
    out << "# HELP db_pool_idle Idle database connections known to Backend.\n";
    out << "# TYPE db_pool_idle gauge\n";
    out << "db_pool_idle " << db_pool_idle_connections_ << '\n';
    out << "# HELP db_pool_configured_connections Configured database connection budget per "
           "Backend replica.\n";
    out << "# TYPE db_pool_configured_connections gauge\n";
    out << "db_pool_configured_connections " << db_pool_configured_connections_ << '\n';

    out << "# HELP model_service_call_duration_seconds Backend outbound model-service call "
           "latency.\n";
    out << "# TYPE model_service_call_duration_seconds histogram\n";
    renderHistogram(out, "model_service_call_duration_seconds", model_service_durations_);

    return out.str();
}

} // namespace metrics
