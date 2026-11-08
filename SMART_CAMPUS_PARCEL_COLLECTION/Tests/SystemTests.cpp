// ============================================================================
// SystemTests.cpp
// Automated tests for the Smart Campus Parcel system (Tasks 1-5 + integration)
//
// Every test runs on a fresh COPY of the Data folder inside the system temp
// directory, so the real CSV files are never changed.
//
// How to run
//   Linux / macOS / Git Bash :  bash Tests/run_tests.sh
//   Windows (Developer Command Prompt for VS, inside the Tests folder):
//                               run_tests.bat
//   Or build by hand with the five module .cpp files and run:
//                               SystemTests <path to Data folder>
//
// This file has its own main(), so it is NOT part of the Visual Studio
// project. The modules are tested only through their public functions and
// menus, exactly as Main.cpp uses them.
// ============================================================================
#include "../Code/Task1/ParcelIntake.hpp"
#include "../Code/Task2/ServiceRequest.hpp"
#include "../Code/Task3/ResourceQueue.hpp"
#include "../Code/Task4/ActivityLog.hpp"
#include "../Code/Task5/ParcelCategoryTree.hpp"
#include "../Code/ActivityHook.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// ============================================================================
//  Tiny test framework
// ============================================================================
namespace {

    const int GROUP_COUNT = 6;
    const char* GROUP_NAMES[GROUP_COUNT] = {
        "Task 1 - Parcel intake, sorting and processing",
        "Task 2 - Service requests",
        "Task 3 - Counters and delivery resources",
        "Task 4 - Activity history and undo",
        "Task 5 - Parcel category BST",
        "Integration - modules working together"
    };
    int g_pass[GROUP_COUNT] = { 0 };
    int g_fail[GROUP_COUNT] = { 0 };
    int g_group = 0;
    std::ostream* g_report = nullptr;    // the real console
    std::string g_dataDir;
    std::string g_testName;

    void check(bool ok, const std::string& what, const std::string& detail, int line) {
        if (ok) { g_pass[g_group]++; return; }
        g_fail[g_group]++;
        *g_report << "  FAIL  " << g_testName << ": " << what << "  (line " << line << ")\n";
        if (!detail.empty()) *g_report << "        -> " << detail << "\n";
    }

    void startGroup(int group) {
        g_group = group;
        *g_report << "\n" << GROUP_NAMES[group] << "\n";
    }

    void startTest(const std::string& name) { g_testName = name; }

    bool contains(const std::string& text, const std::string& part) {
        return text.find(part) != std::string::npos;
    }

    // Everything from the LAST occurrence of 'marker' on ("" if never printed)
    std::string fromLast(const std::string& text, const std::string& marker) {
        size_t at = text.rfind(marker);
        return at == std::string::npos ? std::string() : text.substr(at);
    }

    // Swallows everything the modules print, so the report stays readable
    class NullBuffer : public std::streambuf {
    protected:
        int overflow(int c) override { return c; }
    };

    // Feeds 'input' to std::cin and records everything written to std::cout
    class Console {
    public:
        explicit Console(const std::string& input) : in(input) {
            oldIn = std::cin.rdbuf(in.rdbuf());
            oldOut = std::cout.rdbuf(out.rdbuf());
            std::cin.clear();
        }
        ~Console() {
            std::cin.rdbuf(oldIn);
            std::cout.rdbuf(oldOut);
            std::cin.clear();
        }
        std::string text() const { return out.str(); }
    private:
        std::istringstream in;
        std::ostringstream out;
        std::streambuf* oldIn;
        std::streambuf* oldOut;
    };

    // ---- CSV helpers (the files are the contract between the modules) ------
    const int MAX_FIELDS = 12;
    const int MAX_ROWS = 400;

    struct Row { std::string f[MAX_FIELDS]; int n = 0; };

    int split(const std::string& line, std::string out[], int max) {
        int n = 0;
        std::string cur;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == ',') { if (n < max) out[n] = cur; n++; cur.clear(); }
            else if (line[i] != '\r') cur += line[i];
        }
        if (n < max) out[n] = cur;
        return n + 1;
    }

    // Reads the data rows (header skipped). Returns how many were read.
    int readRows(const std::string& path, Row rows[], int max) {
        std::ifstream in(path.c_str());
        std::string line;
        int n = 0;
        bool header = true;
        while (std::getline(in, line) && n < max) {
            if (header) { header = false; continue; }
            if (line.empty() || line == "\r") continue;
            rows[n].n = split(line, rows[n].f, MAX_FIELDS);
            n++;
        }
        return n;
    }

    Row g_rows[MAX_ROWS];

    int rowCount(const std::string& path) { return readRows(path, g_rows, MAX_ROWS); }

    // Field 'col' of the row whose first field is 'id' ("<none>" if absent)
    std::string field(const std::string& path, const std::string& id, int col) {
        int n = readRows(path, g_rows, MAX_ROWS);
        for (int i = 0; i < n; i++)
            if (g_rows[i].f[0] == id) return g_rows[i].f[col];
        return "<none>";
    }

    bool hasRow(const std::string& path, const std::string& id) {
        return field(path, id, 0) != "<none>";
    }

    // Number of rows whose field 'col' equals 'value'
    int countWhere(const std::string& path, int col, const std::string& value) {
        int n = readRows(path, g_rows, MAX_ROWS), c = 0;
        for (int i = 0; i < n; i++) if (g_rows[i].f[col] == value) c++;
        return c;
    }

    // Rewrites one field, the way another module would change the file
    void setField(const std::string& path, const std::string& id, int col, const std::string& value) {
        std::ifstream in(path.c_str());
        std::string line, all;
        bool header = true;
        while (std::getline(in, line)) {
            if (!header) {
                std::string f[MAX_FIELDS];
                int n = split(line, f, MAX_FIELDS);
                if (f[0] == id) {
                    f[col] = value;
                    line.clear();
                    for (int i = 0; i < n; i++) line += (i ? "," : "") + f[i];
                }
            }
            header = false;
            all += line + "\n";
        }
        in.close();
        std::ofstream out(path.c_str());
        out << all;
    }

    void writeFile(const std::string& path, const std::string& text) {
        std::ofstream out(path.c_str());
        out << text;
    }

    // ---- fixture: a private copy of the Data folder --------------------------
    struct Fixture {
        std::string dir, parcels, requests, resources, activities;
        explicit Fixture(const std::string& name) {
            fs::path d = fs::temp_directory_path() / "smart_campus_tests" / name;
            fs::remove_all(d);
            fs::create_directories(d);
            const char* files[4] = { "parcels.csv", "requests.csv", "resources.csv", "activities.csv" };
            for (int i = 0; i < 4; i++)
                fs::copy_file(fs::path(g_dataDir) / files[i], d / files[i],
                    fs::copy_options::overwrite_existing);
            dir = d.string();
            parcels = (d / "parcels.csv").string();
            requests = (d / "requests.csv").string();
            resources = (d / "resources.csv").string();
            activities = (d / "activities.csv").string();
        }
        ~Fixture() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    };

    // ---- activity hook that just records events (Task 1 / Task 2 tests) ---
    ActivityEvent g_events[64];
    int g_eventCount = 0;
    void recordEvent(const ActivityEvent& e) { if (g_eventCount < 64) g_events[g_eventCount++] = e; }
    void clearEvents() { g_eventCount = 0; }
    const ActivityEvent* lastEvent() { return g_eventCount > 0 ? &g_events[g_eventCount - 1] : nullptr; }

    // ---- the same wiring Main.cpp does --------------------------------------
    ServiceModule* g_service = nullptr;
    ActivityModule* g_activity = nullptr;

    void mainOnRequestLost(const std::string& requestID) {         // copy of Main.cpp
        ServiceRequest r;
        if (g_service == nullptr || !g_service->findRequest(requestID, r)) return;
        if (r.status != STATUS_ASSIGNED) return;
        std::string msg;
        g_service->restoreRequestStatus(requestID, STATUS_PENDING, msg);
    }

    bool mainBeforeRelease(const std::string& requestID) {          // copy of Main.cpp
        ServiceRequest r;
        if (g_service == nullptr || !g_service->findRequest(requestID, r)) return true;
        if (r.status != STATUS_ASSIGNED) return true;
        std::string collector, msg;
        if (r.serviceType == SERVICE_COLLECTION) std::getline(std::cin, collector);
        return g_service->completeRequest(requestID, collector, msg);
    }

    void mainActivityBridge(const ActivityEvent& e) {               // copy of Main.cpp
        if (g_activity == nullptr) return;
        g_activity->logActivity(e.parcelID, e.activityType, e.oldStatus, e.newStatus,
            e.requestID, e.resourceID);
    }

    // All five modules on one fixture, connected like Main.cpp connects them
    struct System {
        Fixture fx;
        ParcelModule parcels;
        ServiceModule service;
        ResourceManager resources;
        ActivityModule activities;
        ParcelCategoryModule categories;

        explicit System(const std::string& name)
            : fx(name), parcels(fx.parcels, fx.requests), service(fx.requests, fx.parcels),
            activities(fx.activities), categories(fx.parcels, fx.requests) {
            parcels.load();
            service.load();
            resources.loadFromCSV(fx.resources);
            categories.load();
            categories.setParcelModule(&parcels);
            g_service = &service;
            resources.setRequestLostHook(mainOnRequestLost);
            resources.setBeforeReleaseHook(mainBeforeRelease);
            activities.load();
            activities.connect(&parcels, &service, &resources, fx.resources);
            g_activity = &activities;
            parcels.setActivityHook(mainActivityBridge);
            service.setActivityHook(mainActivityBridge);
        }
        ~System() { g_service = nullptr; g_activity = nullptr; }
    };

    std::string parcelStatus(ParcelModule& pm, const std::string& id) {
        Parcel p;
        return pm.findParcel(id, p) ? p.status : "<none>";
    }

    std::string requestStatus(ServiceModule& sm, const std::string& id) {
        ServiceRequest r;
        return sm.findRequest(id, r) ? r.status : "<none>";
    }

    Parcel makeInput(const std::string& name, const std::string& id, const std::string& delivery,
        const std::string& type, const std::string& courier, const std::string& arrival) {
        Parcel p;
        p.recipientName = name; p.recipientID = id; p.deliveryInfo = delivery;
        p.parcelType = type; p.courier = courier; p.arrivalDateTime = arrival;
        return p;
    }

    Resource makeResource(const std::string& id, const std::string& type, ResourceStatus status,
        const std::string& assignment = "NONE") {
        Resource r;
        r.resourceID = id; r.type = type; r.name = "Test " + id;
        r.status = status; r.currentAssignment = assignment;
        return r;
    }

    // Which resource (if any) holds a request
    std::string holderOf(ResourceManager& rm, const std::string& requestID) {
        for (int i = 0; i < rm.resourceCount(); i++) {
            Resource* r = rm.resourceAt(i);
            if (r != nullptr && r->currentAssignment == requestID) return r->resourceID;
        }
        return "";
    }

} // namespace

