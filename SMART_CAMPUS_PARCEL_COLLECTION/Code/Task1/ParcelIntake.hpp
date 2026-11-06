// ============================================================================
// ParcelIntake.hpp
// Task 1 - Parcel Intake, Sorting and Processing Module
// CT077-3-2-DSTR  |  Smart Campus Parcel Collection and Delivery System
//
// DATA STRUCTURES (both written by us, no STL containers)
//
//   1. IntakeQueue - a FIFO QUEUE built on a singly linked list.
//      It holds the parcels that are registered but not sorted yet
//      (status "Registered"). Staff always sort the parcel at the FRONT
//      (first registered, first sorted). A newly registered parcel joins
//      the REAR.
//        enqueue  O(1)   link a new node after 'rear'
//        dequeue  O(1)   unlink the node at 'front'
//        remove   O(n)   unlink a parcel from the middle (cancelled or
//                        reported missing before it was sorted)
//      Why a linked-list queue?
//        - Intake must follow arrival order, which is exactly FIFO. A stack
//          would sort the newest parcel first and leave old ones waiting.
//        - Urgency is not decided here. Task 2's priority queue handles it
//          once a service request exists.
//        - In peak periods hundreds of parcels can be waiting. Linked nodes
//          grow one at a time with no fixed MAX, no resizing and no shifting.
//          (Task 3's array circular queue suits its small, fixed set of
//          counters; the intake volume is not fixed.)
//
//   2. ParcelStore - a dynamic array of ALL parcel records, kept in
//      ascending Parcel ID order (P001, P002, ...).
//        find by Parcel ID    O(log n)  binary search
//        insert               O(n)      binary search for the slot, then shift
//                                       right (new IDs are the largest, so in
//                                       practice nothing moves)
//        remove               O(n)      shift left
//        search by recipient  O(n)      linear search (name / recipient ID
//                                       are not the sort key)
//      Loading inserts every row into its sorted place (binary insertion),
//      which also rejects duplicate Parcel IDs.
//
//   Invariant: a parcel is in the intake queue if and only if its status
//   is "Registered" (the same idea as Task 2's "queued iff Pending").
//
// SORTING RULE (the suggested next action - staff may choose another)
//   1. Parcel already has an active Task 2 request -> the matching route
//   2. Fragile parcel                              -> SpecialHandling
//   3. Delivery info "Counter"                     -> ReadyForCollection
//   4. Any other campus location                   -> AssignedForDelivery
//   ReturnToSender and OnHold are exception routes that staff choose.
//
// FILES (integration contract with the other modules)
//   parcels.csv   - owned by this module. Task 2 also rewrites the Status
//                   column, so every operation first re-syncs with the file
//                   and every change is saved straight away. Nothing another
//                   module wrote is ever overwritten.
//   requests.csv  - owned by Task 2. Only READ here, to show a parcel's
//                   active request and to block changes that would leave
//                   that request pointing at an invalid parcel.
// ============================================================================
#ifndef PARCEL_INTAKE_HPP
#define PARCEL_INTAKE_HPP

#include <string>

// ------------------------------------------------------------ parcel statuses
// Waiting to be sorted (in the intake queue)
const std::string PARCEL_REGISTERED = "Registered";
// Sorted, waiting for the next stage
const std::string PARCEL_READY_FOR_COLLECTION = "ReadyForCollection";
const std::string PARCEL_ASSIGNED_FOR_DELIVERY = "AssignedForDelivery";
const std::string PARCEL_SPECIAL_HANDLING = "SpecialHandling";
const std::string PARCEL_RETURN_TO_SENDER = "ReturnToSender";
const std::string PARCEL_ON_HOLD = "OnHold";
// Exception
const std::string PARCEL_MISSING = "Missing";
// Completed (nothing can be done to these any more)
const std::string PARCEL_COLLECTED = "Collected";
const std::string PARCEL_DELIVERED = "Delivered";
const std::string PARCEL_RETURNED = "Returned";
const std::string PARCEL_CANCELLED = "Cancelled";

// ----------------------------------------------------------------- record
struct Parcel {
    std::string parcelID;         // P001, P002, ...
    std::string recipientName;
    std::string recipientID;      // TPxxxxxx (student) or STAFFxxx (staff)
    std::string deliveryInfo;     // "Counter" or a campus location
    std::string parcelType;       // Standard / Fragile / Urgent / Oversized
    std::string courier;
    std::string arrivalDateTime;  // "YYYY-MM-DD HH:MM" (compares correctly as text)
    std::string status;
};

// The five places a parcel can be sent when it is sorted
enum ParcelRoute {
    ROUTE_COLLECTION = 0,      // -> ReadyForCollection
    ROUTE_DELIVERY,            // -> AssignedForDelivery
    ROUTE_SPECIAL_HANDLING,    // -> SpecialHandling
    ROUTE_RETURN,              // -> ReturnToSender
    ROUTE_HOLD                 // -> OnHold (investigation)
};
const int ROUTE_COUNT = 5;

