#include "task_state_machine.h"

bool task_state::isTerminal(std::string_view status)
{
	return status == "success" ||
		status == "failed" ||
		status == "cancelled" ||
		status == "timeout";
}

bool task_state::canCancel(std::string_view status)
{
	return status == "queued" || status == "pending" || status == "generating";
}

bool task_state::canRetry(std::string_view status, int retryCount, int maxRetries)
{
	if (!(status == "failed" || status == "timeout" || status == "cancelled"))
		return false;

	return retryCount < maxRetries;
}

bool task_state::canDelete(std::string_view status)
{
	return isTerminal(status);
}

bool task_state::canReturnBinary(std::string_view status, std::string_view storageKey)
{
	return status == "success" && !storageKey.empty();
}
