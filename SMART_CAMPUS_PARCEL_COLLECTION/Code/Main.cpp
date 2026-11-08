#include "Task1/ParcelIntake.hpp"
#include "Task2/ServiceRequest.hpp"
#include "Task3/ResourceQueue.hpp"
#include "Task4/ActivityLog.hpp"
#include "Task5/ParcelCategoryTree.hpp"
#include "ActivityHook.hpp"
#include <fstream>
#include <iostream>
#include <string>

std::string findDataFile(const std::string& fileName) {
    const char* folders[] = { "Data\\", "..\\Data\\", "..\\..\\Data\\", "..\\..\\..\\Data\\" };
    for (int i = 0; i < 4; i++) {
        std::string path = std::string(folders[i]) + fileName;
        std::ifstream test(path.c_str());
        if (test.is_open()) return path;
    }
    return fileName;
}

// ---------------------------------------------------------------------------
// Task 3 <-> Task 2 hooks
// ---------------------------------------------------------------------------
static ServiceModule* g_service = 0;

void onRequestLost(const std::string& requestID) {
    ServiceRequest r;
    if (g_service == 0 || !g_service->findRequest(requestID, r)) return;
    if (r.status != STATUS_ASSIGNED) return;
    std::string msg;
    g_service->restoreRequestStatus(requestID, STATUS_PENDING, msg);
    std::cout << "[Task 2] " << msg << "\n";
}

bool beforeRelease(const std::string& requestID) {
    ServiceRequest r;
    if (g_service == 0 || !g_service->findRequest(requestID, r)) return true;
    if (r.status != STATUS_ASSIGNED) return true;

    std::string collector, msg;
    if (r.serviceType == SERVICE_COLLECTION) {
        std::cout << "Collection check - enter the collector's Recipient ID: ";
        std::getline(std::cin, collector);
    }
    bool ok = g_service->completeRequest(requestID, collector, msg);
    std::cout << (ok ? "[Task 2] " : "[Error] ") << msg << "\n";
    return ok;
}

// ---------------------------------------------------------------------------
// Task 4 bridge — Tasks 1/2/3 only see the raw function pointer, not
// ActivityModule, so no circular dependency.
// ---------------------------------------------------------------------------
static ActivityModule* g_activity = nullptr;

static void activityBridge(const ActivityEvent& e) {
    if (g_activity == nullptr) return;
    g_activity->logActivity(e.parcelID, e.activityType,
        e.oldStatus, e.newStatus,
        e.requestID, e.resourceID);
}

// ---------------------------------------------------------------------------
// Free every resource whose request is now Completed or Cancelled
// ---------------------------------------------------------------------------
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
// Integrated flow (system steps 2-4)
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
    if (!parcels.sortNextParcel(p)) return;

    std::string serviceType = serviceForSortedParcel(p.status);
    if (serviceType.empty()) {
        std::cout << "[Info] " << p.parcelID << " is " << p.status
            << ": no service request needed now.\n";
        return;
    }
    std::string msg;
    int urgency = parcels.suggestedUrgency(p);
    if (service.createRequest(p.parcelID, p.recipientID, serviceType, urgency, msg))
        std::cout << "[OK] " << msg << " Urgency " << urgency
        << " (" << p.parcelType << " parcel).\n";
    else
        std::cout << "[Info] No new request: " << msg << "\n";
}

// ---------------------------------------------------------------------------
// Integrated flow (system steps 5-7).  Task 4 logging now happens inside
// Task 2::markAssigned(), so this function only orchestrates.
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

    std::string resourceID;
    bool allocated;
    if (chooseResource) {
        std::string typed;
        std::cout << "Resource ID to use (e.g. C01 or D02): ";
        std::getline(std::cin, typed);
        allocated = resources.assignToResource(r.requestID, r.serviceType,
            typed, &resourceID);
    }
    else {
        allocated = resources.assignRequest(r.requestID, r.serviceType, &resourceID);
    }
    if (!allocated) {
        std::cout << "[Info] " << r.requestID << " stays Pending.\n";
        return;
    }
    resources.saveToCSV(resourcesPath);

    if (service.markAssigned(r.requestID, resourceID, msg))     // NEW arg
        std::cout << "[OK] " << msg << "\n";
    else
        std::cout << "[Warning] Resource allocated but request not updated: " << msg << "\n";
}

int readMenuChoice() {
    std::string s;
    std::getline(std::cin, s);
    if (!std::cin) return 0;
    if (s.size() == 1 && s[0] >= 0x30 && s[0] <= 0x39) return s[0] - 0x30;
    return -1;
}

int main() {
    std::string requestsPath = findDataFile("requests.csv");
    std::string parcelsPath = findDataFile("parcels.csv");
    std::string resourcesPath = findDataFile("resources.csv");
    std::string activitiesPath = findDataFile("activities.csv");

    ParcelModule parcels(parcelsPath, requestsPath);
    parcels.load();

    ServiceModule service(requestsPath, parcelsPath);
    service.load();

    ResourceManager manager;
    manager.loadFromCSV(resourcesPath);

    ParcelCategoryModule parcelCategories(parcelsPath, requestsPath);
    parcelCategories.load();
    parcelCategories.setParcelModule(&parcels);

    // Connect Task 3 to Task 2
    g_service = &service;
    manager.setRequestLostHook(onRequestLost);
    manager.setBeforeReleaseHook(beforeRelease);

    ActivityModule activities(activitiesPath);
    activities.load();
    activities.connect(&parcels, &service, &manager, resourcesPath);

    // Connect Task 1 and Task 2 to Task 4 through the bridge
    g_activity = &activities;
    parcels.setActivityHook(activityBridge);
    service.setActivityHook(activityBridge);

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
            << "  8. Task 4 - Parcel activity history and undo\n"
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
        case 3:
            runTask3Menu(manager, &service);
            manager.saveToCSV(resourcesPath);   // persist Task 3 menu changes
            break;
        case 4:
            assignNextRequest(service, manager, resourcesPath, false);
            break;
        case 5:
            sortAndRequestNext(parcels, service);
            break;
        case 6:
            assignNextRequest(service, manager, resourcesPath, true);
            break;
        case 7:
            parcelCategories.run();
            break;
        case 8:
            activities.run();
            break;
        case 0:
            break;
        default:
            std::cout << "Invalid choice.\n";
            break;
        }
    } while (choice != 0);

    service.save();
    manager.saveToCSV(resourcesPath);
    return 0;
}