#define CHECK(cond, what) check((cond), (what), "", __LINE__)
#define CHECK_MSG(cond, what, detail) check((cond), (what), (detail), __LINE__)

// ============================================================================
//  TASK 1 - Parcel Intake, Sorting and Processing
// ============================================================================
static void task1_intakeQueue() {
    startTest("IntakeQueue (linked-list FIFO)");
    IntakeQueue q;
    std::string id;
    CHECK(q.isEmpty() && !q.peek(id) && !q.dequeue(id), "empty queue: peek/dequeue fail");
    q.enqueue("P001"); q.enqueue("P002"); q.enqueue("P003");
    CHECK(q.size() == 3, "size after 3 enqueues");
    CHECK(q.peek(id) && id == "P001", "front is the first one in (FIFO)");
    CHECK(q.position("P002") == 2 && q.position("P999") == 0, "position()");

    CHECK(q.remove("P003"), "remove the rear");
    q.enqueue("P004");                                    // rear pointer must be right
    std::string arr[5];
    int n = q.toArray(arr, 5);
    CHECK(n == 3 && arr[0] == "P001" && arr[1] == "P002" && arr[2] == "P004",
        "enqueue after removing the rear keeps the order");

    CHECK(q.remove("P001") && q.peek(id) && id == "P002", "remove the front");
    CHECK(!q.remove("P999"), "remove a parcel that is not queued");
    CHECK(q.dequeue(id) && id == "P002" && q.dequeue(id) && id == "P004", "dequeue in FIFO order");
    CHECK(q.isEmpty() && !q.dequeue(id), "empty again");
    q.enqueue("P005");
    CHECK(q.peek(id) && id == "P005" && q.size() == 1, "queue reusable after it was emptied");
}

static void task1_parcelStore() {
    startTest("ParcelStore (sorted array + binary search)");
    ParcelStore s;
    Parcel p;
    const char* ids[3] = { "P003", "P001", "P002" };
    for (int i = 0; i < 3; i++) { p.parcelID = ids[i]; s.insert(p); }
    CHECK(s.size() == 3 && s.at(0).parcelID == "P001" && s.at(1).parcelID == "P002" &&
        s.at(2).parcelID == "P003", "out-of-order inserts end up in ID order");
    p.parcelID = "P002";
    CHECK(s.insert(p) == -1 && s.size() == 3, "duplicate Parcel ID rejected");
    CHECK(s.find("P002") == 1 && s.find("P004") == -1, "binary search finds / misses");
    CHECK(s.nextID() == "P004", "nextID is highest + 1");

    p.parcelID = "P1000"; s.insert(p);
    p.parcelID = "P999";  s.insert(p);
    CHECK(s.at(3).parcelID == "P999" && s.at(4).parcelID == "P1000", "P999 sorts before P1000");
    CHECK(s.find("P1000") == 4 && s.nextID() == "P1001", "find / nextID past 999");
    s.removeAt(0);
    CHECK(s.find("P001") == -1 && s.find("P002") == 0 && s.size() == 4, "removeAt shifts left");
}

