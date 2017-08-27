#include <stdio.h>
#include "core/reindexer.h"
#include "pprof/backtrace.h"
#include "server.h"
#include "time/fast_time.h"
#include "tools/logger.h"

using namespace reindexer;

const string kDataDir = "/go/data/";
const int logLevel = 3;
const int kHttpPort = 80;

int main(int, const char **) {
	auto db = std::make_shared<reindexer::Reindexer>();
	backtrace_init();
	logInstallWriter([](int level, char *buf) {
		if (level <= logLevel) {
			std::tm tm;
			std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			fast_gmtime_r(&t, &tm);
			fprintf(stderr, "%02d:%02d:%02d %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, buf);
		}
	});
	Server server(db);
	server.LoadData(kDataDir);
	server.Start(kHttpPort);
	return 0;
}
