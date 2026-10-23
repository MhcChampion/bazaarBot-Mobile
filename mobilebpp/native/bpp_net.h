#pragma once
#include <string>

// Queue a read-only current-season QiuBot query. A single background worker
// debounces selections, respects the public rate limit and drops stale replies.
void QueueQiuRuns(const std::string& request);
void QueueQiuDetail(const std::string& id);
std::string QiuNetworkStatus();