static void task1_loadAndRegister() {
    startTest("load + register");
    Fixture fx("t1_register");
    ParcelModule pm(fx.parcels, fx.requests);
    clearEvents();
    pm.setActivityHook(recordEvent);
    CHECK(pm.load(), "loads parcels.csv");
    CHECK(pm.waitingCount() == countWhere(fx.parcels, 7, "Registered"), "intake queue = Registered parcels");
    Parcel front;
    CHECK(pm.peekNextParcel(front) && front.parcelID == "P021", "front of the queue is P021");

    std::string id, msg;
    int before = pm.waitingCount();
    bool ok = pm.registerParcel(makeInput("Test Person", "tp999001", "counter", "standard", "DHL", ""), id, msg);
    CHECK_MSG(ok && id == "P041", "valid parcel registered as P041", msg);
    CHECK(pm.waitingCount() == before + 1, "new parcel joins the intake queue");
    CHECK(field(fx.parcels, "P041", 7) == "Registered" && field(fx.parcels, "P041", 2) == "TP999001" &&
        field(fx.parcels, "P041", 3) == "Counter" && field(fx.parcels, "P041", 4) == "Standard",
        "saved to parcels.csv with cleaned-up fields");
    CHECK(lastEvent() && lastEvent()->activityType == ACT_REGISTERED && lastEvent()->parcelID == "P041",
        "Registered activity published for Task 4");

    struct Bad { Parcel in; const char* what; };
    Bad bad[] = {
        { makeInput("", "TP999002", "Counter", "Standard", "DHL", ""), "missing recipient name" },
        { makeInput("J0hn", "TP999002", "Counter", "Standard", "DHL", ""), "name with a digit" },
        { makeInput("Jane Doe", "TP12", "Counter", "Standard", "DHL", ""), "badly formatted recipient ID" },
        { makeInput("Someone Else", "TP012345", "Counter", "Standard", "DHL", ""), "recipient ID that belongs to another person" },
        { makeInput("Jane Doe", "TP999002", "Counter", "Huge", "DHL", ""), "unknown parcel type" },
        { makeInput("Jane Doe", "TP999002", "Block A, L3", "Standard", "DHL", ""), "comma in delivery info" },
        { makeInput("Jane Doe", "TP999002", "Counter", "Standard", "DHL", "2099-01-01 10:00"), "arrival in the future" },
        { makeInput("Jane Doe", "TP999002", "Counter", "Standard", "DHL", "2026-02-30 10:00"), "30 February" },
        { makeInput("Jane Doe", "TP999002", "Counter", "Standard", "", ""), "missing courier" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        bool accepted = pm.registerParcel(bad[i].in, id, msg);
        CHECK_MSG(!accepted, std::string("rejects ") + bad[i].what, msg);
    }

    Parcel dup = makeInput("Dup Person", "TP999003", "Block A L1", "Fragile", "J&T", "2026-11-01 10:00");
    CHECK(pm.registerParcel(dup, id, msg), "first registration of a parcel");
    CHECK_MSG(!pm.registerParcel(dup, id, msg) && contains(msg, "Duplicate"), "same parcel scanned twice is refused", msg);
    CHECK(rowCount(fx.parcels) == 42, "rejected registrations did not change the file");
}

static void task1_sorting() {
    startTest("sorting the intake queue");
    Fixture fx("t1_sort");
    ParcelModule pm(fx.parcels, fx.requests);
    clearEvents();
    pm.setActivityHook(recordEvent);
    pm.load();

    struct Expect { const char* id; ParcelRoute route; const char* why; };
    Expect ex[] = {
        { "P021", ROUTE_SPECIAL_HANDLING, "Fragile parcel -> special handling" },
        { "P022", ROUTE_COLLECTION, "existing Collection request R011 decides" },
        { "P032", ROUTE_COLLECTION, "existing Collection request R018 beats its campus address" },
        { "P023", ROUTE_DELIVERY, "existing CampusDelivery request R009" },
        { "P027", ROUTE_DELIVERY, "campus address -> delivery" },
        { "P034", ROUTE_COLLECTION, "Counter -> collection" },
    };
    for (size_t i = 0; i < sizeof(ex) / sizeof(ex[0]); i++) {
        Parcel p;
        std::string reason;
        pm.findParcel(ex[i].id, p);
        CHECK_MSG(pm.suggestRoute(p, reason) == ex[i].route, std::string("suggested route: ") + ex[i].why, reason);
    }

    // A parcel with an active Return request should be routed to ReturnToSender
    // (Task 2 accepts ReturnToSender for Return requests since 7 Nov).
    ServiceModule sm(fx.requests, fx.parcels);
    sm.load();
    std::string msg;
    sm.createRequest("P027", "TP012347", SERVICE_RETURN, 1, msg);
    Parcel p27;
    std::string reason;
    pm.findParcel("P027", p27);
    CHECK_MSG(pm.suggestRoute(p27, reason) == ROUTE_RETURN, "active Return request -> Return to sender", reason);

    Parcel sorted;
    CHECK_MSG(!pm.processNextParcel("P022", ROUTE_COLLECTION, sorted, msg), "refuses when the front is not the expected parcel", msg);
    CHECK(!pm.processNextParcel("", (ParcelRoute)9, sorted, msg), "refuses an invalid route");
    int before = pm.waitingCount();
    CHECK_MSG(pm.processNextParcel("P021", ROUTE_SPECIAL_HANDLING, sorted, msg) && sorted.status == "SpecialHandling",
        "front parcel sorted to SpecialHandling", msg);
    CHECK(pm.waitingCount() == before - 1 && field(fx.parcels, "P021", 7) == "SpecialHandling", "dequeued and saved");
    Parcel front;
    CHECK(pm.peekNextParcel(front) && front.parcelID == "P022", "next in line is P022");
    CHECK(lastEvent() && lastEvent()->activityType == ACT_SORTED && lastEvent()->oldStatus == "Registered",
        "Sorted activity published");

    int sortedCount = 0;
    while (pm.processNextParcel("", ROUTE_HOLD, sorted, msg) && sortedCount < 100) sortedCount++;
    CHECK(sortedCount == before - 1, "every waiting parcel can be processed in turn");
    CHECK_MSG(!pm.processNextParcel("", ROUTE_HOLD, sorted, msg) && contains(msg, "empty"),
        "processing an empty queue is refused", msg);
    CHECK(!pm.peekNextParcel(front) && pm.waitingCount() == 0, "queue is empty");
}

static void task1_statusRules() {
    startTest("status updates, missing, remove");
    Fixture fx("t1_status");
    ParcelModule pm(fx.parcels, fx.requests);
    pm.load();
    std::string msg;

    CHECK_MSG(!pm.updateStatus("P001", "OnHold", msg) && contains(msg, "completed"), "completed parcel cannot be processed again", msg);
    CHECK_MSG(!pm.updateStatus("P027", "ReadyForCollection", msg), "Registered parcel must be sorted first", msg);
    CHECK_MSG(pm.updateStatus("P010", "OnHold", msg), "Missing parcel found -> OnHold", msg);
    CHECK_MSG(!pm.updateStatus("P025", "Collected", msg) && contains(msg, "R001"), "cannot collect while request R001 is active", msg);
    CHECK_MSG(!pm.updateStatus("P017", "Collected", msg), "OnHold -> Collected is not a valid change", msg);
    CHECK(!pm.updateStatus("P017", "Flying", msg), "unknown status refused");
    CHECK(!pm.updateStatus("P999", "OnHold", msg), "non-existent parcel refused");
    CHECK(pm.updateStatus("P017", "ReturnToSender", msg) && pm.updateStatus("P017", "Returned", msg) &&
        field(fx.parcels, "P017", 7) == "Returned", "OnHold -> ReturnToSender -> Returned");

    int before = pm.waitingCount();
    CHECK_MSG(pm.reportMissing("P038", msg), "Registered parcel reported missing", msg);
    CHECK(pm.waitingCount() == before - 1, "missing parcel leaves the intake queue");
    CHECK(!pm.reportMissing("P038", msg), "already missing");

    CHECK_MSG(!pm.removeParcel("P001", msg), "completed record is kept as history", msg);
    CHECK_MSG(!pm.removeParcel("P023", msg) && contains(msg, "R009"), "cannot remove a parcel with an active request", msg);
    CHECK_MSG(pm.removeParcel("P007", msg) && !hasRow(fx.parcels, "P007"), "Cancelled parcel removed", msg);
    CHECK(!pm.removeParcel("P007", msg), "removing it again fails");

    CHECK(pm.updateDeliveryInfo("P027", "Block Z L9", msg) && field(fx.parcels, "P027", 3) == "Block Z L9", "delivery info updated");
    CHECK(!pm.updateDeliveryInfo("P001", "Counter", msg), "delivery info of a completed parcel is locked");
}

static void task1_syncAndRestore() {
    startTest("re-sync with other modules + restore");
    Fixture fx("t1_sync");
    ParcelModule pm(fx.parcels, fx.requests);
    pm.load();
    int before = pm.waitingCount();

    setField(fx.parcels, "P021", 7, "OnHold");                 // another module changes the file
    Parcel front;
    CHECK(pm.peekNextParcel(front) && front.parcelID == "P022", "outside change picked up: P021 left the queue");
    CHECK(pm.waitingCount() == before - 1, "queue size follows the file");
    std::string msg;
    pm.updateStatus("P010", "OnHold", msg);                    // Task 1 saves the whole file
    CHECK(field(fx.parcels, "P021", 7) == "OnHold", "Task 1's save keeps the other module's change");

    Parcel sorted;
    pm.processNextParcel("P022", ROUTE_COLLECTION, sorted, msg);
    CHECK_MSG(pm.restoreParcelStatus("P022", "Registered", msg), "restore (used by undo) back to Registered", msg);
    CHECK(pm.waitingCount() == before - 1, "restored parcel is queued again");
}

// ============================================================================
//  TASK 2 - Service requests
// ============================================================================
static void task2_priorityQueue() {
    startTest("RequestQueue priority rule");
    RequestStore store;
    RequestQueue q(&store);
    struct In { const char* id; int urgency; const char* time; };
    In in[] = {
        { "R101", 1, "2026-10-13 09:00" }, { "R102", 3, "2026-10-13 10:00" },
        { "R103", 3, "2026-10-13 09:30" }, { "R104", 2, "2026-10-13 08:00" },
        { "R105", 3, "2026-10-13 09:30" },
    };
    int idx[5];
    for (int i = 0; i < 5; i++) {
        ServiceRequest r;
        r.requestID = in[i].id; r.urgency = in[i].urgency; r.requestTime = in[i].time; r.status = STATUS_PENDING;
        idx[i] = store.add(r);
        q.enqueue(idx[i]);
    }
    int sortedOrder[5];
    q.snapshotSorted(sortedOrder);
    CHECK(q.size() == 5, "snapshot does not change the queue");
    CHECK(store.at(q.peek()).requestID == "R103", "peek: highest urgency, earliest time");

    CHECK(q.remove(idx[1]) && q.size() == 4, "remove a request from the middle (cancel)");
    const char* expected[4] = { "R103", "R105", "R104", "R101" };
    bool order = true;
    for (int i = 0; i < 4; i++) {
        int top = q.dequeue();
        if (top < 0 || store.at(top).requestID != expected[i]) order = false;
    }
    CHECK(order, "dequeue order: urgency, then time, then smaller Request ID");
    CHECK(q.isEmpty() && q.dequeue() == -1 && q.peek() == -1, "empty queue returns -1");
}

static void task2_createAndValidate() {
    startTest("createRequest validation");
    Fixture fx("t2_create");
    ServiceModule sm(fx.requests, fx.parcels);
    clearEvents();
    sm.setActivityHook(recordEvent);
    CHECK(sm.load(), "loads requests.csv");
    ServiceRequest next;
    CHECK(sm.peekNext(next) && next.requestID == "R011", "next request is R011 (urgency 3, earliest)");

    std::string msg;
    CHECK_MSG(!sm.createRequest("P999", "TP000000", SERVICE_COLLECTION, 1, msg), "unknown parcel refused", msg);
    CHECK_MSG(!sm.createRequest("P027", "TP000000", SERVICE_CAMPUS_DELIVERY, 1, msg), "wrong recipient refused", msg);
    CHECK_MSG(!sm.createRequest("P027", "TP012347", "Teleport", 1, msg), "unknown service refused", msg);
    CHECK_MSG(!sm.createRequest("P027", "TP012347", SERVICE_CAMPUS_DELIVERY, 5, msg), "urgency 5 refused", msg);
    CHECK_MSG(!sm.createRequest("P022", "TP012362", SERVICE_COLLECTION, 1, msg), "second active request for P022 refused", msg);
    CHECK_MSG(!sm.createRequest("P001", "TP012345", SERVICE_COLLECTION, 1, msg), "request for a collected parcel refused", msg);
    CHECK_MSG(sm.createRequest("P027", "tp012347", SERVICE_CAMPUS_DELIVERY, 2, msg), "valid request created", msg);
    CHECK(requestStatus(sm, "R020") == "Pending" && field(fx.requests, "R020", 5) == "Pending", "R020 Pending and saved");
    CHECK(lastEvent() && lastEvent()->activityType == ACT_REQUEST_CREATED && lastEvent()->requestID == "R020",
        "RequestCreated activity published");

    setField(fx.parcels, "P034", 7, "ReturnToSender");
    CHECK_MSG(sm.createRequest("P034", "TP012369", SERVICE_RETURN, 1, msg), "Return request for a ReturnToSender parcel", msg);

    CHECK_MSG(!sm.validateForAssignment("R012", msg), "cancelled request cannot be assigned", msg);
    CHECK_MSG(sm.validateForAssignment("R002", msg), "R002 can be assigned", msg);
    setField(fx.parcels, "P029", 7, "Missing");
    CHECK_MSG(!sm.validateForAssignment("R002", msg), "not when its parcel went missing", msg);

    ServiceModule reloaded(fx.requests, fx.parcels);
    reloaded.load();
    CHECK(requestStatus(reloaded, "R020") == "Pending", "requests survive a reload");
}

static void task2_assignCompleteCancel() {
    startTest("assign, complete, cancel");
    Fixture fx("t2_flow");
    ServiceModule sm(fx.requests, fx.parcels);
    clearEvents();
    sm.setActivityHook(recordEvent);
    sm.load();
    std::string msg;

    CHECK_MSG(sm.markAssigned("R009", "D04", msg), "R009 Pending -> Assigned", msg);
    CHECK(requestStatus(sm, "R009") == "Assigned" && field(fx.parcels, "P023", 7) == "AssignedForDelivery",
        "request Assigned, parcel AssignedForDelivery");
    CHECK(lastEvent() && lastEvent()->activityType == ACT_ASSIGNED && lastEvent()->resourceID == "D04",
        "Assigned activity carries the resource");
    CHECK(!sm.markAssigned("R009", "D04", msg), "cannot assign twice");

    CHECK_MSG(!sm.completeRequest("R001", "TP012346", msg), "Pending request cannot be completed", msg);
    CHECK_MSG(!sm.completeRequest("R003", "TP000000", msg), "wrong collector refused", msg);
    CHECK(field(fx.parcels, "P024", 7) == "ReadyForCollection", "parcel not released to the wrong person");
    CHECK_MSG(sm.completeRequest("R003", "staff024", msg), "right collector completes R003", msg);
    CHECK(requestStatus(sm, "R003") == "Completed" && field(fx.parcels, "P024", 7) == "Collected", "R003 Completed, P024 Collected");
    CHECK(!sm.completeRequest("R003", "STAFF024", msg), "completed request cannot be processed again");

    CHECK(!sm.cancelRequest("R013", msg), "completed request cannot be cancelled");
    CHECK_MSG(sm.cancelRequest("R004", msg), "assigned delivery cancelled", msg);
    CHECK(requestStatus(sm, "R004") == "Cancelled" && field(fx.parcels, "P028", 7) == "OnHold", "its parcel goes OnHold");
    CHECK(!sm.cancelRequest("R004", msg), "already cancelled");

    CHECK(sm.cancelRequest("R011", msg), "pending R011 cancelled");
    ServiceRequest next;
    CHECK(sm.peekNext(next) && next.requestID == "R019", "cancelled request leaves the queue (R019 is next)");
    CHECK(sm.restoreRequestStatus("R011", STATUS_PENDING, msg) && sm.peekNext(next) && next.requestID == "R011",
        "restore puts it back in the queue");
}

// ============================================================================
//  TASK 3 - Counters and delivery resources
// ============================================================================
std::string g_lostRequest;
static void recordLost(const std::string& id) { g_lostRequest = id; }
static bool refuseRelease(const std::string&) { return false; }

static void task3_circularQueue() {
    startTest("ResourceQueue (array circular queue)");
    ResourceQueue q;
    Resource r, out;
    CHECK(q.isEmpty() && !q.dequeue(out), "empty queue");
    for (int i = 1; i <= MAX_RESOURCES; i++) {
        r = makeResource("C" + std::to_string(i), "Counter", AVAILABLE);
        q.enqueue(r);
    }
    CHECK(q.isFull() && q.size() == MAX_RESOURCES, "full at MAX_RESOURCES");
    CHECK(!q.enqueue(makeResource("C99", "Counter", AVAILABLE)), "enqueue on a full queue fails");
    for (int i = 0; i < 3; i++) q.dequeue(out);
    CHECK(out.resourceID == "C3", "dequeue takes from the front");
    for (int i = 11; i <= 13; i++) q.enqueue(makeResource("C" + std::to_string(i), "Counter", OCCUPIED, "R1"));
    CHECK(q.isFull() && q.at(0)->resourceID == "C4" && q.at(9)->resourceID == "C13" && q.at(10) == nullptr,
        "wrap-around keeps front-to-rear order");
    CHECK(q.findByID("C12") != nullptr && q.findByID("C1") == nullptr, "findByID");
    CHECK(q.countByStatus(OCCUPIED) == 3 && q.countByStatus(AVAILABLE) == 7, "countByStatus");
}

static void task3_register() {
    startTest("registerResource validation");
    Fixture fx("t3_register");
    ResourceManager rm;
    CHECK(rm.loadFromCSV(fx.resources) && rm.resourceCount() == 9, "loads 9 resources");
    Resource* c02 = rm.findResource("c02");
    CHECK(c02 != nullptr && c02->status == OCCUPIED && c02->currentAssignment == "R003", "C02 holds R003");

    CHECK(!rm.registerResource(makeResource("X01", "Counter", AVAILABLE), false), "counter ID must start with C");
    CHECK(!rm.registerResource(makeResource("C01", "Counter", AVAILABLE), false), "duplicate ID refused");
    CHECK(!rm.registerResource(makeResource("C07", "Counter", OCCUPIED), false), "Occupied needs a request");
    CHECK(!rm.registerResource(makeResource("C07", "Counter", AVAILABLE, "R099"), false), "Available cannot hold a request");
    CHECK(!rm.registerResource(makeResource("C07", "Counter", OCCUPIED, "R003"), false), "R003 cannot be on two resources");
    CHECK(!rm.registerResource(makeResource("C07", "Robot", AVAILABLE), false), "unknown type refused");
    bool added = true;
    for (int i = 5; i <= 10; i++)
        added = rm.registerResource(makeResource("C" + std::string(i < 10 ? "0" : "") + std::to_string(i), "Counter", AVAILABLE), false) && added;
    CHECK(added, "counters C05-C10 registered");
    CHECK(!rm.registerResource(makeResource("C11", "Counter", AVAILABLE), false), "11th counter refused (queue full)");
}

static void task3_assign() {
    startTest("assignRequest / assignToResource");
    Fixture fx("t3_assign");
    ResourceManager rm;
    rm.loadFromCSV(fx.resources);
    std::string res;

    CHECK(rm.assignRequest("R001", "Collection", &res) && res == "C01", "R001 -> C01 (first free counter)");
    CHECK(rm.assignRequest("R002", "Collection", &res) && res == "C04", "R002 -> C04 (skips occupied / unavailable)");
    CHECK(!rm.assignRequest("R011", "Collection", &res), "no counter left -> refused");
    CHECK(!rm.assignRequest("R001", "Collection", &res), "same request cannot take two resources");
    CHECK(!rm.assignRequest("R019", "Teleport", &res), "unknown service refused");
    CHECK(rm.assignRequest("R009", "CampusDelivery", &res) && res == "D04", "delivery goes to a delivery person");

    rm.setBeforeReleaseHook(nullptr);
    rm.releaseResource("C01");
    rm.releaseResource("C04");
    CHECK(rm.assignRequest("R011", "Collection", &res) && res == "C01", "after release: C01");
    rm.releaseResource("C01");
    CHECK(rm.assignRequest("R019", "Collection", &res) && res == "C04", "round-robin: next job goes to C04, not C01 again");

    Fixture fx2("t3_assign2");
    ResourceManager rm2;
    rm2.loadFromCSV(fx2.resources);
    CHECK(!rm2.assignToResource("R011", "Collection", "D04", &res), "counter job on a delivery person refused");
    CHECK(!rm2.assignToResource("R011", "Collection", "C03", &res), "unavailable resource refused");
    CHECK(!rm2.assignToResource("R011", "Collection", "C02", &res), "occupied resource refused");
    CHECK(!rm2.assignToResource("R011", "Collection", "Z09", &res), "unknown resource refused");
    CHECK(rm2.assignToResource("R011", "Collection", " c01 ", &res) && res == "C01", "chosen counter accepted");
}

static void task3_releaseAndStatus() {
    startTest("release / status / save");
    Fixture fx("t3_status");
    ResourceManager rm;
    rm.loadFromCSV(fx.resources);
    std::string released;

    CHECK(!rm.releaseResource("C04"), "cannot release a free counter");
    rm.setBeforeReleaseHook(refuseRelease);
    CHECK(!rm.releaseResource("C02") && rm.findResource("C02")->status == OCCUPIED, "hook can block a release");
    rm.setBeforeReleaseHook(nullptr);
    CHECK(rm.releaseResource("C02", &released) && released == "R003", "release returns the finished request");
    CHECK(rm.findResource("C02")->status == AVAILABLE && rm.findResource("C02")->currentAssignment == "NONE",
        "released counter is Available with no assignment");

    g_lostRequest.clear();
    rm.setRequestLostHook(recordLost);
    CHECK(rm.updateResourceStatus("D01", UNAVAILABLE) && g_lostRequest == "R004",
        "busy resource going unavailable reports the lost request");
    CHECK(rm.findResource("D01")->currentAssignment == "NONE", "assignment cleared");
    CHECK(!rm.updateResourceStatus("D04", OCCUPIED), "Occupied cannot be set by hand");
    CHECK(!rm.updateResourceStatus("D04", AVAILABLE), "already Available");
    CHECK(!rm.updateResourceStatus("Z09", AVAILABLE), "unknown resource");

    CHECK(rm.saveToCSV(fx.resources), "saved");
    ResourceManager again;
    again.loadFromCSV(fx.resources);
    CHECK(again.resourceCount() == 9 && again.findResource("C02")->status == AVAILABLE &&
        again.findResource("D01")->status == UNAVAILABLE, "changes survive save + load");
}

static void task3_menuWithTask2() {
    startTest("Task 3 menu option 3 with Task 2");
    {
        Fixture fx("t3_menu1");
        ServiceModule sm(fx.requests, fx.parcels);
        sm.load();
        ResourceManager rm;
        rm.loadFromCSV(fx.resources);
        { Console c("3\nR013\nReturn\n0\n"); runTask3Menu(rm, &sm); }
        CHECK_MSG(holderOf(rm, "R013").empty(),
            "a Completed request (R013) must not be given a counter",
            "menu option 3 occupied " + holderOf(rm, "R013") + " with R013, then Task 2 refused it; the counter stays Occupied");
    }
    {
        Fixture fx("t3_menu2");
        ServiceModule sm(fx.requests, fx.parcels);
        sm.load();
        ResourceManager rm;
        rm.loadFromCSV(fx.resources);
        { Console c("3\nR001\nCampusDelivery\n0\n"); runTask3Menu(rm, &sm); }
        std::string holder = holderOf(rm, "R001");
        CHECK_MSG(holder.empty() || holder[0] == 'C',
            "R001 is a Collection request, so it must not go to a delivery person",
            "typing the wrong service type put R001 on " + holder + " and Task 2 still marked it Assigned");
    }
    {
        Fixture fx("t3_menu3");
        ServiceModule sm(fx.requests, fx.parcels);
        sm.load();
        ResourceManager rm;
        rm.loadFromCSV(fx.resources);
        ServiceRequest r;
        { Console c("3\nr001\ncollection\n0\n"); runTask3Menu(rm, &sm); }
        std::string holder = holderOf(rm, "R001");
        CHECK(!holder.empty() && holder[0] == 'C' && sm.findRequest("R001", r) && r.status == STATUS_ASSIGNED,
            "option 3 with the right service type still assigns a counter and Task 2 marks it Assigned");
        { Console c("7\nR009\nCollection\nC04\n0\n"); runTask3Menu(rm, &sm); }
        CHECK(holderOf(rm, "R009").empty() && sm.findRequest("R009", r) && r.status == STATUS_PENDING,
            "option 7 refuses a service type that is not the request's own");
        { Console c("7\nR009\nCampusDelivery\nD04\n0\n"); runTask3Menu(rm, &sm); }
        CHECK(holderOf(rm, "R009") == "D04" && sm.findRequest("R009", r) && r.status == STATUS_ASSIGNED,
            "option 7 with the right type assigns the chosen delivery person");
    }
    {
        Fixture fx("t3_menu4");
        ResourceManager rm;
        rm.loadFromCSV(fx.resources);
        { Console c("3\nR001\nCampusDelivery\n0\n"); runTask3Menu(rm, nullptr); }
        CHECK(holderOf(rm, "R001") == "D04", "without Task 2 connected, the menu still assigns by the typed type");
    }
}

// ============================================================================
//  TASK 4 - Activity history and undo
// ============================================================================
static bool isP002(const Activity& a) { return a.parcelID == "P002"; }

static void task4_stack() {
    startTest("ActivityStack (linked-list stack)");
    ActivityStack s;
    Activity a, out;
    CHECK(s.isEmpty() && s.peek() == nullptr && !s.pop(out), "empty stack");
    const char* ids[3] = { "A001", "A002", "A003" };
    const char* parcels[3] = { "P001", "P002", "P003" };
    for (int i = 0; i < 3; i++) { a.activityID = ids[i]; a.parcelID = parcels[i]; s.push(a); }
    CHECK(s.size() == 3 && s.peek()->activityID == "A003", "top is the newest (LIFO)");
    const Activity* arr[3];
    CHECK(s.toArray(arr, 3) == 3 && arr[0]->activityID == "A003" && arr[2]->activityID == "A001", "toArray newest first");
    int pos = -1;
    Activity* hit = s.findFirst(isP002, &pos);
    CHECK(hit != nullptr && hit->activityID == "A002" && pos == 1, "findFirst walks from the top");
    CHECK(s.pop(out) && out.activityID == "A003" && s.peek()->activityID == "A002", "pop removes the top");
    s.clear();
    CHECK(s.isEmpty(), "clear");
}

static void task4_logAndHistory() {
    startTest("logActivity + history");
    Fixture fx("t4_log");
    ActivityModule am(fx.activities);
    int rows = rowCount(fx.activities);
    CHECK(am.load() && am.count() == rows, "loads every row of activities.csv");
    CHECK(am.latest() != nullptr && am.latest()->activityID == "A0" + std::to_string(rows), "newest activity on top");

    CHECK(am.logActivity("P001", ACT_SORTED, "Registered", "ReadyForCollection"), "valid activity recorded");
    CHECK(am.count() == rows + 1 && am.latest()->activityID == "A0" + std::to_string(rows + 1), "next Activity ID");
    CHECK(!am.logActivity("P001", "Teleported", "Registered", "OnHold"), "unknown type refused");
    CHECK(!am.logActivity("X1", ACT_SORTED, "Registered", "OnHold"), "bad Parcel ID refused");
    CHECK(!am.logActivity("P001", ACT_SORTED, "Registered", "Flying"), "unknown status refused");
    CHECK(!am.logActivity("P001", ACT_SORTED, "Registered", "OnHold", "-", "-", "2026-13-01 10:00"), "bad timestamp refused");
    CHECK(am.count() == rows + 1 && rowCount(fx.activities) == rows + 1, "refused activities are not saved");

    { Console c(""); CHECK(am.displayParcelHistory("P002") > 0, "P002 has history"); }
    { Console c(""); CHECK(am.displayParcelHistory("P999") == 0, "P999 has none"); }
    { Console c(""); CHECK(am.displayParcelHistory("bad") == -1, "invalid Parcel ID"); }
    { Console c(""); CHECK(am.reviewParcel("P002") > 0, "review P002"); }
}

static void task4_undo() {
    startTest("undo");
    std::string msg;
    {
        System s("t4_undo_sorted");
        Parcel sorted;
        s.parcels.processNextParcel("P021", ROUTE_SPECIAL_HANDLING, sorted, msg);
        s.parcels.processNextParcel("P022", ROUTE_COLLECTION, sorted, msg);
        CHECK(s.activities.latest()->type == ACT_SORTED, "sorting is logged through the hook");
        CHECK_MSG(s.activities.undoLatest(msg), "undo the newest Sorted (P022)", msg);
        CHECK(parcelStatus(s.parcels, "P022") == "Registered", "P022 back to Registered");
        CHECK_MSG(s.activities.undoLatest(msg), "undo again: the Sorted before it (P021)", msg);
        CHECK(parcelStatus(s.parcels, "P021") == "Registered" && s.parcels.waitingCount() == 10, "both back in the queue");
        CHECK(s.activities.latest()->type == ACT_UNDONE, "an Undone activity is logged");
    }
    {
        System s("t4_undo_assigned");
        std::string res;
        s.resources.assignRequest("R009", "CampusDelivery", &res);
        s.service.markAssigned("R009", res, msg);
        CHECK(s.activities.latest()->type == ACT_ASSIGNED, "assignment is logged");
        CHECK_MSG(s.activities.undoLatest(msg), "undo the assignment", msg);
        CHECK(s.resources.findResource(res)->status == AVAILABLE, "resource freed");
        CHECK(requestStatus(s.service, "R009") == "Pending", "request back to Pending");
        CHECK(parcelStatus(s.parcels, "P023") == "Registered", "parcel back to Registered");
        CHECK(field(s.fx.resources, res, 3) == "Available", "resources.csv saved");
    }
    {
        System s("t4_undo_refused");
        s.service.createRequest("P027", "TP012347", SERVICE_CAMPUS_DELIVERY, 2, msg);
        CHECK_MSG(!s.activities.undoLatest(msg) && contains(msg, "cannot be reversed"),
            "RequestCreated cannot be undone", msg);

        Parcel sorted;
        s.parcels.processNextParcel("P021", ROUTE_SPECIAL_HANDLING, sorted, msg);
        setField(s.fx.parcels, "P021", 7, "OnHold");          // changed without a log entry
        CHECK_MSG(!s.activities.undoLatest(msg) && contains(msg, "changed"),
            "refused when the parcel changed after the activity", msg);

        s.parcels.updateStatus("P017", "ReturnToSender", msg);
        s.parcels.updateStatus("P017", "Returned", msg);
        CHECK_MSG(!s.activities.undoLatest(msg) && contains(msg, "finished"),
            "a change that finished the parcel is not reopened", msg);
    }
}

// ============================================================================
//  TASK 5 - Parcel category BST (menu-driven, so tested through run())
// ============================================================================
// Rows printed by Task 5 look like "  P001 | name | ... | status"
static int countParcelLines(const std::string& text, std::string statuses[], int max) {
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        if (line.size() > 3 && line.compare(0, 3, "  P") == 0 && contains(line, " | ")) {
            if (n < max) statuses[n] = line.substr(line.rfind(" | ") + 3);
            n++;
        }
    }
    return n;
}