std::string parcelRouteStatus(ParcelRoute route);   // status the route gives
std::string parcelRouteLabel(ParcelRoute route);    // text for menus

// ------------------------------------------- ParcelStore (sorted dynamic array)
class ParcelStore {
private:
    Parcel* items;
    int count;
    int capacity;
    void grow();                                   // doubles the capacity

public:
    ParcelStore();
    ~ParcelStore();
    ParcelStore(const ParcelStore&) = delete;
    ParcelStore& operator=(const ParcelStore&) = delete;

    int size() const { return count; }
    Parcel& at(int i) { return items[i]; }
    const Parcel& at(int i) const { return items[i]; }

    int find(const std::string& parcelID) const;   // binary search, -1 if absent
    int insert(const Parcel& p);                   // keeps ID order, -1 if duplicate
    void removeAt(int i);                          // shifts the rest left
    void swapWith(ParcelStore& other);             // used when re-syncing with the file
    std::string nextID() const;                    // highest number + 1
};

// ------------------------------------------- IntakeQueue (linked-list FIFO queue)
class IntakeQueue {
private:
    struct Node {
        std::string parcelID;
        Node* next;
    };
    Node* front;                                   // next parcel to be sorted
    Node* rear;                                    // most recently registered
    int count;

public:
    IntakeQueue();
    ~IntakeQueue();
    IntakeQueue(const IntakeQueue&) = delete;
    IntakeQueue& operator=(const IntakeQueue&) = delete;

    bool isEmpty() const { return count == 0; }
    int  size() const { return count; }
    void enqueue(const std::string& parcelID);     // O(1)
    bool dequeue(std::string& parcelID);           // O(1), false if empty
    bool peek(std::string& parcelID) const;        // O(1), false if empty
    bool remove(const std::string& parcelID);      // O(n), false if not queued
    int  position(const std::string& parcelID) const; // 1 = front, 0 = not queued
    int  toArray(std::string* out, int max) const; // front-to-rear copy for display
    void clear();
};

// ----------------------------------------------------------- ParcelModule
class ParcelModule {
private:
    ParcelStore store;
    IntakeQueue intake;
    std::string parcelsPath;                       // where parcels.csv lives
    std::string requestsPath;                      // Task 2's requests.csv (read only)

    bool readFile(ParcelStore& into, bool verbose, int& loaded, int& skipped) const;
    bool reload(bool verbose);                     // file -> memory, keeps queue order
    void sync() { reload(false); }                 // called before every operation
    bool commit();                                 // save, or undo the change if saving fails
    bool findActiveRequest(const std::string& parcelID, std::string& requestID,
        std::string& serviceType, std::string& requestStatus) const;
    void printDetails(const Parcel& p) const;

    // menu handlers (console I/O only)
    void showMenu() const;
    void menuRegister();
    void menuDisplay();
    void menuSearch();
    void menuView();
    void menuDisplayQueue();
    void menuSort();
    void menuUpdateStatus();
    void menuUpdateDelivery();
    void menuReportMissing();
    void menuRemove();

public:
    // The paths let Main.cpp point the module at the shared Data folder.
    explicit ParcelModule(const std::string& parcelsFile = "parcels.csv",
        const std::string& requestsFile = "requests.csv");

    bool load();                                   // first load, prints a summary
    bool save() const;                             // write parcels.csv
    void run();                                    // interactive submenu

    // ---- integration API (no console input; result text returned in msg) ----
    // Req 1: register. 'input' needs every field except parcelID and status,
    // which are assigned here. Empty arrivalDateTime = now.
    bool registerParcel(const Parcel& input, std::string& newID, std::string& msg);
    // Req 2: locate
    bool findParcel(const std::string& parcelID, Parcel& out);
    int  waitingCount();
    // Req 3: sort the parcel at the front of the intake queue
    bool peekNextParcel(Parcel& out);
    ParcelRoute suggestRoute(const Parcel& p, std::string& reason) const;
    bool processNextParcel(const std::string& expectedID, ParcelRoute route,
        Parcel& sorted, std::string& msg);
    // Console helper used by this module's menu AND by Main.cpp's integrated
    // "sort + create request" option: shows the front parcel, lets staff pick
    // a route and sorts it. Returns true when a parcel was sorted.
    bool sortNextParcel(Parcel& sorted);
    // Urgency (1-3) Task 2 should give a request for this parcel
    int  suggestedUrgency(const Parcel& p) const;
    // Req 4: update / remove
    bool updateStatus(const std::string& parcelID, const std::string& newStatus, std::string& msg);
    bool updateDeliveryInfo(const std::string& parcelID, const std::string& newInfo, std::string& msg);
    bool reportMissing(const std::string& parcelID, std::string& msg);
    bool removeParcel(const std::string& parcelID, std::string& msg);
    // For Task 4 (undo): put a parcel back to an earlier status, skipping the
    // normal transition rules but keeping the intake queue correct.
    bool restoreParcelStatus(const std::string& parcelID, const std::string& previousStatus,
        std::string& msg);
};

#endif
