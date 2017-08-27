
#include "server.h"
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <thread>
#include "cbinding/serializer.h"
#include "http/listener.h"

using namespace reindexer;

Server::Server(shared_ptr<reindexer::Reindexer> db) : db_(db) {}
Server::~Server() {}

bool Server::Start(int port) {
	ev::dynamic_loop loop;
	http::Router router;

	router.GET<Server, &Server::GetVisits>("/visits/", this);
	router.GET<Server, &Server::GetUsers>("/users/", this);
	router.GET<Server, &Server::GetLocations>("/locations/", this);
	router.POST<Server, &Server::PostVisits>("/visits/", this);
	router.POST<Server, &Server::PostUsers>("/users/", this);
	router.POST<Server, &Server::PostLocations>("/locations/", this);
	router.GET<Server, &Server::GetQuery>("/query", this);

	http::Listener listener(loop, router);

	if (!listener.Bind(port)) {
		printf("Can't listen on %d port\n", port);
		return false;
	}

	listener.Run();
	printf("listener::Run exited\n");

	return true;
}

int Server::GetVisits(http::Context &ctx) {
	char *p;
	int id = strtol(ctx.request->pathParams, &p, 10);

	QueryResults res;
	auto q = Query("visits").Where("id", CondEq, id).Select({"id", "location", "user", "visited_at", "mark"});
	auto ret = db_->Select(q, res);
	if (!ret.ok() || res.size() != 1) {
		return ctx.CString(http::StatusNotFound, ret.what().data());
	}

	WrSerializer wrSer(true);
	res.GetJSON(0, wrSer, false);
	return ctx.JSON(http::StatusOK, wrSer.Buf(), wrSer.Len());
}

int Server::GetUsers(http::Context &ctx) {
	char *p = nullptr;
	int id = strtol(ctx.request->pathParams, &p, 10);

	QueryResults res;
	auto ret = db_->Select(Query("users").Where("id", CondEq, id), res);
	if (!ret.ok() || res.size() != 1) {
		return ctx.CString(http::StatusNotFound, ret.what().data());
	}
	if (!strcmp(p, "/visits")) {
		return GetUserVisits(ctx);
	}

	WrSerializer wrSer(true);
	res.GetJSON(0, wrSer, false);
	return ctx.JSON(http::StatusOK, wrSer.Buf(), wrSer.Len());
}

int Server::GetLocations(http::Context &ctx) {
	char *p = nullptr;
	int id = strtol(ctx.request->pathParams, &p, 10);

	QueryResults res;
	auto ret = db_->Select(Query("locations").Where("id", CondEq, id), res);
	if (!ret.ok() || res.size() != 1) {
		return ctx.CString(http::StatusNotFound, ret.what().data());
	}
	if (!strcmp(p, "/avg")) {
		return GetLocationAvg(ctx);
	}

	WrSerializer wrSer(true);
	res.GetJSON(0, wrSer, false);
	return ctx.JSON(http::StatusOK, wrSer.Buf(), wrSer.Len());
}

int Server::GetUserVisits(http::Context &ctx) {
	char *p;
	int userid = strtol(ctx.request->pathParams, &p, 10);

	QueryResults res;
	auto q = Query("visits").Where("user", CondEq, userid).Sort("visited_at", false).Select({"mark", "place", "visited_at"});

	for (auto p : ctx.request->params) {
		if (!strcmp(p.name, "country")) {
			q.Where("location_country", CondEq, p.val);
		} else if (!strcmp(p.name, "toDistance") && *p.val) {
			q.Where("location_distance", CondLt, p.val);
		} else if (!strcmp(p.name, "fromDate") && *p.val) {
			q.Where("visited_at", CondGt, p.val);
		} else if (!strcmp(p.name, "toDate") && *p.val) {
			q.Where("visited_at", CondLt, p.val);
		}
	}

	auto ret = db_->Select(q, res);
	if (!ret.ok()) {
		return ctx.CString(http::StatusBadRequest, ret.what().data());
	}

	WrSerializer wrSer(true);
	wrSer.PutChars("{\"visits\":[");
	for (size_t i = 0; i < res.size(); i++) {
		if (i != 0) {
			wrSer.PutChar(',');
		}
		res.GetJSON(i, wrSer, false);
	}
	wrSer.PutChars("]}");
	return ctx.JSON(http::StatusOK, wrSer.Buf(), wrSer.Len());
}