static std::string lower(std::string s) {
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)std::tolower((unsigned char)s[i]);
    return s;
}

static bool nonDecreasing(std::string v[], int n) {
    for (int i = 1; i < n; i++) if (lower(v[i - 1]) > lower(v[i])) return false;
    return true;
}

static void task5_organiseAndSearch() {
    startTest("organise + search");
    Fixture fx("t5_search");
    ParcelCategoryModule cat(fx.parcels, fx.requests);
    CHECK(cat.load(), "loads parcels.csv into the BST");
    std::string statuses[MAX_ROWS];
    std::string out;

    { Console c("4\n0\n"); cat.run(); out = c.text(); }
    CHECK(countParcelLines(out, statuses, MAX_ROWS) == rowCount(fx.parcels), "display shows every parcel");

    { Console c("1\n4\n4\n0\n"); cat.run(); out = c.text(); }
    int n = countParcelLines(out, statuses, MAX_ROWS);
    CHECK(n == rowCount(fx.parcels) && nonDecreasing(statuses, n), "key = Status: in-order display is sorted by status");

    std::string want = std::to_string(countWhere(fx.parcels, 7, "Registered"));
    { Console c("1\n4\n3\n2\nregistered\n0\n"); cat.run(); out = c.text(); }
    CHECK_MSG(contains(out, "with " + want + " parcel(s)"), "search key 'registered' finds every Registered parcel", "");

    want = std::to_string(countWhere(fx.parcels, 4, "Fragile"));
    { Console c("1\n3\n3\n2\nFRAGILE\n0\n"); cat.run(); out = c.text(); }
    CHECK(contains(out, "with " + want + " parcel(s)"), "key = Parcel Type, case-insensitive search");

    { Console c("3\n1\nP999\n0\n"); cat.run(); out = c.text(); }
    CHECK(contains(out, "does not exist"), "missing Parcel ID reported");
    { Console c("1\n4\n3\n2\nTeleported\n0\n"); cat.run(); out = c.text(); }
    CHECK(contains(out, "No parcel category"), "missing key reported");
}

