#pragma once

#include <string>

namespace task_state {

	bool isTerminal(const std::string& status);
	bool canCancel(const std::string& status);
	bool canRetry(const std::string& status, int retryCount, int maxRetries);
	bool canDelete(const std::string& status);
	bool canReturnBinary(const std::string& status, const std::string &storageKey);
}