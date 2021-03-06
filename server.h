#pragma once

#include <atomic>
#include <mutex>
#include "core/reindexer.h"
#include "http/router.h"

class JsonAllocator;
using namespace reindexer_server;
using namespace reindexer;
using std::mutex;

class Server {
public:
	Server(shared_ptr<reindexer::Reindexer> db);
	~Server();

	bool Start(int port);
	bool LoadData(const string &dir);

	int GetVisits(http::Context &ctx);
	int GetUsers(http::Context &ctx);
	int GetLocations(http::Context &ctx);
	int GetUserVisits(http::Context &ctx);
	int GetLocationAvg(http::Context &ctx);

	int PostVisits(http::Context &ctx);
	int PostUsers(http::Context &ctx);
	int PostLocations(http::Context &ctx);

	int GetQuery(http::Context &ctx);

protected:
	bool parseBodyToObject(http::Context &ctx, reindexer::Item *item, JsonAllocator &jallocator, char *body);
	void mergeVisit(Item *visit);
	bool loadUsers();
	bool loadLocations();
	bool loadVisits();
	bool loadOptions();
	bool findFilesAndLoadToDB(const char *name, const char *ns);
	void startWarmupRoutine();
	void updateVisits();

	shared_ptr<reindexer::Reindexer> db_;
	string dataDir_;
	int fakeNow_;
	std::atomic<uint64_t> lastUpdated_, lastPrintStats_;
	vector<int> updatedVisits_, updatedUsers_, updatedLocations_;
	mutex lockVisits_, lockUsers_, lockLocations_;
	http::Router router;
};