static void task5_insertUpdateRemove() {
    startTest("insert / update / remove");
    std::string out;
    {
        Fixture fx("t5_insert");
        ParcelModule pm(fx.parcels, fx.requests);
        pm.load();
        ParcelCategoryModule cat(fx.parcels, fx.requests);
        cat.load();
        { Console c("2\nZara Lee\nTP777777\nCounter\nStandard\nDHL\n\n0\n"); cat.run(); out = c.text(); }
        CHECK_MSG(contains(out, "[OK]") == false && contains(out, "not connected"), "insert refused until Task 1 is connected", "");
        cat.setParcelModule(&pm);
        { Console c("2\nZara Lee\nTP777777\nCounter\nStandard\nDHL\n\n0\n"); cat.run(); out = c.text(); }
        CHECK(contains(out, "[OK]") && field(fx.parcels, "P041", 7) == "Registered", "insert goes through Task 1 (P041 Registered)");
        CHECK(pm.waitingCount() == countWhere(fx.parcels, 7, "Registered"), "Task 1 queues the inserted parcel");
        { Console c("2\nJ0hn\nTP12\nCounter\nStandard\nDHL\n\n0\n"); cat.run(); out = c.text(); }
        CHECK(contains(out, "[Error]") && !hasRow(fx.parcels, "P042"), "Task 1's rules reject a bad recipient");
    }
    {
        Fixture fx("t5_update");
        ParcelCategoryModule cat(fx.parcels, fx.requests);
        cat.load();
        { Console c("5\nP023\n0\n"); cat.run(); out = c.text(); }
        CHECK(contains(out, "active request"), "parcel with an active request cannot be edited");
        { Console c("5\nP027\n3\nBlock Z L9\n0\n"); cat.run(); out = c.text(); }
        CHECK(field(fx.parcels, "P027", 3) == "Block Z L9", "delivery information updated");

        { Console c("5\nP027\n2\nabc\n0\n"); cat.run(); out = c.text(); }
        CHECK_MSG(field(fx.parcels, "P027", 2) == "TP012347",
            "recipient ID update must follow Task 1's format (TP + 6 digits / STAFF + 3 digits)",
            "Task 5 saved recipient ID '" + field(fx.parcels, "P027", 2) + "'");
        setField(fx.parcels, "P027", 2, "TP012347");
        { Console c("5\nP027\n2\nTP012345\n0\n"); cat.run(); out = c.text(); }
        CHECK_MSG(field(fx.parcels, "P027", 2) == "TP012347",
            "recipient ID update must not take another person's ID",
            "Task 5 gave P027 (Nur Sofea) recipient ID TP012345, which belongs to Aina Rahman");
        { Console c("5\nP027\n6\n2026-02-31 10:00\n0\n"); cat.run(); out = c.text(); }
        CHECK_MSG(field(fx.parcels, "P027", 6) != "2026-02-31 10:00",
            "arrival date must be a real date", "Task 5 saved 31 February");
        { Console c("5\nP027\n6\n2024-02-29 10:00\n0\n"); cat.run(); out = c.text(); }
        CHECK(field(fx.parcels, "P027", 6) == "2024-02-29 10:00", "a leap day is accepted");
        { Console c("5\nP027\n1\nJ0hn\n0\n"); cat.run(); out = c.text(); }
        CHECK(field(fx.parcels, "P027", 1) == "Nur Sofea", "recipient name update follows Task 1's name rule");
        { Console c("5\nP027\n1\nBob Tan\n0\n"); cat.run(); out = c.text(); }
        CHECK(field(fx.parcels, "P027", 1) == "Nur Sofea",
            "renaming P027 is refused while P003 still has TP012347 under Nur Sofea");
        { Console c("5\nP027\n2\ntp999999\n0\n"); cat.run(); out = c.text(); }
        CHECK(field(fx.parcels, "P027", 2) == "TP999999", "a new valid recipient ID is accepted and stored in upper case");
    }
    {
        Fixture fx("t5_remove");
        ParcelModule pm(fx.parcels, fx.requests);
        pm.load();
        ParcelCategoryModule cat(fx.parcels, fx.requests);
        cat.load();
        { Console c("6\nP001\n0\n"); cat.run(); out = c.text(); }
        CHECK(hasRow(fx.parcels, "P001"), "collected parcel kept as history");
        { Console c("6\nP023\n0\n"); cat.run(); out = c.text(); }
        CHECK(hasRow(fx.parcels, "P023"), "parcel with an active request kept");
        { Console c("6\nP027\n0\n"); cat.run(); out = c.text(); }
        CHECK(!hasRow(fx.parcels, "P027"), "Registered parcel without a request removed");
        Parcel p;
        CHECK(!pm.findParcel("P027", p) && pm.waitingCount() == countWhere(fx.parcels, 7, "Registered"),
            "Task 1 sees the removal and drops it from the queue");

        std::string statuses[MAX_ROWS];
        { Console c("1\n4\n6\nP007\n4\n0\n"); cat.run(); out = c.text(); }
        int n = countParcelLines(out, statuses, MAX_ROWS);
        CHECK(!hasRow(fx.parcels, "P007") && n == rowCount(fx.parcels) && nonDecreasing(statuses, n) &&
            !contains(lower(fromLast(out, "=== Parcel records")), "| cancelled"),
            "removing the only Cancelled parcel deletes its BST node and keeps the order");
    }
}

