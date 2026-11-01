// ============================================================================
// ServiceRequest.hpp
// Task 2 - Parcel Service and Collection Management Module
// CT077-3-2-DSTR  |  Smart Campus Parcel Collection and Delivery System
//
// DATA STRUCTURE CHOICE
//   Active (Pending) service requests are held in a PRIORITY QUEUE built on a
//   plain array that we manage ourselves (no STL containers), exactly as taught
//   in the lecture: items are stored in ARRIVAL ORDER (no sorting) and the
//   highest-priority item is found by scanning the whole array.
//
//   Priority rule (the "next request" rule):
//     1. Higher urgency first            (3 = most urgent, 1 = least urgent)
//     2. Earlier RequestTime first       (first come, first served on equal urgency)
//     3. Smaller RequestID first         (final tie-breaker for equivalent requests)
//
//   Why a priority queue?  A plain (FIFO) queue ignores urgency, a stack is
//   LIFO, and a circular queue is for reusing array slots / cyclic work.
//   Only a priority queue lets an urgent parcel overtake earlier, less urgent ones.
//
//   Time complexity (n = number of Pending requests)
//     enqueue  O(1)   add at the rear, no sorting
//     peek     O(n)   scan for the highest priority
//     dequeue  O(n)   scan, then shift the remaining items left
//     cancel   O(n)   find the item, then shift
//   The array grows by doubling, so there is no fixed MAX.
//
//   All requests (including Assigned / Completed / Cancelled ones) live in a
//   second self-written dynamic array (RequestStore). The queue stores only
//   INDICES into that array, so there is exactly one copy of every request.
//   Invariant: a request is in the queue if and only if its status is Pending.
//
// FILES (integration contract with the other modules)
//   requests.csv  - owned by this module (read + written here)
//   parcels.csv   - owned by Task 1. This module only changes the Status column
//                   (always reloaded just before and saved just after the edit)
// ============================================================================
#ifndef SERVICE_REQUEST_HPP
#define SERVICE_REQUEST_HPP

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
using namespace std;

// ---------------------------------------------------------------- constants
const string REQUESTS_FILE = "requests.csv";
const string PARCELS_FILE = "parcels.csv";

const int URGENCY_MIN = 1;
const int URGENCY_MAX = 3;   // 3 = most urgent

// Request statuses
const string STATUS_PENDING = "Pending";
const string STATUS_ASSIGNED = "Assigned";
const string STATUS_COMPLETED = "Completed";
const string STATUS_CANCELLED = "Cancelled";

// Service types
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
    string requestTime;     // "YYYY-MM-DD HH:MM"  (compares correctly as text)
    string status;
};

// One row of parcels.csv (Task 1's file)
struct ParcelRecord
{
    string parcelID, recipientName, recipientID, deliveryInfo;
    string parcelType, courier, arrivalDateTime, status;
};

// ------------------------------------------------- RequestStore (dyn. array)
class RequestStore
{
private:
    ServiceRequest* items;
    int count;
    int capacity;
    void grow();                                   // doubles the capacity

public:
    RequestStore();
    ~RequestStore();
    RequestStore(const RequestStore&) = delete;
    RequestStore& operator=(const RequestStore&) = delete;

    int  add(const ServiceRequest& r);             // returns index of new item
    int  size() const { return count; }
    ServiceRequest& at(int i) { return items[i]; }
    const ServiceRequest& at(int i) const { return items[i]; }
    int  findByID(const string& id) const;         // -1 if not found
    int  findActiveByParcel(const string& parcelID) const; // Pending/Assigned
    string nextID() const;                         // R001, R002, ...
};

// ---------------------------------------------- RequestQueue (priority queue)
class RequestQueue
{
private:
    const RequestStore* store;
    int* arr;                                      // store indices, ARRIVAL order
    int count;
    int capacity;

    bool higher(int a, int b) const;               // does request a outrank b?
    int  findTopPos() const;                       // scan: position of best item, -1 if empty
    void removeAt(int pos);                        // shift items left over pos
    void grow();

public:
    explicit RequestQueue(const RequestStore* s);
    ~RequestQueue();
    RequestQueue(const RequestQueue&) = delete;
    RequestQueue& operator=(const RequestQueue&) = delete;

    bool isEmpty() const { return count == 0; }
    int  size() const { return count; }
    void enqueue(int storeIndex);                  // O(1), no sorting
    int  peek() const;                             // O(n), -1 if empty
    int  dequeue();                                // O(n), -1 if empty
    bool remove(int storeIndex);                   // O(n), used when cancelling
    void snapshotSorted(int* out) const;           // priority order, queue untouched
};

// ----------------------------------------------------- ParcelTable (parcels.csv)
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
    int  find(const string& parcelID) const;       // -1 if not found
    ParcelRecord& at(int i) { return items[i]; }
    const ParcelRecord& at(int i) const { return items[i]; }
};

// ------------------------------------------------------------- ServiceModule
class ServiceModule
{
private:
    RequestStore store;
    RequestQueue pendingQueue;                     // declared after store (init order)
    string requestsPath;                           // where requests.csv lives
    string parcelsPath;                            // where parcels.csv lives

    void applyStatus(int idx, const string& newStatus); // keeps queue in sync
    void persist(string& msg) const;                    // save + warn on failure

    // menu handlers (console I/O only)
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
    // The paths let Main.cpp point the module at the shared Data folder.
    explicit ServiceModule(const string& requestsFile = REQUESTS_FILE,
        const string& parcelsFile = PARCELS_FILE);

    bool load();                                   // read requests.csv
    bool save() const;                             // write requests.csv
    void run();                                    // interactive submenu

    // ---- integration API (no console input; result text returned in msg) ----
    // Task 1 can call createRequest after sorting a parcel for its next action.
    bool createRequest(const string& parcelID, const string& recipientID,
        const string& serviceType, int urgency, string& msg);
    bool peekNext(ServiceRequest& out) const;
    // ---- assignment hand-over with Task 3 ----
    // 1) peekNext()                 : which Pending request is most urgent? (queue unchanged)
    // 2) validateForAssignment()    : is it safe to give it to a resource right now?
    // 3) Task 3 allocates a counter / delivery person and saves resources.csv
    // 4) markAssigned()             : Pending -> Assigned, parcel updated, requests.csv saved
    // A request is therefore only ever Assigned when a resource really holds it.
    bool validateForAssignment(const string& requestID, string& msg) const;
    bool markAssigned(const string& requestID, string& msg);
    // Assigned -> Completed and updates the parcel's status in parcels.csv.
    bool completeRequest(const string& requestID, const string& collectorID, string& msg);
    // Pending/Assigned -> Cancelled.
    bool cancelRequest(const string& requestID, string& msg);
    // Used by Task 4 (undo): put a request back to an earlier status.
    bool restoreRequestStatus(const string& requestID, const string& previousStatus, string& msg);
    bool findRequest(const string& requestID, ServiceRequest& out) const;
};

#endif