int years2unix(int age) { return age * 365 * 60 * 60 * 24 + (age / 4 * 60 * 60 * 24); }

int Server::GetLocationAvg(http::Context &ctx) {
	char *pend;
	int locationid = strtol(ctx.request->pathParams, &pend, 10);

	QueryResults res;
	auto q = Query("visits").Where("location", CondEq, locationid).Aggregate("mark", AggAvg);

	for (auto p : ctx.request->params) {
		if (!strcmp(p.name, "fromDate")) {
			q.Where("visited_at", CondGt, p.val);
		} else if (!strcmp(p.name, "toDate") && *p.val) {
			q.Where("visited_at", CondLt, p.val);
		} else if (!strcmp(p.name, "gender") && *p.val) {
			if (strcmp(p.val, "f") && strcmp(p.val, "m")) {
				return ctx.CString(http::StatusBadRequest, "Invalid gender value");
			}
			q.Where("user_gender", CondEq, p.val);
		} else if (!strcmp(p.name, "fromAge") && *p.val) {
			int age = strtol(p.val, &pend, 10);
			if (*pend) {
				ctx.CString(http::StatusBadRequest, "Invalid fromAge value");
			}
			q.Where("user_birth_date", CondLt, fakeNow_ - years2unix(age));
		} else if (!strcmp(p.name, "toAge") && *p.val) {
			int age = strtol(p.val, &pend, 10);
			if (*pend) {
				return ctx.CString(http::StatusBadRequest, "Invalid toAge value");
			}
			q.Where("user_birth_date", CondGt, fakeNow_ - years2unix(age));
		}
	}

	auto ret = db_->Select(q, res);
	if (!ret.ok()) {
		return ctx.CString(http::StatusBadRequest, ret.what().data());
	}

	char tmpBuf[256];
	int l = snprintf(tmpBuf, sizeof(tmpBuf), "{\"avg\":%g}", res.aggregationResults[0]);
	return ctx.JSON(http::StatusOK, tmpBuf, l);
}

int Server::GetQuery(http::Context &ctx) {
	reindexer::QueryResults res;
	const char *sqlQuery = nullptr;

	for (auto p : ctx.request->params) {
		if (!strcmp(p.name, "q")) sqlQuery = p.val;
	}

	if (!sqlQuery) {
		return ctx.CString(http::StatusBadRequest, "Missed `q` parameter");
	}

	auto ret = db_->Select(sqlQuery, res);

	if (!ret.ok()) {
		return ctx.CString(http::StatusInternalServerError, ret.what().data());
	}
	ctx.writer->SetRespCode(http::StatusOK);
	reindexer::WrSerializer wrSer(true);
	ctx.writer->Write("{\"items\":[", 10);
	for (size_t i = 0; i < res.size(); i++) {
		wrSer.Reset();
		if (i != 0) {
			ctx.writer->Write(",", 1);
		}
		res.GetJSON(i, wrSer, false);
		ctx.writer->Write(wrSer.Buf(), wrSer.Len());
	}
	ctx.writer->Write("]}", 2);
	return 0;
}