static void task5_bstDeleteShapes() {
    startTest("BST delete (two-children node)");
    Fixture fx("t5_bst");
    std::string text = "ParcelID,RecipientName,RecipientID,DeliveryInfo,ParcelType,Courier,ArrivalDateTime,Status\n";
    const char* order[9] = { "P050", "P030", "P070", "P020", "P040", "P060", "P080", "P065", "P075" };
    for (int i = 0; i < 9; i++)
        text += std::string(order[i]) + ",Test Person,TP9000" + std::to_string(10 + i) +
        ",Counter,Standard,DHL,2026-10-01 10:00,Registered\n";
    writeFile(fx.parcels, text);
    writeFile(fx.requests, "RequestID,ParcelID,ServiceType,Urgency,RequestTime,Status\n");

    ParcelCategoryModule cat(fx.parcels, fx.requests);
    cat.load();
    std::string out, ids[20];
    // remove the root (two children, successor deep in the right subtree),
    // then P030 (two children, successor is its right child), then a leaf
    { Console c("6\nP050\n6\nP030\n6\nP075\n4\n0\n"); cat.run(); out = c.text(); }
    std::istringstream in(fromLast(out, "=== Parcel records"));
    std::string line;
    int n = 0;
    while (std::getline(in, line))
        if (line.compare(0, 3, "  P") == 0) ids[n++] = line.substr(2, 4);
    const char* expect[6] = { "P020", "P040", "P060", "P065", "P070", "P080" };
    bool ok = (n == 6);
    for (int i = 0; ok && i < 6; i++) ok = (ids[i] == expect[i]);
    CHECK(ok, "after deleting root / inner node / leaf, in-order is still sorted and complete");

    writeFile(fx.parcels, text + "P020,Dup,TP900099,Counter,Standard,DHL,2026-10-01 10:00,Registered\n");
    ParcelCategoryModule dup(fx.parcels, fx.requests);
    CHECK(!dup.load(), "file with a duplicate Parcel ID is refused");
}

