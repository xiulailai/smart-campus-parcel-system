#include "ResourceQueue.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstdlib>

// Task 3 - Collection Counter and Delivery Resource Management
// Big picture: the program keeps two circular queues. One holds the
// collection counters (C01, C02...) and the other holds the delivery
// staff (D01, D02...). Think of each queue as a line of workers. When a
// request comes in, we go down the line until we find a free worker.
// A worker who just took a job goes to the back, so everyone gets a turn.

// Small helper functions (only used inside this file)
namespace {

    // trim: cuts spaces, tabs and line endings off both ends of a string.
    // We need it because CSV lines and keyboard input often have extra
    // spaces (or a hidden '\r' from Windows), and "C01 " would not match "C01".
    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");   // first real character
        if (start == std::string::npos) return "";        // string was all spaces
        size_t end = s.find_last_not_of(" \t\r\n");       // last real character
        return s.substr(start, end - start + 1);
    }

    // toUpper: turns every letter into a capital, so "c01" and "C01" count
    // as the same ID. Without this the user would have to type exact case.
    std::string toUpper(std::string s) {
        for (size_t i = 0; i < s.size(); i++)
            s[i] = (char)std::toupper((unsigned char)s[i]);
        return s;
    }

    // normaliseType: accepts "counter", "COUNTER", "Counter" and returns the
    // one clean spelling we use everywhere. Returns "" if the type is invalid.
    std::string normaliseType(const std::string& text) {
        std::string t = toUpper(trim(text));
        if (t == "COUNTER") return "Counter";
        if (t == "DELIVERY") return "Delivery";
        return "";
    }

    // serviceToResourceType: our allocation rule. A request needs either a
    // counter or a delivery person, depending on its service type.
    // Collection, special handling and return happen at a counter, while
    // campus delivery and re-delivery need someone to walk the parcel over.
    // Agree this rule with the Task 2 member.
    std::string serviceToResourceType(const std::string& serviceType) {
        std::string s = toUpper(trim(serviceType));
        if (s == "COLLECTION" || s == "SPECIALHANDLING" || s == "RETURN")
            return "Counter";
        if (s == "CAMPUSDELIVERY" || s == "REDELIVERY")
            return "Delivery";
        return "";   // unknown service type
    }

    // readLine: shows a prompt and reads a whole line. We use getline instead
    // of cin >> so names with spaces (like "Lim Boon Hock") work.
    std::string readLine(const std::string& prompt) {
        std::string line;
        std::cout << prompt;
        std::getline(std::cin, line);
        return trim(line);
    }

    // readChoice: reads a menu number safely. If the user types letters or
    // nothing, it returns -1 instead of crashing, and the menu says "invalid".
    int readChoice(const std::string& prompt) {
        std::string line = readLine(prompt);
        if (line.empty()) return -1;
        for (size_t i = 0; i < line.size(); i++)
            if (!std::isdigit((unsigned char)line[i])) return -1;   // not all digits
        return std::atoi(line.c_str());
    }

} // end of helper namespace

// Status conversion
// statusToString: enum -> text. The enum is a number inside the program,
// but people (and the CSV file) need to read "Available", not 0.
std::string statusToString(ResourceStatus status) {
    switch (status) {
    case AVAILABLE:   return "Available";
    case OCCUPIED:    return "Occupied";
    case UNAVAILABLE: return "Unavailable";
    }
    return "Unknown";
}

// stringToStatus: text -> enum, the opposite direction. Returns false when
// the text is not a real status, so the caller can reject bad CSV lines or
// bad keyboard input. The result goes back through the 'result' reference.
bool stringToStatus(const std::string& text, ResourceStatus& result) {
    std::string t = toUpper(trim(text));
    if (t == "AVAILABLE") { result = AVAILABLE;   return true; }
    if (t == "OCCUPIED") { result = OCCUPIED;    return true; }
    if (t == "UNAVAILABLE") { result = UNAVAILABLE; return true; }
    return false;
}

// ResourceQueue : circular queue
//
// Picture a roundabout with 10 exits (the array slots). 'front' marks where
// the next person leaves and 'rear' marks where the last person joined.
// When an index reaches the end of the array, the % operator sends it back
// to 0, like a clock going from 12 back to 1. That is what "circular"
// means here: freed slots at the start get reused instead of wasted.



// Constructor: starts the queue empty. rear is -1 so that the first enqueue
// (rear + 1) lands on slot 0.
ResourceQueue::ResourceQueue() {
    front = 0;
    rear = -1;
    count = 0;
}