int Server::PostVisits(http::Context &ctx) {
	int id = -1;
	char *p = nullptr;
	if (strcmp(ctx.request->pathParams, "new")) {
		id = strtol(ctx.request->pathParams, &p, 10);
	}

	ctx.writer->SetConnectionClose();

	unique_ptr<Item> item;
	lock_guard<mutex> lock(wrLock_);

	if (id >= 0) {
		QueryResults res;
		auto ret = db_->Select(Query("visits").Where("id", CondEq, id), res);
		if (!ret.ok() || res.size() != 1) {
			return ctx.CString(http::StatusNotFound, ret.what().data());
		}
		item.reset(db_->GetItem("visits", res[0].id));
		item->Clone();
	} else {
		item.reset(db_->NewItem("visits"));
		item->FromJSON(string("{\"user\": 0, \"location\": 0, \"visited_at\": 0, \"id\": 0, \"mark\": 1, \"place\":\"\"}"));
	}

	JsonAllocator jallocator;
	char *body = (char *)alloca(ctx.body->Pending() + 1);
	if (!parseBodyToObject(ctx, item.get(), jallocator, body)) {
		return ctx.CString(http::StatusBadRequest, "Can't parse json or null field in json");
	}

	if (id >= 0) {
		item->SetField("id", (KeyRef)id);
	}

	mergeVisit(item.get());
	db_->Upsert("visits", item.get());
	lastUpdated_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	return ctx.JSON(http::StatusOK, "{}", 2);
}

int Server::PostUsers(http::Context &ctx) {
	int id = -1;
	char *p = nullptr;
	if (strcmp(ctx.request->pathParams, "new")) {
		id = strtol(ctx.request->pathParams, &p, 10);
	}

	ctx.writer->SetConnectionClose();

	lock_guard<mutex> lock(wrLock_);

	unique_ptr<Item> item;

	if (id >= 0) {
		QueryResults res;
		auto ret = db_->Select(Query("users").Where("id", CondEq, id), res);
		if (!ret.ok() || res.size() != 1) {
			return ctx.CString(http::StatusNotFound, ret.what().data());
		}
		item.reset(db_->GetItem("users", res[0].id));
		item->Clone();
	} else {
		item.reset(db_->NewItem("users"));
		item->FromJSON(
			string("{\"first_name\": \"\", \"last_name\": \"\", \"birth_date\": 0, \"gender\": \"\", \"id\": 0, \"email\": \"\"}"));
	}
	JsonAllocator jallocator;
	char *body = (char *)alloca(ctx.body->Pending() + 1);
	if (!parseBodyToObject(ctx, item.get(), jallocator, body)) {
		return ctx.CString(http::StatusBadRequest, "Can't parse json or null field in json");
	}
	if (id >= 0) {
		item->SetField("id", (KeyRef)id);
	}
	db_->Upsert("users", item.get());
	mergeVisitsByUser(item.get());
	lastUpdated_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	return ctx.JSON(http::StatusOK, "{}", 2);
}

int Server::PostLocations(http::Context &ctx) {
	int id = -1;
	char *p = nullptr;
	if (strcmp(ctx.request->pathParams, "new")) {
		id = strtol(ctx.request->pathParams, &p, 10);
	}

	ctx.writer->SetConnectionClose();

	lock_guard<mutex> lock(wrLock_);
	unique_ptr<Item> item;

	if (id >= 0) {
		QueryResults res;
		auto ret = db_->Select(Query("locations").Where("id", CondEq, id), res);
		if (!ret.ok() || res.size() != 1) {
			return ctx.CString(http::StatusNotFound, ret.what().data());
		}
		item.reset(db_->GetItem("locations", res[0].id));
		item->Clone();
	} else {
		item.reset(db_->NewItem("locations"));
		item->FromJSON(string("{\"distance\":0, \"city\": \"\", \"place\": \"\", \"id\": 0, \"country\": \"\"}"));
	}

	JsonAllocator jallocator;
	char *body = (char *)alloca(ctx.body->Pending() + 1);
	if (!parseBodyToObject(ctx, item.get(), jallocator, body)) {
		return ctx.CString(http::StatusBadRequest, "Can't parse json or null field in json");
	}
	if (id >= 0) {
		item->SetField("id", (KeyRef)id);
	}

	db_->Upsert("locations", item.get());
	mergeVisitsByLocation(item.get());
	lastUpdated_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	return ctx.JSON(http::StatusOK, "{}", 2);
}

