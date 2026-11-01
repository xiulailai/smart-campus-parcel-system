#include "Task2/ServiceRequest.hpp"
#include "Task3/ResourceQueue.hpp"
#include <fstream>
#include <iostream>
#include <string>

// Looks for a data file in a few likely places and returns the first one that opens
std::string findDataFile(const std::string& fileName) {
    const char* folders[] = { "Data\\", "..\\Data\\", "..\\..\\Data\\", "..\\..\\..\\Data\\" };
    for (int i = 0; i < 4; i++) {
        std::string path = std::string(folders[i]) + fileName;
        std::ifstream test(path.c_str());
        if (test.is_open()) return path;
    }
    return fileName;   // not found: loadFromCSV will show the error
}

// ---------------------------------------------------------------------------
// Integrated flow (system steps 5-7).  Task 2 decides, Task 3 allocates:
//   1. Task 2 says which Pending request is the most urgent      (peekNext)
//   2. Task 2 checks the request can be assigned right now       (validateForAssignment)
//   3. Task 3 allocates a counter / delivery person              (assignRequest)
//   4. Task 2 marks the request Assigned and saves requests.csv  (markAssigned)
// If nobody is free in step 3 nothing changes: the request simply stays Pending.
// ---------------------------------------------------------------------------
void assignNextRequest(ServiceModule& service, ResourceManager& resources,
    const std::string& resourcesPath) {
    ServiceRequest r;
    std::string msg;
    if (!service.peekNext(r)) {
        std::cout << "[Info] No pending service requests.\n";
        return;
    }
    std::cout << "Next request: " << r.requestID << " (" << r.serviceType
        << ", urgency " << r.urgency << ", parcel " << r.parcelID << ")\n";

    if (!service.validateForAssignment(r.requestID, msg)) {
        std::cout << "[Error] " << msg << "\n";
        return;
    }
    if (!resources.assignRequest(r.requestID, r.serviceType)) {
        std::cout << "[Info] " << r.requestID << " stays Pending.\n";
        return;
    }
    resources.saveToCSV(resourcesPath);              // keep resources.csv in step

    if (service.markAssigned(r.requestID, msg))
        std::cout << "[OK] " << msg << "\n";
    else
        std::cout << "[Warning] Resource allocated but request not updated: " << msg << "\n";
}

int readMenuChoice() {
    std::string s;
    std::getline(std::cin, s);
    if (!std::cin) return 0;                         // end of input = exit
    if (s.size() == 1 && s[0] >= 0x30 && s[0] <= 0x39) return s[0] - 0x30;
    return -1;
}

int main() {
    std::string requestsPath = findDataFile("requests.csv");
    std::string parcelsPath = findDataFile("parcels.csv");
    std::string resourcesPath = findDataFile("resources.csv");

    ServiceModule service(requestsPath, parcelsPath);   // Task 2
    service.load();

    ResourceManager manager;                            // Task 3
    manager.loadFromCSV(resourcesPath);

    int choice;
    do {
        std::cout << "\n==============================================\n"
            << "  SMART CAMPUS PARCEL SYSTEM\n"
            << "==============================================\n"
            << "  2. Task 2 - Service requests\n"
            << "  3. Task 3 - Counters and delivery resources\n"
            << "  4. Assign a resource to the next request (Task 2 + Task 3)\n"
            << "  0. Exit\n"
            << "----------------------------------------------\n"
            << "Enter choice: ";
        choice = readMenuChoice();
        switch (choice) {
        case 2: service.run();                                   break;
        case 3: runTask3Menu(manager);                           break;
        case 4: assignNextRequest(service, manager, resourcesPath); break;
        case 0:                                                  break;
        default: std::cout << "Invalid choice.\n";               break;
        }
    } while (choice != 0);

    service.save();
    manager.saveToCSV(resourcesPath);
    return 0;
}