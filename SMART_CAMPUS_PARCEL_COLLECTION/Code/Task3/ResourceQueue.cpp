#include "ResourceQueue.hpp"
#include "../Task2/ServiceRequest.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstdlib>

namespace {

    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    std::string toUpper(std::string s) {
        for (size_t i = 0; i < s.size(); i++)
            s[i] = (char)std::toupper((unsigned char)s[i]);
        return s;
    }

    std::string normaliseType(const std::string& text) {
        std::string t = toUpper(trim(text));
        if (t == "COUNTER") return "Counter";
        if (t == "DELIVERY") return "Delivery";
        return "";
    }

    std::string serviceToResourceType(const std::string& serviceType) {
        std::string s = toUpper(trim(serviceType));
        if (s == "COLLECTION" || s == "SPECIALHANDLING" || s == "RETURN")
            return "Counter";
        if (s == "CAMPUSDELIVERY" || s == "REDELIVERY")
            return "Delivery";
        return "";
    }

    std::string readLine(const std::string& prompt) {
        std::string line;
        std::cout << prompt;
        std::getline(std::cin, line);
        return trim(line);
    }

    int readChoice(const std::string& prompt) {
        std::string line = readLine(prompt);
        if (line.empty()) return -1;
        for (size_t i = 0; i < line.size(); i++)
            if (!std::isdigit((unsigned char)line[i])) return -1;
        return std::atoi(line.c_str());
    }

}

std::string statusToString(ResourceStatus status) {
    switch (status) {
    case AVAILABLE:   return "Available";
    case OCCUPIED:    return "Occupied";
    case UNAVAILABLE: return "Unavailable";
    }
    return "Unknown";
}

bool stringToStatus(const std::string& text, ResourceStatus& result) {
    std::string t = toUpper(trim(text));
    if (t == "AVAILABLE") { result = AVAILABLE;   return true; }
    if (t == "OCCUPIED") { result = OCCUPIED;    return true; }
    if (t == "UNAVAILABLE") { result = UNAVAILABLE; return true; }
    return false;
}

// ============================================================================
//  ResourceQueue
// ============================================================================
ResourceQueue::ResourceQueue() {
    front = 0;
    rear = -1;
    count = 0;
}

bool ResourceQueue::isEmpty() const { return count == 0; }
bool ResourceQueue::isFull() const { return count == MAX_RESOURCES; }
int  ResourceQueue::size() const { return count; }

bool ResourceQueue::enqueue(const Resource& item) {
    if (isFull()) return false;
    rear = (rear + 1) % MAX_RESOURCES;
    data[rear] = item;
    count++;
    return true;
}

bool ResourceQueue::dequeue(Resource& item) {
    if (isEmpty()) return false;
    item = data[front];
    front = (front + 1) % MAX_RESOURCES;
    count--;
    return true;
}

Resource* ResourceQueue::at(int position) {
    if (position < 0 || position >= count) return 0;
    return &data[(front + position) % MAX_RESOURCES];
}

const Resource* ResourceQueue::at(int position) const {
    if (position < 0 || position >= count) return 0;
    return &data[(front + position) % MAX_RESOURCES];
}

Resource* ResourceQueue::findByID(const std::string& id) {
    for (int i = 0; i < count; i++) {
        Resource* r = at(i);
        if (r->resourceID == id) return r;
    }
    return 0;
}

int ResourceQueue::countByStatus(ResourceStatus status) const {
    int total = 0;
    for (int i = 0; i < count; i++)
        if (at(i)->status == status) total++;
    return total;
}

void ResourceQueue::display() const {
    if (isEmpty()) {
        std::cout << "  (no resources registered)\n";
        return;
    }
    for (int i = 0; i < count; i++) {
        const Resource* r = at(i);
        std::cout << "  " << std::left
            << std::setw(6) << r->resourceID
            << std::setw(16) << r->name
            << std::setw(13) << statusToString(r->status)
            << r->currentAssignment << "\n";
    }
}