void Server::mergeVisit(Item *visit) {
	QueryResults res1, res2;

	auto ret = db_->Select(Query("users").Where("id", CondEq, visit->GetField("user")), res1);
	if (!ret.ok() || res1.size() != 1) {
		abort();
	}
	unique_ptr<Item> user(db_->GetItem("users", res1[0].id));

	ret = db_->Select(Query("locations").Where("id", CondEq, visit->GetField("location")), res2);
	if (!ret.ok() || res2.size() != 1) {
		abort();
	}
	unique_ptr<Item> location(db_->GetItem("locations", res2[0].id));

	visit->SetField("location_distance", location->GetField("distance"));
	visit->SetField("place", location->GetField("place"));
	visit->SetField("location_country", location->GetField("country"));
	visit->SetField("user_gender", user->GetField("gender"));
	visit->SetField("user_birth_date", user->GetField("birth_date"));
}

void Server::mergeVisitsByUser(Item *user) {
	QueryResults res;

	auto ret = db_->Select(Query("visits").Where("user", CondEq, user->GetField("id")), res);
	if (!ret.ok()) {
		abort();
	}

	for (auto r : res) {
		unique_ptr<Item> visit(db_->GetItem("visits", r.id));
		visit->Clone();
		visit->SetField("user_gender", user->GetField("gender"));
		visit->SetField("user_birth_date", user->GetField("birth_date"));
		db_->Upsert("visits", visit.get());
	}
}

void Server::mergeVisitsByLocation(Item *location) {
	QueryResults res;

	auto ret = db_->Select(Query("visits").Where("location", CondEq, location->GetField("id")), res);
	if (!ret.ok()) {
		abort();
	}

	for (auto r : res) {
		unique_ptr<Item> visit(db_->GetItem("visits", r.id));
		visit->Clone();
		visit->SetField("location_distance", location->GetField("distance"));
		visit->SetField("place", location->GetField("place"));
		visit->SetField("location_country", location->GetField("country"));
		db_->Upsert("visits", visit.get());
	}
}

bool Server::LoadData(const string &dataDir) {
	dataDir_ = dataDir;
	bool ret = loadUsers();
	ret = ret && loadLocations();
	ret = ret && loadVisits();
	ret = ret && loadOptions();
	lastUpdated_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	startWarmupRoutine();
	return ret;
}

bool Server::loadUsers() {
	db_->AddNamespace("users");

	IndexOpts oppk{0, 1}, op{0, 0};
	db_->AddIndex("users", "id", "id", IndexIntHash, &oppk);
	db_->AddIndex("users", "gender", "gender", IndexStrStore, &op);
	db_->AddIndex("users", "first_name", "first_name", IndexStrStore, &op);
	db_->AddIndex("users", "last_name", "last_name", IndexStrStore, &op);
	db_->AddIndex("users", "birth_date", "birth_date", IndexIntStore, &op);
	db_->AddIndex("users", "email", "email", IndexStrStore, &op);
	return findFilesAndLoadToDB("users", "users");
}

bool Server::loadLocations() {
	db_->AddNamespace("locations");
	IndexOpts oppk{0, 1}, op{0, 0};
	db_->AddIndex("locations", "id", "id", IndexIntHash, &oppk);
	db_->AddIndex("locations", "place", "place", IndexStrStore, &op);
	db_->AddIndex("locations", "city", "city", IndexStrStore, &op);
	db_->AddIndex("locations", "country", "country", IndexStrStore, &op);
	db_->AddIndex("locations", "distance", "distance", IndexIntStore, &op);
	return findFilesAndLoadToDB("locations", "locations");
}

bool Server::loadVisits() {
	db_->AddNamespace("visits");
	IndexOpts oppk{0, 1}, op{0, 0};
	db_->AddIndex("visits", "id", "id", IndexIntHash, &oppk);
	db_->AddIndex("visits", "user", "user", IndexIntHash, &op);
	db_->AddIndex("visits", "location", "location", IndexIntHash, &op);
	db_->AddIndex("visits", "visited_at", "visited_at", IndexInt);
	db_->AddIndex("visits", "mark", "mark", IndexIntStore);

	db_->AddIndex("visits", "location_distance", "location_distance", IndexIntStore);
	db_->AddIndex("visits", "location_country", "location_country", IndexHash);
	db_->AddIndex("visits", "place", "place", IndexStrStore);

	db_->AddIndex("visits", "user_gender", "user_gender", IndexStrStore);
	db_->AddIndex("visits", "user_birth_date", "user_birth_date", IndexIntStore);
	return findFilesAndLoadToDB("visits", "visits");
}

