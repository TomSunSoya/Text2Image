#include "image_storage.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <openssl/evp.h>

#include "Backend.h"

namespace {
	namespace fs = std::filesystem;

	struct StorageConfig {
		fs::path root_dir{ "storage/images" };
		std::string public_url_prefix{ "/api/images" };
		std::string extension{ "png" };
	};

	StorageConfig loadStorageConfg() {
		StorageConfig cfg;

		try {
			const auto config = backend::loadConfig();
			if (config.contains("storage") && config.at("storage").is_object()) {
				const auto& storageConfig = config.at("storage");
				cfg.root_dir = storageConfig.value("root_dir", cfg.root_dir.string());
				cfg.public_url_prefix = storageConfig.value("public_url_prefix", cfg.public_url_prefix);
				cfg.extension = storageConfig.value("extension", cfg.extension);
			}
		}
		catch (...) {

		}
		return cfg;
	}

	std::string sanitizeKeyPart(std::string value) {
		for (auto& ch : value) {
			const auto c = static_cast<unsigned char>(ch);
			if (!(std::isalnum(c) || ch == '-' || ch == '_')) {
				ch = '_';
			}
		}

		if (value.empty())
			value = "task";

		return value;
	}

	std::string encodeToBase64(const std::string& bytes) {
		if (bytes.empty()) {
			return {};
		}

		std::string out((bytes.size() + 2) / 3 * 4, '\0');
		const int len = EVP_EncodeBlock(
			reinterpret_cast<unsigned char*>(&out[0]),
			reinterpret_cast<const unsigned char*>(bytes.data()),
			static_cast<int>(bytes.size()));

		if (len <= 0)
			return {};

		out.resize(static_cast<size_t>(len));
		return out;
	}

	std::string decodeBase64(std::string base64) {
		base64.erase(
			std::remove_if(base64.begin(), base64.end(), [](unsigned char c) {
				return std::isspace(c);
				}),
			base64.end()
		);
		
		std::string out(base64.size() / 4 * 3, '\0');

		const int decoded = EVP_DecodeBlock(
			reinterpret_cast<unsigned char*>(&out[0]),
			reinterpret_cast<const unsigned char*>(base64.data()),
			static_cast<int>(base64.size()));

		if (decoded < 0)
			throw std::runtime_error("invalid base64 payload");

		int padding = 0;
		if (!base64.empty() && base64.back() == '=')
			++padding;
		if (base64.size() > 1 && base64[base64.size() - 2] == '=')
			++padding;

		out.resize(static_cast<size_t>(decoded - padding));
		return out;
	}

	fs::path filePathForKey(const fs::path& rootDir, const std::string& storageKey) {
		return rootDir / storageKey;
	}
}


StoredImage ImageStorage::storeBase64(int64_t taskId, const std::string requestId, const std::string& imageBase64) const
{
	if (taskId <= 0)
		throw std::runtime_error("taskId must be positive");

	if (imageBase64.empty())
		throw std::runtime_error("imageBase64 is empty");

	const auto cfg = loadStorageConfg();
	const auto safeRequestId = sanitizeKeyPart(requestId);
	const auto extension = sanitizeKeyPart(cfg.extension);
	const auto storageKey = "task-" + std::to_string(taskId) + "-" + safeRequestId + "." + extension;
	const auto path = filePathForKey(cfg.root_dir, storageKey);

	fs::create_directories(path.parent_path());

	const auto bytes = decodeBase64(imageBase64);

	std::ofstream output(path, std::ios::binary);
	if (!output)
		throw std::runtime_error("failed to open file for writing: " + path.string());

	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	if (!output)
		throw std::runtime_error("failed to write image bytes: " + path.string());

	StoredImage stored;
	stored.storage_key = storageKey;
	stored.image_url = cfg.public_url_prefix + "/" + std::to_string(taskId) + "/binary";
	stored.content_type = contentTypeForKey(storageKey);
	return stored;
}

std::optional<std::string> ImageStorage::loadBytes(const std::string& storageKey, std::string& error) const
{
	error.clear();

	if (storageKey.empty()) {
		error = "storageKey is empty";
		return std::nullopt;
	}

	const auto cfg = loadStorageConfg();
	const auto path = filePathForKey(cfg.root_dir, storageKey);

	std::ifstream input(path, std::ios::binary);
	if (!input.is_open()) {
		error = "failed to open storage file: " + path.string();
		return std::nullopt;
	}

	std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	return bytes;
}

std::optional<std::string> ImageStorage::loadBase64(const std::string& storageKey, std::string& error) const
{
	auto bytes = loadBytes(storageKey, error);
	if (!bytes)
		return std::nullopt;

	return encodeToBase64(*bytes);
}

std::string ImageStorage::contentTypeForKey(const std::string& storageKey) const
{
	if (storageKey.ends_with(".jpg") || storageKey.ends_with(".jpeg"))
		return "image/jpeg";
	if (storageKey.ends_with(".webp"))
		return "image/webp";
	return "image/png";
}