// ============================================================================
//  ResourceManager
// ============================================================================
ResourceQueue* ResourceManager::queueForType(const std::string& type) {
    if (type == "Counter")  return &counterQueue;
    if (type == "Delivery") return &deliveryQueue;
    return 0;
}

ResourceManager::ResourceManager() {
    requestLostHook = 0;
    beforeReleaseHook = 0;
}

void ResourceManager::setRequestLostHook(RequestLostHook hook) { requestLostHook = hook; }
void ResourceManager::setBeforeReleaseHook(BeforeReleaseHook hook) { beforeReleaseHook = hook; }

int ResourceManager::resourceCount() const {
    return counterQueue.size() + deliveryQueue.size();
}

Resource* ResourceManager::resourceAt(int index) {
    if (index < 0) return 0;
    if (index < counterQueue.size()) return counterQueue.at(index);
    return deliveryQueue.at(index - counterQueue.size());
}

Resource* ResourceManager::findResource(const std::string& resourceID) {
    std::string id = toUpper(trim(resourceID));
    Resource* r = counterQueue.findByID(id);
    if (r != 0) return r;
    return deliveryQueue.findByID(id);
}

bool ResourceManager::isRequestAssigned(const std::string& requestID) {
    std::string id = toUpper(trim(requestID));
    for (int i = 0; i < counterQueue.size(); i++)
        if (counterQueue.at(i)->currentAssignment == id) return true;
    for (int i = 0; i < deliveryQueue.size(); i++)
        if (deliveryQueue.at(i)->currentAssignment == id) return true;
    return false;
}

bool ResourceManager::registerResource(Resource r, bool verbose) {
    r.resourceID = toUpper(trim(r.resourceID));
    r.name = trim(r.name);
    r.type = normaliseType(r.type);
    r.currentAssignment = toUpper(trim(r.currentAssignment));
    if (r.currentAssignment.empty()) r.currentAssignment = "NONE";

    if (r.resourceID.empty() || r.name.empty()) {
        std::cout << "Error: resource ID and name cannot be empty.\n";
        return false;
    }
    if (r.type.empty()) {
        std::cout << "Error: type must be Counter or Delivery.\n";
        return false;
    }
    if (r.name.find(',') != std::string::npos) {
        std::cout << "Error: name cannot contain a comma.\n";
        return false;
    }
    char expectedPrefix = (r.type == "Counter") ? 'C' : 'D';
    if (r.resourceID.size() < 2 || r.resourceID[0] != expectedPrefix) {
        std::cout << "Error: " << r.type << " IDs must start with '"
            << expectedPrefix << "' (e.g. " << expectedPrefix << "01).\n";
        return false;
    }

    if (findResource(r.resourceID) != 0) {
        std::cout << "Error: resource ID " << r.resourceID << " already exists.\n";
        return false;
    }

    if (r.status == OCCUPIED) {
        if (r.currentAssignment == "NONE") {
            std::cout << "Error: an Occupied resource needs an assignment.\n";
            return false;
        }
        if (isRequestAssigned(r.currentAssignment)) {
            std::cout << "Error: " << r.currentAssignment
                << " is already assigned to another resource.\n";
            return false;
        }
    }
    else if (r.currentAssignment != "NONE") {
        std::cout << "Error: only an Occupied resource can have an assignment.\n";
        return false;
    }

    ResourceQueue* queue = queueForType(r.type);
    if (!queue->enqueue(r)) {
        std::cout << "Error: the " << r.type << " queue is full ("
            << MAX_RESOURCES << " resources maximum).\n";
        return false;
    }
    if (verbose)
        std::cout << "Resource " << r.resourceID << " registered.\n";
    return true;
}

