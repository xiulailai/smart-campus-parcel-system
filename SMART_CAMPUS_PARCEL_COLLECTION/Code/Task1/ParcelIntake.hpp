// ============================================================================
// ParcelIntake.hpp
// Task 1 - Parcel Intake, Sorting and Processing Module
// CT077-3-2-DSTR  |  Smart Campus Parcel Collection and Delivery System
//
// DATA STRUCTURES (both written by us, no STL containers)
//
//   1. IntakeQueue - a FIFO QUEUE built on a singly linked list.
//   2. ParcelStore - a dynamic array of ALL parcel records, kept in
//      ascending Parcel ID order.
//
// Invariant: a parcel is in the intake queue if and only if its status
// is "Registered".
//
// FILES (integration contract with the other modules)
//   parcels.csv   - owned by this module.
//   requests.csv  - owned by Task 2. Only READ here.
//
// Activity reporting
//   Every successful state change publishes an ActivityEvent through
//   activityHook (set by Main.cpp). Task 4 logs it. If no hook has been
//   registered, publishing is a safe no-op.
// ============================================================================
#ifndef PARCEL_INTAKE_HPP
#define PARCEL_INTAKE_HPP

#include <string>
#include "../ActivityHook.hpp"   // NEW

// ------------------------------------------------------------ parcel statuses
const std::string PARCEL_REGISTERED = "Registered";
const std::string PARCEL_READY_FOR_COLLECTION = "ReadyForCollection";
const std::string PARCEL_ASSIGNED_FOR_DELIVERY = "AssignedForDelivery";
const std::string PARCEL_SPECIAL_HANDLING = "SpecialHandling";
const std::string PARCEL_RETURN_TO_SENDER = "ReturnToSender";
const std::string PARCEL_ON_HOLD = "OnHold";
const std::string PARCEL_MISSING = "Missing";
const std::string PARCEL_COLLECTED = "Collected";
const std::string PARCEL_DELIVERED = "Delivered";
const std::string PARCEL_RETURNED = "Returned";
const std::string PARCEL_CANCELLED = "Cancelled";

// ----------------------------------------------------------------- record
struct Parcel {
    std::string parcelID;
    std::string recipientName;
    std::string recipientID;
    std::string deliveryInfo;
    std::string parcelType;
    std::string courier;
    std::string arrivalDateTime;
    std::string status;
};

enum ParcelRoute {
    ROUTE_COLLECTION = 0,
    ROUTE_DELIVERY,
    ROUTE_SPECIAL_HANDLING,
    ROUTE_RETURN,
    ROUTE_HOLD
};
const int ROUTE_COUNT = 5;

std::string parcelRouteStatus(ParcelRoute route);
std::string parcelRouteLabel(ParcelRoute route);

// ------------------------------------------- ParcelStore (sorted dynamic array)
class ParcelStore {
private:
    Parcel* items;
    int count;
    int capacity;
    void grow();

public:
    ParcelStore();
    ~ParcelStore();
    ParcelStore(const ParcelStore&) = delete;
    ParcelStore& operator=(const ParcelStore&) = delete;

    int size() const { return count; }
    Parcel& at(int i) { return items[i]; }
    const Parcel& at(int i) const { return items[i]; }

    int find(const std::string& parcelID) const;
    int insert(const Parcel& p);
    void removeAt(int i);
    void swapWith(ParcelStore& other);
    std::string nextID() const;
};

// ------------------------------------------- IntakeQueue (linked-list FIFO queue)
class IntakeQueue {
private:
    struct Node {
        std::string parcelID;
        Node* next;
    };
    Node* front;
    Node* rear;
    int count;

public:
    IntakeQueue();
    ~IntakeQueue();
    IntakeQueue(const IntakeQueue&) = delete;
    IntakeQueue& operator=(const IntakeQueue&) = delete;

    bool isEmpty() const { return count == 0; }
    int  size() const { return count; }
    void enqueue(const std::string& parcelID);
    bool dequeue(std::string& parcelID);
    bool peek(std::string& parcelID) const;
    bool remove(const std::string& parcelID);
    int  position(const std::string& parcelID) const;
    int  toArray(std::string* out, int max) const;
    void clear();
};

// ----------------------------------------------------------- ParcelModule
class ParcelModule {
private:
    ParcelStore store;
    IntakeQueue intake;
    std::string parcelsPath;
    std::string requestsPath;
    ActivityHook activityHook = nullptr;                              // NEW

    bool readFile(ParcelStore& into, bool verbose, int& loaded, int& skipped) const;
    bool reload(bool verbose);
    void sync() { reload(false); }
    bool commit();
    bool findActiveRequest(const std::string& parcelID, std::string& requestID,
        std::string& serviceType, std::string& requestStatus) const;
    void printDetails(const Parcel& p) const;

    // NEW: single helper used everywhere a state change needs reporting
    void publish(const std::string& parcelID, const std::string& type,
        const std::string& oldStatus, const std::string& newStatus,
        const std::string& requestID = "-",
        const std::string& resourceID = "-") const {
        publishActivity(activityHook, parcelID, type, oldStatus, newStatus,
            requestID, resourceID);
    }

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
    explicit ParcelModule(const std::string& parcelsFile = "parcels.csv",
        const std::string& requestsFile = "requests.csv");

    bool load();
    bool save() const;
    void run();

    // NEW: Main.cpp wires Task 4 in here
    void setActivityHook(ActivityHook h) { activityHook = h; }

    // ---- integration API ----
    bool registerParcel(const Parcel& input, std::string& newID, std::string& msg);
    bool findParcel(const std::string& parcelID, Parcel& out);
    int  waitingCount();
    bool peekNextParcel(Parcel& out);
    ParcelRoute suggestRoute(const Parcel& p, std::string& reason) const;
    bool processNextParcel(const std::string& expectedID, ParcelRoute route,
        Parcel& sorted, std::string& msg);
    bool sortNextParcel(Parcel& sorted);
    int  suggestedUrgency(const Parcel& p) const;

    // CHANGED: extra activity-type parameter, default keeps old calls working
    bool updateStatus(const std::string& parcelID, const std::string& newStatus,
        std::string& msg,
        const std::string& activityType = ACT_STATUS_UPDATED);

    bool updateDeliveryInfo(const std::string& parcelID, const std::string& newInfo, std::string& msg);
    bool reportMissing(const std::string& parcelID, std::string& msg);
    bool removeParcel(const std::string& parcelID, std::string& msg);

    // For Task 4 undo — deliberately does NOT publish (undo logs its own event)
    bool restoreParcelStatus(const std::string& parcelID, const std::string& previousStatus,
        std::string& msg);
};

#endif