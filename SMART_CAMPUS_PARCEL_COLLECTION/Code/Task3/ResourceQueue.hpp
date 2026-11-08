#ifndef RESOURCEQUEUE_HPP
#define RESOURCEQUEUE_HPP

// ============================================================
// Task 3 - Collection Counter and Delivery Resource Management
// Data structure: Circular Queue (array based, written by us,
// no STL vector/list/stack/queue/priority_queue/deque)
// ============================================================

#include <string>

// Maximum number of resources each queue can hold
// (one queue for counters, one queue for delivery staff)
const int MAX_RESOURCES = 10;

// ---------- Resource data ----------

enum ResourceStatus { AVAILABLE, OCCUPIED, UNAVAILABLE };

struct Resource {
    std::string resourceID;         // C01 (counter) or D01 (delivery)
    std::string type;               // "Counter" or "Delivery"
    std::string name;               // e.g. "Counter 1" or "Ahmad Faiz"
    ResourceStatus status;          // AVAILABLE / OCCUPIED / UNAVAILABLE
    std::string currentAssignment;  // a Request ID, or "NONE" when free
};

// Convert between enum and text (used for CSV and display)
std::string statusToString(ResourceStatus status);
bool stringToStatus(const std::string& text, ResourceStatus& result);

// ---------- Circular queue ----------

class ResourceQueue {
private:
    Resource data[MAX_RESOURCES];
    int front;   // index of the first item
    int rear;    // index of the last item
    int count;   // number of items stored (used to tell full from empty)

public:
    ResourceQueue();

    bool isEmpty() const;
    bool isFull() const;
    int size() const;

    bool enqueue(const Resource& item);   // add at rear, wraps around
    bool dequeue(Resource& item);         // remove from front, wraps around

    // Look at an item without removing it. position 0 = front.
    // Returns nullptr if the position is invalid.
    Resource* at(int position);
    const Resource* at(int position) const;

    Resource* findByID(const std::string& id);
    int countByStatus(ResourceStatus status) const;
    void display() const;
};

// ---------- Resource manager (holds both queues) ----------

// Hooks let Main.cpp connect Task 3 to Task 2 without Task 3 knowing about Task 2
typedef void (*RequestLostHook)(const std::string& requestID);     // a busy resource lost its request
typedef bool (*BeforeReleaseHook)(const std::string& requestID);   // return false to stop the release

class ResourceManager {
private:
    ResourceQueue counterQueue;    // collection counters (C01, C02, ...)
    ResourceQueue deliveryQueue;   // delivery personnel (D01, D02, ...)
    RequestLostHook requestLostHook;
    BeforeReleaseHook beforeReleaseHook;

    ResourceQueue* queueForType(const std::string& type);

public:
    ResourceManager();
    void setRequestLostHook(RequestLostHook hook);
    void setBeforeReleaseHook(BeforeReleaseHook hook);

    // Requirement 1: register and maintain resources
    bool loadFromCSV(const std::string& filename);
    bool saveToCSV(const std::string& filename) const;
    bool registerResource(Resource r, bool verbose = true);

    // Requirement 2: display current availability
    void displayAll() const;

    // Requirement 3: assign a request to an available resource
    bool assignRequest(const std::string& requestID, const std::string& serviceType);
    // Same, but the staff member picks the resource (rejects Unavailable / Occupied ones)
    bool assignToResource(const std::string& requestID, const std::string& serviceType,
        const std::string& resourceID);

    // Requirement 4: update resource status
    bool releaseResource(const std::string& resourceID, std::string* releasedRequest = 0);
    bool updateResourceStatus(const std::string& resourceID, ResourceStatus newStatus);

    // Helpers other modules may need
    Resource* findResource(const std::string& resourceID);
    bool isRequestAssigned(const std::string& requestID);
    int resourceCount() const;               // counters + delivery
    Resource* resourceAt(int index);         // 0 .. resourceCount()-1
};

// Simple console menu for Task 3 (call this from the main menu)
void runTask3Menu(ResourceManager& manager);

#endif