// isEmpty / isFull use 'count'. If we only compared front and rear, a full
// queue and an empty queue would look the same, so we keep a separate count.
bool ResourceQueue::isEmpty() const { return count == 0; }
bool ResourceQueue::isFull() const { return count == MAX_RESOURCES; }
int  ResourceQueue::size() const { return count; }

// enqueue: adds an item at the rear. O(1), because we jump straight to the
// next slot without moving anything else. Returns false if the queue is full.
bool ResourceQueue::enqueue(const Resource& item) {
    if (isFull()) return false;                 // nowhere to put it
    rear = (rear + 1) % MAX_RESOURCES;          // move rear one step, wrap at the end
    data[rear] = item;                          // store a copy
    count++;
    return true;
}

// dequeue: removes the item at the front and gives it back through 'item'.
// O(1) as well. Returns false if the queue is empty.
bool ResourceQueue::dequeue(Resource& item) {
    if (isEmpty()) return false;                // nothing to take
    item = data[front];                         // copy the front item out
    front = (front + 1) % MAX_RESOURCES;        // move front one step, wrap at the end
    count--;
    return true;
}

// at: peeks at the item 'position' steps from the front (0 = front) without
// removing it. The (front + position) % MAX part handles the wrap-around.
// Returns 0 (null) for a bad position. We have a normal version, which lets
// the caller change the item, and a const version for read-only code.
Resource* ResourceQueue::at(int position) {
    if (position < 0 || position >= count) return 0;
    return &data[(front + position) % MAX_RESOURCES];
}

const Resource* ResourceQueue::at(int position) const {
    if (position < 0 || position >= count) return 0;
    return &data[(front + position) % MAX_RESOURCES];
}

// findByID: walks from front to rear looking for a matching ID. This is a
// plain linear search, so it is O(n). With at most 10 resources per queue
// that is fine. Nothing is removed, so the queue order stays the same.
Resource* ResourceQueue::findByID(const std::string& id) {
    for (int i = 0; i < count; i++) {
        Resource* r = at(i);
        if (r->resourceID == id) return r;
    }
    return 0;   // not found
}

// countByStatus: counts how many resources have a given status. Used for
// the summary line under each table in the display.
int ResourceQueue::countByStatus(ResourceStatus status) const {
    int total = 0;
    for (int i = 0; i < count; i++)
        if (at(i)->status == status) total++;
    return total;
}

// display: prints one row per resource, front to back. setw gives each
// column a fixed width so the rows line up like a table.
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

// ------------------------------------------------------------
// ResourceManager
// The manager owns both queues and decides which one to use.
// ------------------------------------------------------------

// queueForType: gives back the right queue for "Counter" or "Delivery".
// Returns 0 (null) for anything else.
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

// resourceCount / resourceAt: let other modules walk through every resource
// (counters first, then delivery staff) without touching the queues.
int ResourceManager::resourceCount() const {
    return counterQueue.size() + deliveryQueue.size();
}

Resource* ResourceManager::resourceAt(int index) {
    if (index < 0) return 0;
    if (index < counterQueue.size()) return counterQueue.at(index);
    return deliveryQueue.at(index - counterQueue.size());   // at() returns 0 if out of range
}

// findResource: looks for an ID in the counter queue first, then in the
// delivery queue. Returns a pointer to the real record, so the caller can
// update it directly, or 0 if the ID does not exist anywhere.
Resource* ResourceManager::findResource(const std::string& resourceID) {
    std::string id = toUpper(trim(resourceID));
    Resource* r = counterQueue.findByID(id);
    if (r != 0) return r;
    return deliveryQueue.findByID(id);
}

// isRequestAssigned: checks if a request is already sitting on any resource,
// in either queue. This stops one request from taking two resources
// (the "duplicate active assignment" case in the brief).
bool ResourceManager::isRequestAssigned(const std::string& requestID) {
    std::string id = toUpper(trim(requestID));
    for (int i = 0; i < counterQueue.size(); i++)
        if (counterQueue.at(i)->currentAssignment == id) return true;
    for (int i = 0; i < deliveryQueue.size(); i++)
        if (deliveryQueue.at(i)->currentAssignment == id) return true;
    return false;
}

// Requirement 1: register and maintain

