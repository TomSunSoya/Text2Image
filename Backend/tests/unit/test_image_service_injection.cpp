#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "database/i_image_repo.h"
#include "models/i_image_storage.h"
#include "services/image_service.h"

namespace {

class FakeImageRepo : public IImageRepo {
  public:
    int64_t insert(const models::ImageGeneration&) override {
        return 1;
    }

    ImagePageResult findByUserId(int64_t, int, int) override {
        return {};
    }

    ImagePageResult findByUserIdAndStatus(int64_t, models::TaskStatus, int, int) override {
        return {};
    }

    std::optional<models::ImageGeneration> findByIdAndUserId(int64_t, int64_t) override {
        return std::nullopt;
    }

    std::optional<models::ImageGeneration> findByRequestIdAndUserId(const std::string&,
                                                                    int64_t) override {
        return std::nullopt;
    }

    bool deleteByIdAndUserId(int64_t, int64_t) override {
        return false;
    }

    bool cancelByIdAndUserId(int64_t, int64_t, models::ImageGeneration*) override {
        return false;
    }

    bool retryByIdAndUserId(int64_t, int64_t, models::ImageGeneration*) override {
        return false;
    }

    std::vector<ExpiredLease> expireLeasesReturningExpired() override {
        return {};
    }
};

class FakeImageStorage : public IImageStorage {
  public:
    std::optional<std::string> getBytes(const std::string&) const override {
        return std::nullopt;
    }

    std::string presignUrl(const std::string&, int) const override {
        return {};
    }

    bool remove(const std::string&) const override {
        return false;
    }

    std::string contentTypeForKey(const std::string&) const override {
        return "image/png";
    }
};

} // namespace

TEST(ImageServiceInjection, RejectsNullRepoDependency) {
    auto storage = std::make_shared<FakeImageStorage>();

    EXPECT_THROW((ImageService(nullptr, storage)), std::invalid_argument);
}

TEST(ImageServiceInjection, RejectsNullStorageDependency) {
    auto repo = std::make_shared<FakeImageRepo>();

    EXPECT_THROW((ImageService(repo, nullptr)), std::invalid_argument);
}
