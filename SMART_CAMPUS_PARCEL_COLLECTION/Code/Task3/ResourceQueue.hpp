#ifndef RESOURCEQUEUE_HPP
#define RESOURCEQUEUE_HPP

// ============================================================
// Task 3 - Collection Counter and Delivery Resource Management
// ============================================================
#include <string>

const int MAX_RESOURCES = 10;

enum ResourceStatus { AVAILABLE, OCCUPIED, UNAVAILABLE };

struct Resource {
    std::string resourceID;
    std::string type;
    std::string name;
    ResourceStatus status;
    std::string currentAssignment;
};

std::string statusToString(ResourceStatus status);
bool stringToStatus(const std::string& text, ResourceStatus& result);

class ResourceQueue {
    // ... unchanged ...
private:
    Resource data[MAX_RESOURCES];
    int front;
    int rear;
    int count;

public:
    ResourceQueue();
    bool isEmpty() const;
    bool isFull() const;
    int size() const;
    bool enqueue(const Resource& item);
    bool dequeue(Resource& item);
    Resource* at(int position);
    const Resource* at(int position) const;
    Resource* findByID(const std::string& id);
    int countByStatus(ResourceStatus status) const;
    void display() const;
};

typedef void (*RequestLostHook)(const std::string& requestID);
typedef bool (*BeforeReleaseHook)(const std::string& requestID);

class ResourceManager {
    // ... unchanged ...
private:
    ResourceQueue counterQueue;
    ResourceQueue deliveryQueue;
    RequestLostHook requestLostHook;
    BeforeReleaseHook beforeReleaseHook;

    ResourceQueue* queueForType(const std::string& type);

public:
    ResourceManager();
    void setRequestLostHook(RequestLostHook hook);
    void setBeforeReleaseHook(BeforeReleaseHook hook);

    bool loadFromCSV(const std::string& filename);
    bool saveToCSV(const std::string& filename) const;
    bool registerResource(Resource r, bool verbose = true);
    void displayAll() const;

    bool assignRequest(const std::string& requestID, const std::string& serviceType,
        std::string* assignedResourceID = nullptr);
    bool assignToResource(const std::string& requestID, const std::string& serviceType,
        const std::string& resourceID,
        std::string* normalisedID = nullptr);

    bool releaseResource(const std::string& resourceID, std::string* releasedRequest = 0);
    bool updateResourceStatus(const std::string& resourceID, ResourceStatus newStatus);

    Resource* findResource(const std::string& resourceID);
    bool isRequestAssigned(const std::string& requestID);
    int resourceCount() const;
    Resource* resourceAt(int index);
};

// ---------------------------------------------------- NEW: forward decl
class ServiceModule;

// NEW: pass the Task 2 module so the menu can call markAssigned() after a
// successful assignment. Default nullptr keeps the old signature callable.
void runTask3Menu(ResourceManager& manager, ServiceModule* service = nullptr);

#endif