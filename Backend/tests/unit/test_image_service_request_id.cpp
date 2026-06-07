#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "services/image_service.h"
#include "image_service_test_fakes.h"

namespace {

using image_service_test_fakes::FakeImageRepo;
using image_service_test_fakes::FakeImageStorage;
using image_service_test_fakes::makeService;
using image_service_test_fakes::SpyCacheClient;

nlohmann::json payloadWithRequestId(std::string requestId) {
    return {{"requestId", std::move(requestId)},
            {"prompt", "a valid image generation prompt"},
            {"negativePrompt", ""},
            {"numSteps", 8},
            {"height", 768},
            {"width", 768}};
}

struct Fixture {
    std::shared_ptr<FakeImageRepo> repo = std::make_shared<FakeImageRepo>();
    std::shared_ptr<FakeImageStorage> storage = std::make_shared<FakeImageStorage>();
    std::shared_ptr<SpyCacheClient> cache = std::make_shared<SpyCacheClient>();

    ImageService service() {
        return makeService(repo, storage, cache);
    }
};

} // namespace

TEST(ImageServiceRequestId, AcceptsBusinessRequestIdShape) {
    Fixture f;
    auto service = f.service();

    const auto result = service.create(7, payloadWithRequestId("img-1700000000-deadbeef"));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.request_id, "img-1700000000-deadbeef");
    EXPECT_EQ(f.repo->insert_calls, 1);
}

TEST(ImageServiceRequestId, GeneratesIdWhenEmpty) {
    Fixture f;
    auto service = f.service();

    const auto result = service.create(7, payloadWithRequestId(""));

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->generation.request_id.empty());
    EXPECT_TRUE(result->generation.request_id.starts_with("img-"));
    EXPECT_EQ(f.repo->insert_calls, 1);
}

TEST(ImageServiceRequestId, TrimsSurroundingWhitespace) {
    Fixture f;
    auto service = f.service();

    const auto result = service.create(7, payloadWithRequestId("  dedup-001  "));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.request_id, "dedup-001");
}

TEST(ImageServiceRequestId, AcceptsMaxLengthRequestId) {
    Fixture f;
    auto service = f.service();

    const std::string id(64, 'a');
    const auto result = service.create(7, payloadWithRequestId(id));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.request_id, id);
}

TEST(ImageServiceRequestId, RejectsCrlfHeaderInjection) {
    Fixture f;
    auto service = f.service();

    const auto result = service.create(7, payloadWithRequestId("abc\r\nX-Evil: pwned"));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k400BadRequest);
    EXPECT_EQ(result.error().code, "invalid_request_id");
    EXPECT_EQ(f.repo->insert_calls, 0);
}

TEST(ImageServiceRequestId, RejectsControlCharacters) {
    Fixture f;
    auto service = f.service();

    const auto result = service.create(7, payloadWithRequestId(std::string("a\x01"
                                                                           "b")));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "invalid_request_id");
    EXPECT_EQ(f.repo->insert_calls, 0);
}

TEST(ImageServiceRequestId, RejectsEmbeddedSpace) {
    Fixture f;
    auto service = f.service();

    const auto result = service.create(7, payloadWithRequestId("has space"));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "invalid_request_id");
    EXPECT_EQ(f.repo->insert_calls, 0);
}

TEST(ImageServiceRequestId, RejectsDisallowedPunctuation) {
    Fixture f;
    auto service = f.service();

    // '.' and ':' are allowed for trace headers but not for the idempotency key.
    const auto result = service.create(7, payloadWithRequestId("trace.42:abc"));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "invalid_request_id");
    EXPECT_EQ(f.repo->insert_calls, 0);
}

TEST(ImageServiceRequestId, RejectsLeadingNonAlphanumeric) {
    Fixture f;
    auto service = f.service();

    const auto result = service.create(7, payloadWithRequestId("-abc"));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "invalid_request_id");
    EXPECT_EQ(f.repo->insert_calls, 0);
}

TEST(ImageServiceRequestId, RejectsOverlongRequestId) {
    Fixture f;
    auto service = f.service();

    const auto result = service.create(7, payloadWithRequestId(std::string(65, 'a')));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "invalid_request_id");
    EXPECT_EQ(f.repo->insert_calls, 0);
}
