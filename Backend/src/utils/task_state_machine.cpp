#include "task_state_machine.h"

bool task_state::isTerminal(const std::string& status)
{
	return status == "success" ||
		status == "failed" ||
		status == "cancelled" ||
		status == "timeout";
}

bool task_state::canCancel(const std::string& status)
{
	return status == "queued" || status == "pending" || status == "generating";
}

bool task_state::canRetry(const std::string& status, int retryCount, int maxRetries)
{
	if (!(status == "failed" || status == "timeout" || status == "cancelled"))
		return false;

	return retryCount < maxRetries;
}

bool task_state::canDelete(const std::string& status)
{
	return isTerminal(status);
}

bool task_state::canReturnBinary(const std::string& status, const std::string &storageKey)
{
	return status == "success" && !storageKey.empty();
}