bool ResourceManager::loadFromCSV(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        std::cout << "Error: cannot open " << filename << "\n";
        return false;
    }

    std::string line;
    int lineNumber = 0, loaded = 0, skipped = 0;

    while (std::getline(file, line)) {
        lineNumber++;
        if (lineNumber == 1) continue;
        if (trim(line).empty()) continue;

        std::string fields[6];
        int fieldCount = 0;
        std::stringstream ss(line);
        std::string part;
        while (fieldCount < 6 && std::getline(ss, part, ','))
            fields[fieldCount++] = part;

        ResourceStatus status;
        if (fieldCount != 5 || !stringToStatus(fields[3], status)) {
            std::cout << "Line " << lineNumber << " skipped (invalid format).\n";
            skipped++;
            continue;
        }

        Resource r;
        r.resourceID = fields[0];
        r.type = fields[1];
        r.name = fields[2];
        r.status = status;
        r.currentAssignment = fields[4];

        if (registerResource(r, false)) loaded++;
        else { std::cout << "  (line " << lineNumber << " skipped)\n"; skipped++; }
    }

    std::cout << "Loaded " << loaded << " resources from " << filename;
    if (skipped > 0) std::cout << " (" << skipped << " skipped)";
    std::cout << ".\n";
    return true;
}

bool ResourceManager::saveToCSV(const std::string& filename) const {
    std::ofstream file(filename.c_str());
    if (!file.is_open()) {
        std::cout << "Error: cannot write to " << filename << "\n";
        return false;
    }
    file << "ResourceID,Type,Name,Status,CurrentAssignment\n";
    const ResourceQueue* queues[2] = { &counterQueue, &deliveryQueue };
    for (int q = 0; q < 2; q++) {
        for (int i = 0; i < queues[q]->size(); i++) {
            const Resource* r = queues[q]->at(i);
            file << r->resourceID << "," << r->type << "," << r->name << ","
                << statusToString(r->status) << "," << r->currentAssignment << "\n";
        }
    }
    return true;
}

void ResourceManager::displayAll() const {
    std::cout << "\n=== Collection Counters ===\n";
    std::cout << "  " << std::left << std::setw(6) << "ID" << std::setw(16) << "Name"
        << std::setw(13) << "Status" << "Assignment\n";
    counterQueue.display();
    std::cout << "  Available: " << counterQueue.countByStatus(AVAILABLE)
        << " | Occupied: " << counterQueue.countByStatus(OCCUPIED)
        << " | Unavailable: " << counterQueue.countByStatus(UNAVAILABLE) << "\n";

    std::cout << "\n=== Delivery Personnel ===\n";
    std::cout << "  " << std::left << std::setw(6) << "ID" << std::setw(16) << "Name"
        << std::setw(13) << "Status" << "Assignment\n";
    deliveryQueue.display();
    std::cout << "  Available: " << deliveryQueue.countByStatus(AVAILABLE)
        << " | Occupied: " << deliveryQueue.countByStatus(OCCUPIED)
        << " | Unavailable: " << deliveryQueue.countByStatus(UNAVAILABLE) << "\n";
}

// CHANGED: fills *assignedResourceID with the resource that took the request
bool ResourceManager::assignRequest(const std::string& requestIDInput,
    const std::string& serviceType,
    std::string* assignedResourceID) {
    std::string requestID = toUpper(trim(requestIDInput));

    if (requestID.empty() || requestID == "NONE") {
        std::cout << "Error: invalid Request ID.\n";
        return false;
    }
    std::string neededType = serviceToResourceType(serviceType);
    if (neededType.empty()) {
        std::cout << "Error: unknown service type '" << serviceType << "'.\n";
        return false;
    }
    if (isRequestAssigned(requestID)) {
        std::cout << "Error: " << requestID << " is already assigned to a resource.\n";
        return false;
    }

    ResourceQueue* queue = queueForType(neededType);
    if (queue->isEmpty()) {
        std::cout << "Error: no " << neededType << " resources are registered.\n";
        return false;
    }

    int total = queue->size();
    int occupied = 0, unavailable = 0;

    for (int i = 0; i < total; i++) {
        Resource r;
        queue->dequeue(r);

        if (r.status == AVAILABLE) {
            r.status = OCCUPIED;
            r.currentAssignment = requestID;
            if (assignedResourceID) *assignedResourceID = r.resourceID;   // NEW
            queue->enqueue(r);
            std::cout << "Request " << requestID << " assigned to "
                << r.resourceID << " (" << r.name << ").\n";
            return true;
        }

        if (r.status == OCCUPIED) occupied++;
        else unavailable++;
        queue->enqueue(r);
    }

    if (occupied == total)
        std::cout << "All " << total << " " << neededType
        << " resources are occupied. Please try again later.\n";
    else if (unavailable == total)
        std::cout << "All " << total << " " << neededType
        << " resources are unavailable.\n";
    else
        std::cout << "No suitable " << neededType << " resource available ("
        << occupied << " occupied, " << unavailable << " unavailable).\n";
    return false;
}