// registerResource: adds a new resource after checking everything that could
// be wrong with it. It cleans the input first (trim, capitals), then
// rejects the resource at the first problem, with a message. Only if all
// checks pass does it enqueue the resource into the correct queue.
// 'verbose' is false when loading the CSV, so we do not print 9 "registered" lines at startup.
bool ResourceManager::registerResource(Resource r, bool verbose) {
    // Step 1: clean the input
    r.resourceID = toUpper(trim(r.resourceID));
    r.name = trim(r.name);
    r.type = normaliseType(r.type);
    r.currentAssignment = toUpper(trim(r.currentAssignment));
    if (r.currentAssignment.empty()) r.currentAssignment = "NONE";

    // Step 2: incomplete information
    if (r.resourceID.empty() || r.name.empty()) {
        std::cout << "Error: resource ID and name cannot be empty.\n";
        return false;
    }
    if (r.type.empty()) {
        std::cout << "Error: type must be Counter or Delivery.\n";
        return false;
    }
    // A comma in the name would split the field when we save to CSV
    if (r.name.find(',') != std::string::npos) {
        std::cout << "Error: name cannot contain a comma.\n";
        return false;
    }
    // Counters must start with C and delivery staff with D, so the ID
    // always matches the type
    char expectedPrefix = (r.type == "Counter") ? 'C' : 'D';
    if (r.resourceID.size() < 2 || r.resourceID[0] != expectedPrefix) {
        std::cout << "Error: " << r.type << " IDs must start with '"
            << expectedPrefix << "' (e.g. " << expectedPrefix << "01).\n";
        return false;
    }

    // Step 3: duplicate ID (checks both queues)
    if (findResource(r.resourceID) != 0) {
        std::cout << "Error: resource ID " << r.resourceID << " already exists.\n";
        return false;
    }

    // Step 4: status and assignment must agree. An Occupied resource needs a
    // request on it, while a free or unavailable one must have NONE.
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

    // Step 5: add it to the right queue. This only fails if the queue is full.
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

// loadFromCSV: reads the resource file at program start.
// Each line looks like: ResourceID,Type,Name,Status,CurrentAssignment
// Every line goes through registerResource, so bad lines (duplicate ID,
// wrong status, wrong number of fields) are skipped with a message instead
// of breaking the whole load.
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
        if (lineNumber == 1) continue;       // first line is the header
        if (trim(line).empty()) continue;    // ignore blank lines

        // Split the line at each comma into up to 6 pieces. We allow one
        // more than the 5 we expect, so a line with too many fields is caught.
        std::string fields[6];
        int fieldCount = 0;
        std::stringstream ss(line);
        std::string part;
        while (fieldCount < 6 && std::getline(ss, part, ','))
            fields[fieldCount++] = part;

        // Must have exactly 5 fields and a valid status word
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

        // registerResource does the rest of the validation
        if (registerResource(r, false)) loaded++;
        else { std::cout << "  (line " << lineNumber << " skipped)\n"; skipped++; }
    }

    std::cout << "Loaded " << loaded << " resources from " << filename;
    if (skipped > 0) std::cout << " (" << skipped << " skipped)";
    std::cout << ".\n";
    return true;
}

// saveToCSV: writes the current resources back to a file in the same format we load from. This keeps the changes (assignments, status updates) after
// the program closes. It loops over both queues front to back without removing anything, so the queues are not changed.
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

// Requirement 2: display availability

// displayAll: shows both queues as tables, with a count of available,
// occupied and unavailable resources under each one. The Assignment column
// shows which request a busy resource is working on, or NONE.
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

// Requirement 3: assign a request

