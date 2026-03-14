#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct StoredImage {
	std::string storage_key;
	std::string image_url;
	std::string content_type{ "image/png" };
};

class ImageStorage {
public:
	StoredImage storeBase64(int64_t taskId, const std::string requestId, const std::string& imageBase64) const;
	
	std::optional<std::string> loadBytes(const std::string& storageKey, std::string& error) const;

	std::optional<std::string> loadBase64(const std::string& storageKey, std::string& error) const;

	std::string contentTypeForKey(const std::string& storageKey) const;
};