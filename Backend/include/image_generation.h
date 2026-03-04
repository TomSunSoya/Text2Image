#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace models {

class ImageGeneration {
public:
	int64_t id{ 0 };
	int64_t user_id{ 0 };
	std::string request_id;
	std::string prompt;
	std::string negative_prompt;
	int num_steps{ 8 };
	int height{ 768 };
	int width{ 768 };
	std::optional<int> seed;
	std::string status{ "pending" };
	std::string image_url;
	std::string image_base64;
	std::string error_message;
	double generation_time{ 0 };
	std::chrono::system_clock::time_point created_at;
	std::optional<std::chrono::system_clock::time_point> completed_at;

	nlohmann::json toJson() const;

	static ImageGeneration fromJson(const nlohmann::json& j);
};
}