// ============================================================================
//  INTEGRATION - the full parcel journey across the modules
// ============================================================================
static void integration_fullJourney() {
    startTest("parcel journey: sort -> request -> assign -> complete -> release");
    System s("int_journey");
    std::string msg, res;
    int logBefore = s.activities.count();
    Parcel sorted;

    CHECK(s.parcels.processNextParcel("P021", ROUTE_SPECIAL_HANDLING, sorted, msg), "Task 1 sorts P021");
    CHECK_MSG(s.service.createRequest("P021", "TP012345", SERVICE_SPECIAL_HANDLING, 2, msg), "Task 2 opens R020", msg);
    CHECK(s.resources.assignRequest("R020", SERVICE_SPECIAL_HANDLING, &res) && res == "C01", "Task 3 gives it C01");
    CHECK_MSG(s.service.markAssigned("R020", res, msg), "Task 2 marks R020 Assigned", msg);
    CHECK_MSG(s.service.completeRequest("R020", "", msg), "special handling completed", msg);
    CHECK(parcelStatus(s.parcels, "P021") == "ReadyForCollection", "parcel ready for collection afterwards");
    CHECK(s.resources.releaseResource(res) && s.resources.findResource(res)->status == AVAILABLE, "Task 3 frees C01");
    CHECK(s.activities.count() == logBefore + 4, "Task 4 logged Sorted, RequestCreated, Assigned, Completed");

    CHECK(s.parcels.processNextParcel("P022", ROUTE_COLLECTION, sorted, msg), "P022 sorted (it already has R011)");
    CHECK(!s.service.createRequest("P022", "TP012362", SERVICE_COLLECTION, 1, msg), "no duplicate request for P022");
}

