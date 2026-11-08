// ============================================================================
// ServiceRequest.hpp
// Task 2 - Parcel Service and Collection Management Module
//
// DATA STRUCTURE CHOICE
//   Active (Pending) service requests are held in a PRIORITY QUEUE built on a
//   plain array (no STL containers). Items are stored in ARRIVAL ORDER, and the
//   highest-priority item is found by scanning the whole array.
//
//   Priority rule:
//     1. Higher urgency first            (3 = most urgent, 1 = least urgent)
//     2. Earlier RequestTime first       (FIFO on equal urgency)
//     3. Smaller RequestID first         (final tie-breaker)
//
//   Complexity: enqueue O(1); peek O(n); dequeue O(n); cancel O(n).
//
//   All requests live in RequestStore (dynamic array). The queue stores
//   INDICES into that array, so there is exactly one copy of every request.
//
// Activity reporting
//   Every successful change publishes an ActivityEvent through activityHook
//   (set by Main.cpp). Task 4 logs it. If no hook is registered, publishing
//   is a safe no-op.
// ============================================================================
#ifndef SERVICE_REQUEST_HPP
#define SERVICE_REQUEST_HPP

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include "../ActivityHook.hpp"   // NEW
using namespace std;

// ---------------------------------------------------------------- constants
const string REQUESTS_FILE = "requests.csv";
const string PARCELS_FILE = "parcels.csv";

const int URGENCY_MIN = 1;
const int URGENCY_MAX = 3;

const string STATUS_PENDING = "Pending";
const string STATUS_ASSIGNED = "Assigned";
const string STATUS_COMPLETED = "Completed";
const string STATUS_CANCELLED = "Cancelled";

const string SERVICE_COLLECTION = "Collection";
const string SERVICE_CAMPUS_DELIVERY = "CampusDelivery";
const string SERVICE_SPECIAL_HANDLING = "SpecialHandling";
const string SERVICE_REDELIVERY = "ReDelivery";
const string SERVICE_RETURN = "Return";

// ------------------------------------------------------------------ records
struct ServiceRequest
{
    string requestID;
    string parcelID;
    string serviceType;
    int    urgency = 0;
    string requestTime;     // "YYYY-MM-DD HH:MM"
    string status;
};

struct ParcelRecord
{
    string parcelID, recipientName, recipientID, deliveryInfo;
    string parcelType, courier, arrivalDateTime, status;
};

// ------------------------------------------------- RequestStore
class RequestStore
{
private:
    ServiceRequest* items;
    int count;
    int capacity;
    void grow();

public:
    RequestStore();
    ~RequestStore();
    RequestStore(const RequestStore&) = delete;
    RequestStore& operator=(const RequestStore&) = delete;

    int  add(const ServiceRequest& r);
    int  size() const { return count; }
    ServiceRequest& at(int i) { return items[i]; }
    const ServiceRequest& at(int i) const { return items[i]; }
    int  findByID(const string& id) const;
    int  findActiveByParcel(const string& parcelID) const;
    string nextID() const;
};

// ---------------------------------------------- RequestQueue (priority queue)
class RequestQueue
{
private:
    const RequestStore* store;
    int* arr;
    int count;
    int capacity;

    bool higher(int a, int b) const;
    int  findTopPos() const;
    void removeAt(int pos);
    void grow();

public:
    explicit RequestQueue(const RequestStore* s);
    ~RequestQueue();
    RequestQueue(const RequestQueue&) = delete;
    RequestQueue& operator=(const RequestQueue&) = delete;

    bool isEmpty() const { return count == 0; }
    int  size() const { return count; }
    void enqueue(int storeIndex);
    int  peek() const;
    int  dequeue();
    bool remove(int storeIndex);
    void snapshotSorted(int* out) const;
};

// ----------------------------------------------------- ParcelTable
class ParcelTable
{
private:
    ParcelRecord* items;
    int count;
    int capacity;
    void grow();

public:
    ParcelTable();
    ~ParcelTable();
    ParcelTable(const ParcelTable&) = delete;
    ParcelTable& operator=(const ParcelTable&) = delete;

    bool load(const string& file);
    bool save(const string& file) const;
    int  find(const string& parcelID) const;
    ParcelRecord& at(int i) { return items[i]; }
    const ParcelRecord& at(int i) const { return items[i]; }
};

// ------------------------------------------------------------- ServiceModule
class ServiceModule
{
private:
    RequestStore store;
    RequestQueue pendingQueue;
    string requestsPath;
    string parcelsPath;
    ActivityHook activityHook = nullptr;                              // NEW

    void applyStatus(int idx, const string& newStatus);
    void persist(string& msg) const;

    // NEW: single helper used everywhere a state change needs reporting
    void publish(const string& parcelID, const string& type,
        const string& oldStatus, const string& newStatus,
        const string& requestID = "-",
        const string& resourceID = "-") const {
        publishActivity(activityHook, parcelID, type, oldStatus, newStatus,
            requestID, resourceID);
    }

    // menu handlers
    void showMenu() const;
    void menuCreate();
    void menuDisplayActive() const;
    void menuView() const;
    void menuNext() const;
    void menuMarkAssigned();
    void menuComplete();
    void menuCancel();
    void menuDisplayAll() const;

public:
    explicit ServiceModule(const string& requestsFile = REQUESTS_FILE,
        const string& parcelsFile = PARCELS_FILE);

    bool load();
    bool save() const;
    void run();

    // NEW: Main.cpp wires Task 4 in here
    void setActivityHook(ActivityHook h) { activityHook = h; }

    // ---- integration API ----
    bool createRequest(const string& parcelID, const string& recipientID,
        const string& serviceType, int urgency, string& msg);
    bool peekNext(ServiceRequest& out) const;
    bool validateForAssignment(const string& requestID, string& msg) const;

    // CHANGED: new parameter 'resourceID' (what was actually allocated in Task 3)
    bool markAssigned(const string& requestID, const string& resourceID, string& msg);

    bool completeRequest(const string& requestID, const string& collectorID, string& msg);
    bool cancelRequest(const string& requestID, string& msg);

    // Used by Task 4 undo — deliberately does NOT publish
    bool restoreRequestStatus(const string& requestID, const string& previousStatus, string& msg);

    bool findRequest(const string& requestID, ServiceRequest& out) const;
};

#endif