vector<char> loadFile(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		printf("Can't load %s\n", path);
		abort();
	}
	fseek(f, 0, SEEK_END);
	size_t sz = ftell(f);
	vector<char> data(sz + 1, 0);
	fseek(f, 0, SEEK_SET);
	if (fread(&data[0], 1, sz, f) != sz) {
		abort();
	}

	fclose(f);
	return data;
}

bool Server::loadOptions() {
	fakeNow_ = 1503333691;
	auto v = loadFile((dataDir_ + "/" + "options.txt").c_str());
	if (!v.size()) {
		logPrintf(LogWarning, "options.txt not found");
		return true;
	}
	fakeNow_ = strtol(v.data(), nullptr, 10);
	logPrintf(LogInfo, "now from options.txt is %d", fakeNow_);

	return true;
}

bool Server::findFilesAndLoadToDB(const char *name, const char *ns) {
	size_t len = strlen(name);
	DIR *dirp = opendir(dataDir_.c_str());
	dirent *dp;

	while ((dp = readdir(dirp)) != nullptr) {
		if (strlen(dp->d_name) < len || strncmp(dp->d_name, name, len)) continue;

		auto data = loadFile((dataDir_ + "/" + dp->d_name).c_str());
		logPrintf(LogInfo, "Loadind %s(%d) bytes", dp->d_name, (int)data.size());

		for (char *end = &data.at(1), *beg = strchr(end, '{'); beg; beg = strchr(end, '{')) {
			unique_ptr<reindexer::Item> it(db_->NewItem(ns));
			end = strchr(beg, '}') + 1;
			*end++ = 0;
			char tmpBuf[2048];

			if (!strcmp(ns, "visits")) {
				strcpy(tmpBuf, beg);
				strcpy(tmpBuf + (end - beg) - 2, ",\"place\":\"\"}");
				it->FromJSON(Slice(tmpBuf, strlen(tmpBuf) + 1));
				mergeVisit(it.get());
			} else {
				it->FromJSON(Slice(beg, end - beg));
			}
			auto res = db_->Upsert(ns, it.get());
			if (!res.ok()) {
				abort();
			}
		}
	}
	closedir(dirp);
	return true;
}

bool Server::parseBodyToObject(http::Context &ctx, Item *item, JsonAllocator &jallocator, char *body) {
	char *pend = nullptr;
	ssize_t nread = ctx.body->Read(body, ctx.body->Pending());
	body[nread] = 0;

	if (nread == 0) {
		return false;
	}

	JsonValue jvalue;
	int status = jsonParse(body, &pend, &jvalue, jallocator);

	if (status != JSON_OK) {
		return false;
	}

	for (auto elem : jvalue) {
		switch (elem->value.getTag()) {
			case JSON_NUMBER:
				item->SetField(elem->key, KeyRef((int)elem->value.toNumber()));
				break;
			case JSON_STRING:
				if (strlen(elem->value.toString())) {
					item->SetField(elem->key, KeyRef(p_string(elem->value.toString())));
				}
				break;
			case JSON_NULL:
			default:
				return false;
		}
	}
	return true;
}

void Server::startWarmupRoutine() {
	new std::thread([&]() {
		for (;;) {
			uint64_t t = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			if (lastUpdated_.load() != 0 && t - lastUpdated_ > 3000) {
				lock_guard<mutex> lock(wrLock_);
				QueryResults res;
				logPrintf(LogInfo, "Start warming up");
				db_->Select(Query("visits").Sort("visited_at", false), res);
				logPrintf(LogInfo, "Finish warming up");
				lastUpdated_ = 0;
			}
			usleep(100000);
		}
	});
}
