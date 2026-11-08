// ============================================================================
// ActivityHook.hpp
// Shared bridge between Task 1/2/3 and Task 4.
//
// Tasks 1/2/3 only need to include this tiny header to talk to Task 4.
// That avoids the circular dependency of Task 1 -> Task 4 -> Task 1.
//
// Activity type constants live here so everyone uses the same spellings.
// ============================================================================
#ifndef ACTIVITY_HOOK_HPP
#define ACTIVITY_HOOK_HPP

#include <string>

// activity types
const std::string ACT_REGISTERED = "Registered";           // parcel arrived
const std::string ACT_SORTED = "Sorted";               // routed to next action
const std::string ACT_STATUS_UPDATED = "StatusUpdated";        // status changed by staff
const std::string ACT_INFO_UPDATED = "InfoUpdated";          // delivery info changed
const std::string ACT_MISSING = "Missing";              // reported missing
const std::string ACT_REMOVED = "Removed";              // parcel record removed
const std::string ACT_REQUEST_CREATED = "RequestCreated";       // service request opened
const std::string ACT_ASSIGNED = "Assigned";             // request given a resource
const std::string ACT_COMPLETED = "Completed";            // request finished
const std::string ACT_CANCELLED = "Cancelled";            // parcel or request cancelled
const std::string ACT_UNDONE = "Undone";               // an earlier activity was reversed

// one activity event
struct ActivityEvent {
    std::string parcelID;
    std::string activityType;        // one of the ACT_* strings above
    std::string oldStatus = "-";     // parcel status before the change
    std::string newStatus;           // parcel status after the change
    std::string requestID = "-";     // request ID or "-"
    std::string resourceID = "-";    // C01 / D01 / "-"
};

// Raw function pointer (same style Task 3 uses for its own hooks).
typedef void (*ActivityHook)(const ActivityEvent& event);

// Small helper so each module does not have to repeat the null check.
// Do nothing when no hook has been registered.
inline void publishActivity(ActivityHook hook,
    const std::string& parcelID,
    const std::string& type,
    const std::string& oldStatus,
    const std::string& newStatus,
    const std::string& requestID = "-",
    const std::string& resourceID = "-") {
    if (hook == nullptr) return;
    ActivityEvent e;
    e.parcelID = parcelID;
    e.activityType = type;
    e.oldStatus = oldStatus;
    e.newStatus = newStatus;
    e.requestID = requestID;
    e.resourceID = resourceID;
    hook(e);
}

#endif