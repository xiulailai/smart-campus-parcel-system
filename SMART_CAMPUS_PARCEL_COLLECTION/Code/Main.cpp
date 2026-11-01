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
// Integrated flow (system steps 5-7):
//   Task 2 picks the next request  ->  Task 3 assigns a counter / delivery person.
// If Task 3 has nobody free, the request goes back to Pending so nothing is lost.
// ---------------------------------------------------------------------------
void dispatchAndAssign(ServiceModule& service, ResourceManager& resources,
    const std::string& resourcesPath) {
    ServiceRequest r;
    std::string msg;
    if (!service.dispatchNext(r, msg)) {            // Pending -> Assigned
        std::cout << "[Error] " << msg << "\n";
        return;
    }
    std::cout << "Dispatching " << r.requestID << " (" << r.serviceType
        << ", parcel " << r.parcelID << ")...\n";

    if (resources.assignRequest(r.requestID, r.serviceType)) {
        resources.saveToCSV(resourcesPath);          // keep resources.csv in step
        std::cout << "[OK] " << r.requestID << " is now Assigned to a resource.\n";
    }
    else {
        std::string undoMsg;                         // nobody free: back to the queue
        service.restoreRequestStatus(r.requestID, STATUS_PENDING, undoMsg);
        std::cout << "[Info] No resource available. " << r.requestID
            << " returned to the pending queue.\n";
    }
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
            << "  4. Integrated: dispatch next request AND assign a resource\n"
            << "  0. Exit\n"
            << "----------------------------------------------\n"
            << "Enter choice: ";
        choice = readMenuChoice();
        switch (choice) {
        case 2: service.run();                                   break;
        case 3: runTask3Menu(manager);                           break;
        case 4: dispatchAndAssign(service, manager, resourcesPath); break;
        case 0:                                                  break;
        default: std::cout << "Invalid choice.\n";               break;
        }
    } while (choice != 0);

    service.save();
    manager.saveToCSV(resourcesPath);
    return 0;
}