static void integration_returnToSender() {
    startTest("return to sender journey");
    System s("int_return");
    std::string msg, res;
    Parcel sorted;
    CHECK(s.parcels.processNextParcel("P021", ROUTE_RETURN, sorted, msg) && sorted.status == "ReturnToSender",
        "Task 1 routes P021 to ReturnToSender");
    CHECK_MSG(s.service.createRequest("P021", "TP012345", SERVICE_RETURN, 1, msg), "Task 2 opens a Return request", msg);
    CHECK(s.resources.assignRequest("R020", SERVICE_RETURN, &res) && s.service.markAssigned("R020", res, msg),
        "assigned to a counter");
    CHECK_MSG(s.service.completeRequest("R020", "", msg), "return completed", msg);
    CHECK(parcelStatus(s.parcels, "P021") == "Returned", "parcel Returned");
}

static void integration_resourceLost() {
    startTest("resource becomes unavailable mid-job");
    System s("int_lost");
    CHECK(requestStatus(s.service, "R004") == "Assigned", "R004 starts Assigned on D01");
    s.resources.updateResourceStatus("D01", UNAVAILABLE);
    CHECK(requestStatus(s.service, "R004") == "Pending", "Task 2 puts R004 back to Pending through the hook");
    std::string res;
    CHECK(s.resources.assignRequest("R004", SERVICE_CAMPUS_DELIVERY, &res) && res == "D04", "R004 can be re-assigned to D04");
}

static void integration_releaseCompletesCollection() {
    startTest("releasing a counter completes its collection");
    System s("int_release");
    std::string out;
    bool ok;
    { Console c("TP000000\n"); ok = s.resources.releaseResource("C02"); }
    CHECK(!ok && s.resources.findResource("C02")->status == OCCUPIED, "wrong collector: counter not released");
    { Console c("STAFF024\n"); ok = s.resources.releaseResource("C02"); }
    CHECK(ok && requestStatus(s.service, "R003") == "Completed" && parcelStatus(s.parcels, "P024") == "Collected",
        "right collector: R003 completed, P024 Collected, C02 free");
}

// ============================================================================
int main(int argc, char* argv[]) {
    std::ostream report(std::cout.rdbuf());
    g_report = &report;

    // Data folder: argument, or the usual places relative to the Tests folder
    const char* tries[4] = { "../../Data", "../Data", "Data", "../../../Data" };
    if (argc > 1) g_dataDir = argv[1];
    for (int i = 0; g_dataDir.empty() && i < 4; i++)
        if (fs::exists(fs::path(tries[i]) / "parcels.csv")) g_dataDir = tries[i];
    if (g_dataDir.empty() || !fs::exists(fs::path(g_dataDir) / "parcels.csv")) {
        report << "Cannot find the Data folder. Run: SystemTests <path to Data>\n";
        return 2;
    }

    NullBuffer quiet;                       // module messages are not part of the report
    std::cout.rdbuf(&quiet);
    std::istringstream noInput("");
    std::cin.rdbuf(noInput.rdbuf());        // nothing may block waiting for a keyboard

    report << "Smart Campus Parcel System - automated tests\nData: " << g_dataDir << "\n";

    startGroup(0);
    task1_intakeQueue(); task1_parcelStore(); task1_loadAndRegister();
    task1_sorting(); task1_statusRules(); task1_syncAndRestore();
    startGroup(1);
    task2_priorityQueue(); task2_createAndValidate(); task2_assignCompleteCancel();
    startGroup(2);
    task3_circularQueue(); task3_register(); task3_assign(); task3_releaseAndStatus(); task3_menuWithTask2();
    startGroup(3);
    task4_stack(); task4_logAndHistory(); task4_undo();
    startGroup(4);
    task5_organiseAndSearch(); task5_insertUpdateRemove(); task5_bstDeleteShapes();
    startGroup(5);
    integration_fullJourney(); integration_returnToSender(); integration_resourceLost();
    integration_releaseCompletesCollection();

    int pass = 0, fail = 0;
    report << "\n" << std::string(60, '=') << "\nSUMMARY\n";
    for (int i = 0; i < GROUP_COUNT; i++) {
        report << "  " << (g_fail[i] ? "FAIL " : "ok   ") << GROUP_NAMES[i] << ": "
            << g_pass[i] << " passed, " << g_fail[i] << " failed\n";
        pass += g_pass[i];
        fail += g_fail[i];
    }
    report << "  TOTAL: " << pass << " passed, " << fail << " failed\n";
    std::cout.rdbuf(report.rdbuf());
    return fail == 0 ? 0 : 1;
}