// assignRequest: the main algorithm of this task.
//
// Think of a line of workers. We call the first worker forward. If free, they take the job. Either way they walk to the back of the 
// line, so the next job starts with someone else. That is round-robin: work is shared out fairly instead of always hitting C01.
//
// We check each worker at most once, so after one full loop we stop. That way the loop can never run forever 
// when everyone is busy. Time: O(n), where n is the number of resources in that queue (10 at most).
bool ResourceManager::assignRequest(const std::string& requestIDInput,
    const std::string& serviceType) {
    std::string requestID = toUpper(trim(requestIDInput));

    // Check 1: the request ID makes sense
    if (requestID.empty() || requestID == "NONE") {
        std::cout << "Error: invalid Request ID.\n";
        return false;
    }
    // Check 2: we know which kind of resource this service needs
    std::string neededType = serviceToResourceType(serviceType);
    if (neededType.empty()) {
        std::cout << "Error: unknown service type '" << serviceType << "'.\n";
        return false;
    }
    // Check 3: the request is not already on another resource
    if (isRequestAssigned(requestID)) {
        std::cout << "Error: " << requestID << " is already assigned to a resource.\n";
        return false;
    }

    // Pick the counter queue or the delivery queue
    ResourceQueue* queue = queueForType(neededType);
    if (queue->isEmpty()) {
        std::cout << "Error: no " << neededType << " resources are registered.\n";
        return false;
    }

    int total = queue->size();      // how many we will check (one full loop)
    int occupied = 0, unavailable = 0;   // counted so the failure message is clear

    for (int i = 0; i < total; i++) {
        Resource r;
        queue->dequeue(r);          // take the worker at the front

        if (r.status == AVAILABLE) {
            // Free: give them the job and send them to the back
            r.status = OCCUPIED;
            r.currentAssignment = requestID;
            queue->enqueue(r);
            std::cout << "Request " << requestID << " assigned to "
                << r.resourceID << " (" << r.name << ").\n";
            // TODO (integration):
            //   Task 2: set the request status to "Assigned"
            //   Task 1: update the parcel status if needed
            //   Task 4: log this activity
            return true;
        }

        // Not free: remember why, then put them back at the rear unchanged
        if (r.status == OCCUPIED) occupied++;
        else unavailable++;
        queue->enqueue(r);
    }

    // We went through everyone once and nobody was free. Say why.
    // Because each checked worker went to the back in order, the queue is
    // back in its original order now.
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

// assignToResource: the staff member chooses the resource. Every check that
// assignRequest does still applies, plus the chosen resource must exist, be the
// right type, and be Available. This is where "selection of an unavailable
// resource" is rejected.
bool ResourceManager::assignToResource(const std::string& requestIDInput,
    const std::string& serviceType,
    const std::string& resourceIDInput) {
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
    std::cout << "Request " << requestID << " assigned to "
        << r->resourceID << " (" << r->name << ").\n";
    return true;
}

// Requirement 4: update status

// releaseResource: frees a resource when its job is done. It only works on
// an Occupied resource, because releasing a free one makes no sense. The
// finished request ID can be passed back through 'releasedRequest', so Task 2
// knows which request to mark Completed.
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

    // Ask Task 2 to complete the request first. If it refuses (for example a wrong
    // collector ID), the resource stays Occupied so both modules stay consistent.
    if (beforeReleaseHook != 0 && !beforeReleaseHook(finishedRequest)) {
        std::cout << r->resourceID << " not released: " << finishedRequest
            << " is not completed.\n";
        return false;
    }

    if (releasedRequest != 0) *releasedRequest = finishedRequest;
    std::cout << r->resourceID << " released from " << finishedRequest << ".\n";
    r->status = AVAILABLE;
    r->currentAssignment = "NONE";   // status and assignment always change together
    return true;
}

// updateResourceStatus: changes a resource to Available or Unavailable.

// Rules that keep the data consistent:
//  1. The ID must exist, and the new status must be different from the old one.
//  2. Occupied cannot be set by hand. It only happens through assignRequest, so an Occupied resource always has a real request on it.
//  3. If a busy resource changes status, its assignment is cleared. If it went Unavailable, the request 
//     on it lost its worker, so we warn the request needs a new resource (the "unavailable during processing" case in the brief).
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

    // A busy resource is changing status, so it no longer holds its request
    std::string lostRequest;
    if (r->status == OCCUPIED) {
        lostRequest = r->currentAssignment;
        r->currentAssignment = "NONE";
        std::cout << "Warning: " << r->resourceID << " was handling " << lostRequest
            << ". That request needs a new resource.\n";
    }

    r->status = newStatus;
    std::cout << r->resourceID << " is now " << statusToString(newStatus) << ".\n";

    // Tell Task 2 (through the hook) so the request goes back to Pending
    if (!lostRequest.empty() && requestLostHook != 0)
        requestLostHook(lostRequest);
    return true;
}

// Console menu for Task 3

// runTask3Menu: shows the menu again and again until the user picks 0.
// Each option reads the input it needs and hands it to one manager
// function. The menu itself does very little checking, because the manager
// functions already validate everything.
void runTask3Menu(ResourceManager& manager) {
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
            // New resources can only start as Available or Unavailable.
            // Occupied needs a request, which only assignRequest can give.
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
            std::string requestID = readLine("Request ID: ");
            std::string service = readLine("Service type (Collection / SpecialHandling / Return / CampusDelivery / ReDelivery): ");
            manager.assignRequest(requestID, service);
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
            std::string requestID = readLine("Request ID: ");
            std::string service = readLine("Service type (Collection / SpecialHandling / Return / CampusDelivery / ReDelivery): ");
            std::string resourceID = readLine("Resource ID to use (e.g. C01 or D02): ");
            manager.assignToResource(requestID, service, resourceID);
        }
        else if (choice != 0) {
            std::cout << "Invalid choice, please try again.\n";
        }
    }
}
