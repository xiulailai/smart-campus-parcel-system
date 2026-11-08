#include "Task1/ParcelIntake.hpp"
#include "Task2/ServiceRequest.hpp"
#include "Task3/ResourceQueue.hpp"
#include "Task5/ParcelCategoryTree.hpp"
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

// Lets the Task 3 hooks below reach the Task 2 module
static ServiceModule* g_service = 0;

// ---------------------------------------------------------------------------
// Task 3 -> Task 2 hooks
// ---------------------------------------------------------------------------

// A busy resource lost its request (it became Unavailable, or was switched to
// Available by hand). The request goes back to Pending so it can be re-assigned.
void onRequestLost(const std::string& requestID) {
    ServiceRequest r;
    if (g_service == 0 || !g_service->findRequest(requestID, r)) return;
    if (r.status != STATUS_ASSIGNED) return;
    std::string msg;
    g_service->restoreRequestStatus(requestID, STATUS_PENDING, msg);
    std::cout << "[Task 2] " << msg << "\n";
}

// Called just before Task 3 releases a resource. Releasing means the job is
// finished, so Task 2 completes the request first. Returning false cancels the release.
bool beforeRelease(const std::string& requestID) {
    ServiceRequest r;
    if (g_service == 0 || !g_service->findRequest(requestID, r)) return true;
    if (r.status != STATUS_ASSIGNED) return true;   // already Completed / Cancelled: nothing to close

    std::string collector, msg;
    if (r.serviceType == SERVICE_COLLECTION) {
        std::cout << "Collection check - enter the collector's Recipient ID: ";
        std::getline(std::cin, collector);
    }
    bool ok = g_service->completeRequest(requestID, collector, msg);
    std::cout << (ok ? "[Task 2] " : "[Error] ") << msg << "\n";
    return ok;
}

// Task 2 -> Task 3: after the Task 2 menu, free every resource whose request is
// now Completed or Cancelled.
void releaseFinishedResources(ServiceModule& service, ResourceManager& resources,
    const std::string& resourcesPath) {
    bool changed = false;
    for (int i = 0; i < resources.resourceCount(); i++) {
        Resource* res = resources.resourceAt(i);
        if (res == 0 || res->status != OCCUPIED) continue;

        ServiceRequest r;
        if (!service.findRequest(res->currentAssignment, r)) continue;
        if (r.status == STATUS_COMPLETED || r.status == STATUS_CANCELLED) {
            std::string id = res->resourceID;
            if (resources.releaseResource(id)) changed = true;
        }
    }
    if (changed) resources.saveToCSV(resourcesPath);
}

// ---------------------------------------------------------------------------
// Integrated flow (system steps 2-4).  Task 1 sorts, Task 2 opens the service:
//   1. Task 1 shows the parcel at the front of the intake queue, suggests a
//      route and staff confirm it                            (sortNextParcel)
//   2. Task 1 updates the parcel status and removes it from the queue
//   3. If the new status needs a service, Task 2 creates the matching
//      Pending request, ready for option 4 to give it a resource (createRequest)
// On-hold parcels wait for staff, so no request is made.
// ---------------------------------------------------------------------------
std::string serviceForSortedParcel(const std::string& parcelStatus) {
    if (parcelStatus == PARCEL_READY_FOR_COLLECTION) return SERVICE_COLLECTION;
    if (parcelStatus == PARCEL_ASSIGNED_FOR_DELIVERY) return SERVICE_CAMPUS_DELIVERY;
    if (parcelStatus == PARCEL_SPECIAL_HANDLING) return SERVICE_SPECIAL_HANDLING;
    if (parcelStatus == PARCEL_RETURN_TO_SENDER) return SERVICE_RETURN;
    return "";
}

void sortAndRequestNext(ParcelModule& parcels, ServiceModule& service) {
    Parcel p;
    if (!parcels.sortNextParcel(p)) return;          // Task 1 already said why

    std::string serviceType = serviceForSortedParcel(p.status);
    if (serviceType.empty()) {
        std::cout << "[Info] " << p.parcelID << " is " << p.status << ": no service request needed now.\n";
        return;
    }
    std::string msg;
    int urgency = parcels.suggestedUrgency(p);
    if (service.createRequest(p.parcelID, p.recipientID, serviceType, urgency, msg))
        std::cout << "[OK] " << msg << " Urgency " << urgency << " (" << p.parcelType << " parcel).\n";
    else
        std::cout << "[Info] No new request: " << msg << "\n";
}

// ---------------------------------------------------------------------------
// Integrated flow (system steps 5-7).  Task 2 decides, Task 3 allocates:
//   1. Task 2 says which Pending request is the most urgent      (peekNext)
//   2. Task 2 checks the request can be assigned right now       (validateForAssignment)
//   3. Task 3 allocates a resource: automatically (round-robin) or the one the
//      staff member chooses                       (assignRequest / assignToResource)
//   4. Task 2 marks the request Assigned and saves requests.csv  (markAssigned)
// If nobody is free in step 3 nothing changes: the request simply stays Pending.
// ---------------------------------------------------------------------------
void assignNextRequest(ServiceModule& service, ResourceManager& resources,
    const std::string& resourcesPath, bool chooseResource) {
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
    bool allocated;
    if (chooseResource) {
        std::string resourceID;
        std::cout << "Resource ID to use (e.g. C01 or D02): ";
        std::getline(std::cin, resourceID);
        allocated = resources.assignToResource(r.requestID, r.serviceType, resourceID);
    }
    else {
        allocated = resources.assignRequest(r.requestID, r.serviceType);
    }
    if (!allocated) {
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

    ParcelModule parcels(parcelsPath, requestsPath);    // Task 1
    parcels.load();

    ServiceModule service(requestsPath, parcelsPath);   // Task 2
    service.load();

    ResourceManager manager;                            // Task 3
    manager.loadFromCSV(resourcesPath);

    ParcelCategoryModule parcelCategories(parcelsPath, requestsPath); // Task 5
    parcelCategories.load();

    // Connect Task 3 to Task 2
    g_service = &service;
    manager.setRequestLostHook(onRequestLost);
    manager.setBeforeReleaseHook(beforeRelease);

    int choice;
    do {
        std::cout << "\n==============================================\n"
            << "  SMART CAMPUS PARCEL SYSTEM\n"
            << "==============================================\n"
            << "  1. Task 1 - Parcel intake, sorting and processing\n"
            << "  2. Task 2 - Service requests\n"
            << "  3. Task 3 - Counters and delivery resources\n"
            << "  4. Assign a resource to the next request (Task 2 + Task 3)\n"
            << "  5. Sort the next parcel and open its request (Task 1 + Task 2)\n"
            << "  6. Assign the next request to a resource I choose (Task 2 + Task 3)\n"
            << "  7. Task 5 - Parcel category organisation (BST)\n"
            << "  0. Exit\n"
            << "----------------------------------------------\n"
            << "Enter choice: ";
        choice = readMenuChoice();
        switch (choice) {
        case 1: parcels.run();
            break;
        case 2:
            service.run();
            releaseFinishedResources(service, manager, resourcesPath);
            break;
        case 3: runTask3Menu(manager);
            break;
        case 4: assignNextRequest(service, manager, resourcesPath, false);
            break;
        case 5: sortAndRequestNext(parcels, service);
            break;
        case 6: assignNextRequest(service, manager, resourcesPath, true);
            break;
        case 7: parcelCategories.run();
        break;
        case 0:
            break;
        default: std::cout << "Invalid choice.\n";
            break;
        }
    } while (choice != 0);

    service.save();
    manager.saveToCSV(resourcesPath);
    return 0;
}
