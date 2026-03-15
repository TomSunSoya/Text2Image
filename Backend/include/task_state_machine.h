#pragma once

#include <string>
#include <string_view>

namespace task_state {

	bool isTerminal(std::string_view status);
	bool canCancel(std::string_view status);
	bool canRetry(std::string_view status, int retryCount, int maxRetries);
	bool canDelete(std::string_view status);
	bool canReturnBinary(std::string_view status, std::string_view storageKey);
}