// CHANGED: fills *normalisedID with the resource ID that was picked
bool ResourceManager::assignToResource(const std::string& requestIDInput,
    const std::string& serviceType,
    const std::string& resourceIDInput,
    std::string* normalisedID) {
    std::string requestID = toUpper(trim(requestIDInput));

    if (requestID.empty() || requestID == "NONE") {
        std::cout << "Error: invalid Request ID.\n";
        return false;
    }
    std::string neededType = serviceToResourceType(serviceType);
    if (neededType.empty()) {
        std::cout << "Error: unknown service type '" << serviceType << "'.\n";
        return false;
    }
    if (isRequestAssigned(requestID)) {
        std::cout << "Error: " << requestID << " is already assigned to a resource.\n";
        return false;
    }

    Resource* r = findResource(resourceIDInput);
    if (r == 0) {
        std::cout << "Error: resource ID '" << trim(resourceIDInput) << "' not found.\n";
        return false;
    }
    if (r->type != neededType) {
        std::cout << "Error: " << r->resourceID << " is a " << r->type
            << " but this request needs a " << neededType << ".\n";
        return false;
    }
    if (r->status == UNAVAILABLE) {
        std::cout << "Error: " << r->resourceID << " is unavailable and cannot be selected.\n";
        return false;
    }
    if (r->status == OCCUPIED) {
        std::cout << "Error: " << r->resourceID << " is already occupied with "
            << r->currentAssignment << ".\n";
        return false;
    }

    r->status = OCCUPIED;
    r->currentAssignment = requestID;
    if (normalisedID) *normalisedID = r->resourceID;                     // NEW
    std::cout << "Request " << requestID << " assigned to "
        << r->resourceID << " (" << r->name << ").\n";
    return true;
}

bool ResourceManager::releaseResource(const std::string& resourceID,
    std::string* releasedRequest) {
    Resource* r = findResource(resourceID);
    if (r == 0) {
        std::cout << "Error: resource ID '" << trim(resourceID) << "' not found.\n";
        return false;
    }
    if (r->status != OCCUPIED) {
        std::cout << "Error: " << r->resourceID << " is not occupied, nothing to release.\n";
        return false;
    }

    std::string finishedRequest = r->currentAssignment;

    if (beforeReleaseHook != 0 && !beforeReleaseHook(finishedRequest)) {
        std::cout << r->resourceID << " not released: " << finishedRequest
            << " is not completed.\n";
        return false;
    }

    if (releasedRequest != 0) *releasedRequest = finishedRequest;
    std::cout << r->resourceID << " released from " << finishedRequest << ".\n";
    r->status = AVAILABLE;
    r->currentAssignment = "NONE";
    return true;
}

bool ResourceManager::updateResourceStatus(const std::string& resourceID,
    ResourceStatus newStatus) {
    Resource* r = findResource(resourceID);
    if (r == 0) {
        std::cout << "Error: resource ID '" << trim(resourceID) << "' not found.\n";
        return false;
    }
    if (r->status == newStatus) {
        std::cout << "Error: " << r->resourceID << " is already "
            << statusToString(newStatus) << ".\n";
        return false;
    }
    if (newStatus == OCCUPIED) {
        std::cout << "Error: a resource becomes Occupied only by assigning a request to it.\n";
        return false;
    }

    std::string lostRequest;
    if (r->status == OCCUPIED) {
        lostRequest = r->currentAssignment;
        r->currentAssignment = "NONE";
        std::cout << "Warning: " << r->resourceID << " was handling " << lostRequest
            << ". That request needs a new resource.\n";
    }

    r->status = newStatus;
    std::cout << r->resourceID << " is now " << statusToString(newStatus) << ".\n";

    if (!lostRequest.empty() && requestLostHook != 0)
        requestLostHook(lostRequest);
    return true;
}

void runTask3Menu(ResourceManager& manager, ServiceModule* service) {
    int choice = -1;
    while (choice != 0) {
        std::cout << "\n===== Task 3: Resource Management =====\n"
            << "1. Display all resources\n"
            << "2. Register a new resource\n"
            << "3. Assign a request to a resource\n"
            << "4. Release a resource\n"
            << "5. Update resource status\n"
            << "6. Save resources to file\n"
            << "7. Assign a request to a chosen resource\n"
            << "0. Back\n";
        choice = readChoice("Choice: ");

        if (choice == 1) {
            manager.displayAll();
        }
        else if (choice == 2) {
            Resource r;
            r.resourceID = readLine("Resource ID (e.g. C05 or D06): ");
            r.type = readLine("Type (Counter / Delivery): ");
            r.name = readLine("Name: ");
            std::string statusText = readLine("Status (Available / Unavailable): ");
            ResourceStatus status;
            if (!stringToStatus(statusText, status) || status == OCCUPIED) {
                std::cout << "Error: status must be Available or Unavailable.\n";
            }
            else {
                r.status = status;
                r.currentAssignment = "NONE";
                manager.registerResource(r);
            }
        }
        else if (choice == 3) {
            // ---- NEW: capture the resource ID, then complete the request ----
            std::string requestID = readLine("Request ID: ");
            std::string serviceType = readLine(
                "Service type (Collection / SpecialHandling / Return / CampusDelivery / ReDelivery): ");
            std::string assignedResourceID;
            if (manager.assignRequest(requestID, serviceType, &assignedResourceID)) {
                if (service != nullptr) {
                    std::string msg;
                    if (service->markAssigned(requestID, assignedResourceID, msg))
                        std::cout << "[Task 2] " << msg << "\n";
                    else
                        std::cout << "[Warning] " << msg << "\n";
                }
            }
        }
        else if (choice == 4) {
            manager.releaseResource(readLine("Resource ID to release: "));
        }
        else if (choice == 5) {
            std::string id = readLine("Resource ID: ");
            std::string statusText = readLine("New status (Available / Unavailable): ");
            ResourceStatus status;
            if (!stringToStatus(statusText, status)) {
                std::cout << "Error: invalid status.\n";
            }
            else {
                manager.updateResourceStatus(id, status);
            }
        }
        else if (choice == 6) {
            std::string path = readLine("Save to file (e.g. ..\\Data\\resources_saved.csv): ");
            if (manager.saveToCSV(path)) std::cout << "Saved.\n";
        }
        else if (choice == 7) {
            // ---- NEW: same, but the resource is chosen by the user ----
            std::string requestID = readLine("Request ID: ");
            std::string serviceType = readLine(
                "Service type (Collection / SpecialHandling / Return / CampusDelivery / ReDelivery): ");
            std::string resourceID = readLine("Resource ID to use (e.g. C01 or D02): ");
            std::string assignedResourceID;
            if (manager.assignToResource(requestID, serviceType, resourceID, &assignedResourceID)) {
                if (service != nullptr) {
                    std::string msg;
                    if (service->markAssigned(requestID, assignedResourceID, msg))
                        std::cout << "[Task 2] " << msg << "\n";
                    else
                        std::cout << "[Warning] " << msg << "\n";
                }
            }
        }
        else if (choice != 0) {
            std::cout << "Invalid choice, please try again.\n";
        }
    }
}