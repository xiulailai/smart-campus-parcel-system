// ============================================================================
// ActivityLog.cpp
// Task 4 - Parcel Activity and Record Management Module
// ============================================================================
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS     // lets Visual Studio accept localtime()
#endif

#include "ActivityLog.hpp"
#include "../Task1/ParcelIntake.hpp"      // PARCEL_ status names + restoreParcelStatus (undo)
#include "../Task2/ServiceRequest.hpp"    // findRequest / restoreRequestStatus (undo)
#include "../Task3/ResourceQueue.hpp"     // findResource / updateResourceStatus (undo)
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cctype>
#include <cstdlib>

// ============================================================================
//  Small helpers (only used inside this file)
// ============================================================================
namespace {

    const std::string NONE = "-";               // "no value" in the file
    const std::string CSV_HEADER =
        "ActivityID,ParcelID,Type,OldStatus,NewStatus,RequestID,ResourceID,Timestamp,Reversed";

    const int ACTIVITY_TYPE_COUNT = 11;
    const std::string ALL_TYPES[ACTIVITY_TYPE_COUNT] = {
        ACT_REGISTERED, ACT_SORTED, ACT_STATUS_UPDATED, ACT_INFO_UPDATED, ACT_MISSING,
        ACT_REMOVED, ACT_REQUEST_CREATED, ACT_ASSIGNED, ACT_COMPLETED, ACT_CANCELLED,
        ACT_UNDONE
    };

    const int STATUS_COUNT = 11;
    const std::string ALL_STATUSES[STATUS_COUNT] = {
        PARCEL_REGISTERED, PARCEL_READY_FOR_COLLECTION, PARCEL_ASSIGNED_FOR_DELIVERY,
        PARCEL_SPECIAL_HANDLING, PARCEL_RETURN_TO_SENDER, PARCEL_ON_HOLD, PARCEL_MISSING,
        PARCEL_COLLECTED, PARCEL_DELIVERED, PARCEL_RETURNED, PARCEL_CANCELLED
    };

    std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    std::string toUpper(std::string s) {
        for (size_t i = 0; i < s.size(); i++)
            s[i] = (char)std::toupper((unsigned char)s[i]);
        return s;
    }

    bool allDigits(const std::string& s) {
        if (s.empty()) return false;
        for (size_t i = 0; i < s.size(); i++)
            if (!std::isdigit((unsigned char)s[i])) return false;
        return true;
    }

    // Splits a CSV line on commas into fields[] (at most maxFields stored).
    // Returns the REAL number of fields so callers can detect malformed rows.
    int splitCSV(const std::string& line, std::string fields[], int maxFields) {
        int n = 0;
        std::string cur;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == ',') {
                if (n < maxFields) fields[n] = trim(cur);
                n++;
                cur.clear();
            }
            else cur += line[i];
        }
        if (n < maxFields) fields[n] = trim(cur);
        return n + 1;
    }

    std::string fit(const std::string& s, size_t width) {   // truncate for table columns
        return s.size() > width ? s.substr(0, width) : s;
    }

    std::string currentTimestamp() {
        time_t t = time(nullptr);
        tm* lt = localtime(&t);
        char buf[32] = "";
        if (lt != nullptr) strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", lt);
        return std::string(buf);
    }

    std::string makeActivityID(int number) {
        std::ostringstream out;
        out << 'A' << std::setw(3) << std::setfill('0') << number;
        return out.str();
    }

    // ---- validation --------------------------------------------------------

    bool isKnownStatus(const std::string& s) {
        for (int i = 0; i < STATUS_COUNT; i++)
            if (ALL_STATUSES[i] == s) return true;
        return false;
    }

    // "A" followed by 3 to 6 digits; number receives the digits as an int
    bool isValidActivityID(const std::string& id, int& number) {
        if (id.size() < 4 || id.size() > 7 || id[0] != 'A' || !allDigits(id.substr(1)))
            return false;
        number = std::atoi(id.c_str() + 1);
        return true;
    }

    // "P" followed by 3 to 6 digits, e.g. P001
    bool isValidParcelID(const std::string& id) {
        return id.size() >= 4 && id.size() <= 7 && id[0] == 'P' && allDigits(id.substr(1));
    }

    // "-" (none) or "R" followed by 3 to 6 digits, e.g. R014
    bool isValidRequestRef(const std::string& id) {
        if (id == NONE) return true;
        return id.size() >= 4 && id.size() <= 7 && id[0] == 'R' && allDigits(id.substr(1));
    }

    // "-" (none) or "C" / "D" followed by 2 to 4 digits, e.g. C01, D03
    bool isValidResourceRef(const std::string& id) {
        if (id == NONE) return true;
        return id.size() >= 3 && id.size() <= 5 && (id[0] == 'C' || id[0] == 'D') &&
            allDigits(id.substr(1));
    }

    // "YYYY-MM-DD HH:MM"
    bool isValidTimestamp(const std::string& t) {
        if (t.size() != 16) return false;
        for (size_t i = 0; i < t.size(); i++) {
            if (i == 4 || i == 7) { if (t[i] != '-') return false; }
            else if (i == 10) { if (t[i] != ' ') return false; }
            else if (i == 13) { if (t[i] != ':') return false; }
            else if (!std::isdigit((unsigned char)t[i])) return false;
        }
        int month = std::atoi(t.substr(5, 2).c_str());
        int day = std::atoi(t.substr(8, 2).c_str());
        int hour = std::atoi(t.substr(11, 2).c_str());
        int minute = std::atoi(t.substr(14, 2).c_str());
        return month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
            hour <= 23 && minute <= 59;
    }

    // Tidies and checks every field of an Activity. Used for rows read from the
    // file AND for activities logged by other modules, so both follow the same
    // rules. Because every field is checked against a fixed pattern or list, no
    // field can contain a comma, which keeps the CSV safe.
    bool checkActivity(Activity& a, std::string& reason) {
        int number = 0;
        a.activityID = toUpper(trim(a.activityID));
        a.parcelID = toUpper(trim(a.parcelID));
        a.type = trim(a.type);
        a.oldStatus = trim(a.oldStatus);
        a.newStatus = trim(a.newStatus);
        a.requestID = toUpper(trim(a.requestID));
        a.resourceID = toUpper(trim(a.resourceID));
        a.timestamp = trim(a.timestamp);
        if (a.requestID.empty()) a.requestID = NONE;
        if (a.resourceID.empty()) a.resourceID = NONE;

        if (!isValidActivityID(a.activityID, number)) { reason = "invalid Activity ID '" + a.activityID + "'"; return false; }
        if (!isValidParcelID(a.parcelID)) { reason = "invalid Parcel ID '" + a.parcelID + "'"; return false; }
        if (!isKnownActivityType(a.type)) { reason = "unknown activity type '" + a.type + "'"; return false; }
        if (a.oldStatus != NONE && !isKnownStatus(a.oldStatus)) { reason = "unknown old status '" + a.oldStatus + "'"; return false; }
        if (!isKnownStatus(a.newStatus)) { reason = "unknown new status '" + a.newStatus + "'"; return false; }
        if (!isValidRequestRef(a.requestID)) { reason = "invalid Request ID '" + a.requestID + "'"; return false; }
        if (!isValidResourceRef(a.resourceID)) { reason = "invalid Resource ID '" + a.resourceID + "'"; return false; }
        if (!isValidTimestamp(a.timestamp)) { reason = "invalid timestamp '" + a.timestamp + "' (use YYYY-MM-DD HH:MM)"; return false; }
        return true;
    }

    // Reads a menu choice; returns -1 for anything that is not a single number
    int readChoice() {
        std::string s;
        std::getline(std::cin, s);
        if (!std::cin) return 0;                    // end of input = leave the menu
        s = trim(s);
        if (s.size() == 1 && s[0] >= '0' && s[0] <= '9') return s[0] - '0';
        return -1;
    }

    std::string ask(const std::string& prompt) {
        std::string s;
        std::cout << prompt;
        std::getline(std::cin, s);
        return trim(s);
    }

    // Tidies a typed Parcel ID and prints an error if it cannot be one
    bool cleanParcelID(std::string& id) {
        id = toUpper(trim(id));
        if (isValidParcelID(id)) return true;
        std::cout << "[Error] Invalid Parcel ID '" << id
            << "'. Use P followed by digits, e.g. P001.\n";
        return false;
    }

    // An activity is "live" while it has not been reversed. The Undone records
    // are only receipts of a reversal, so they are never undone themselves.
    bool isLive(const Activity& a) {
        return a.type != ACT_UNDONE && !a.reversed;
    }

    // Statuses that finish a parcel's journey
    bool isFinishedParcelStatus(const std::string& s) {
        return s == PARCEL_COLLECTED || s == PARCEL_DELIVERED || s == PARCEL_RETURNED;
    }

    // Table for ONE parcel's activities (no Parcel column), list[0] = newest
    void printParcelRows(const Activity* const* list, int n) {
        std::cout << std::string(104, '=') << "\n"
            << std::left
            << std::setw(4) << "No" << std::setw(6) << "ID" << std::setw(17) << "Time"
            << std::setw(15) << "Type" << std::setw(21) << "Old status"
            << std::setw(21) << "New status" << std::setw(6) << "Req"
            << std::setw(6) << "Res" << "Reversed\n"
            << std::string(104, '-') << "\n";
        for (int i = 0; i < n; i++) {
            const Activity& a = *list[i];
            std::cout << std::setw(4) << (i + 1) << std::setw(6) << a.activityID
                << std::setw(17) << a.timestamp << std::setw(15) << fit(a.type, 14)
                << std::setw(21) << fit(a.oldStatus, 20) << std::setw(21) << fit(a.newStatus, 20)
                << std::setw(6) << a.requestID << std::setw(6) << a.resourceID
                << (a.reversed ? "Yes" : "No") << "\n";
        }
        std::cout << std::string(104, '=') << "\n";
    }

}   // namespace

// ============================================================================
//  Activity types
// ============================================================================
bool isKnownActivityType(const std::string& type) {
    for (int i = 0; i < ACTIVITY_TYPE_COUNT; i++)
        if (ALL_TYPES[i] == type) return true;
    return false;
}

// Parcel status changes can be reversed. Registering, request creation,
// completion and cancellation cannot: they would leave the parcel, request
// and resource disagreeing with each other.
bool isUndoableActivityType(const std::string& type) {
    return type == ACT_SORTED || type == ACT_STATUS_UPDATED ||
        type == ACT_MISSING || type == ACT_ASSIGNED;
}

// ============================================================================
//  ActivityStack - linked-list stack
// ============================================================================
ActivityStack::ActivityStack() : topNode(nullptr), count(0) {}

ActivityStack::~ActivityStack() { clear(); }

// O(1): the new node becomes the top
void ActivityStack::push(const Activity& a) {
    Node* node = new Node{ a, topNode };
    topNode = node;
    count++;
}

// O(1): the most recent activity, left in place
Activity* ActivityStack::peek() {
    return topNode == nullptr ? nullptr : &topNode->data;
}

const Activity* ActivityStack::peek() const {
    return topNode == nullptr ? nullptr : &topNode->data;
}

// O(1): removes the top. Used to drop an activity that could not be saved.
bool ActivityStack::pop(Activity& out) {
    if (topNode == nullptr) return false;
    Node* node = topNode;
    out = node->data;
    topNode = node->next;
    delete node;
    count--;
    return true;
}

// O(n): pointers to the activities, newest first (index 0 = top)
int ActivityStack::toArray(const Activity** out, int max) const {
    int n = 0;
    for (const Node* cur = topNode; cur != nullptr && n < max; cur = cur->next)
        out[n++] = &cur->data;
    return n;
}

// O(n): walks from the top and stops at the first activity match() accepts
Activity* ActivityStack::findFirst(bool (*match)(const Activity&), int* position) {
    int passed = 0;
    for (Node* cur = topNode; cur != nullptr; cur = cur->next, passed++) {
        if (match(cur->data)) {
            if (position != nullptr) *position = passed;
            return &cur->data;
        }
    }
    if (position != nullptr) *position = passed;
    return nullptr;
}

void ActivityStack::clear() {
    while (topNode != nullptr) {
        Node* next = topNode->next;
        delete topNode;
        topNode = next;
    }
    count = 0;
}

// ============================================================================
//  ActivityModule - file I/O
// ============================================================================
ActivityModule::ActivityModule(const std::string& activitiesFile)
    : activitiesPath(activitiesFile), nextNumber(1),
    parcels(nullptr), requests(nullptr), resources(nullptr) {
}

// Checks one row of activities.csv. Bad rows are skipped, never crash.
static bool parseActivityRow(const std::string& line, Activity& a, std::string& reason) {
    std::string f[9];
    if (splitCSV(line, f, 9) != 9) { reason = "expected 9 fields"; return false; }
    for (int i = 0; i < 9; i++)
        if (f[i].empty()) { reason = "missing field"; return false; }

    a.activityID = f[0];  a.parcelID = f[1];  a.type = f[2];
    a.oldStatus = f[3];   a.newStatus = f[4]; a.requestID = f[5];
    a.resourceID = f[6];  a.timestamp = f[7];
    if (f[8] == "true") a.reversed = true;
    else if (f[8] == "false") a.reversed = false;
    else { reason = "Reversed must be true or false, not '" + f[8] + "'"; return false; }

    return checkActivity(a, reason);
}

// Reads activities.csv into the stack, oldest row first, so the newest row
// ends up on top. Activity IDs must rise down the file, which also rejects
// duplicates in O(1) per row.
bool ActivityModule::readFile(bool verbose, int& loaded, int& skipped) {
    loaded = skipped = 0;
    std::ifstream in(activitiesPath.c_str());
    if (!in.is_open()) return false;

    history.clear();
    nextNumber = 1;

    std::string line, reason;
    int lineNo = 0;
    int lastNumber = 0;
    while (std::getline(in, line)) {
        lineNo++;
        if (lineNo == 1) continue;                 // header row
        line = trim(line);
        if (line.empty()) continue;

        Activity a;
        if (!parseActivityRow(line, a, reason)) {
            if (verbose) std::cout << "[Skipped] activities.csv line " << lineNo << ": " << reason << "\n";
            skipped++;
            continue;
        }
        int number = std::atoi(a.activityID.c_str() + 1);
        if (number <= lastNumber) {
            if (verbose) std::cout << "[Skipped] activities.csv line " << lineNo
                << ": " << a.activityID << " is a duplicate or out of order\n";
            skipped++;
            continue;
        }
        lastNumber = number;
        history.push(a);
        loaded++;
    }
    nextNumber = lastNumber + 1;
    return true;
}

bool ActivityModule::load() {
    int loaded = 0, skipped = 0;
    if (!readFile(true, loaded, skipped)) {
        history.clear();
        nextNumber = 1;
        std::cout << "[Info] " << activitiesPath
            << " not found. Starting with an empty history; it is created when the first activity is recorded.\n";
        return false;
    }
    std::cout << "Loaded " << loaded << " activity record(s), " << skipped << " row(s) skipped.";
    const Activity* top = history.peek();
    if (top != nullptr)
        std::cout << " Most recent: " << top->activityID << " (" << top->parcelID << " "
        << top->type << ", " << top->timestamp << ").";
    std::cout << "\n";
    return true;
}

// Writes the whole file, oldest first. The stack is read newest first, so the
// pointers are written back to front.
bool ActivityModule::save() const {
    std::ofstream out(activitiesPath.c_str());
    if (!out.is_open()) return false;
    out << CSV_HEADER << "\n";

    int n = history.size();
    const Activity** items = new const Activity * [n > 0 ? n : 1];
    history.toArray(items, n);
    for (int i = n - 1; i >= 0; i--) {
        const Activity& a = *items[i];
        out << a.activityID << ',' << a.parcelID << ',' << a.type << ','
            << a.oldStatus << ',' << a.newStatus << ',' << a.requestID << ','
            << a.resourceID << ',' << a.timestamp << ','
            << (a.reversed ? "true" : "false") << '\n';
    }
    delete[] items;
    return out.good();
}

// ============================================================================
//  ActivityModule - integration API
// ============================================================================

// Req 1: push the activity, then save. If the file cannot be written the
// activity is popped again, so memory never disagrees with the file.
bool ActivityModule::logActivity(const std::string& parcelID, const std::string& type,
    const std::string& oldStatus, const std::string& newStatus,
    const std::string& requestID, const std::string& resourceID,
    const std::string& timestamp) {
    Activity a;
    a.activityID = makeActivityID(nextNumber);
    a.parcelID = parcelID;
    a.type = type;
    a.oldStatus = oldStatus;
    a.newStatus = newStatus;
    a.requestID = requestID;
    a.resourceID = resourceID;
    a.timestamp = trim(timestamp).empty() ? currentTimestamp() : timestamp;
    a.reversed = false;

    std::string reason;
    if (!checkActivity(a, reason)) {
        lastMessage = "Activity not recorded: " + reason + ".";
        return false;
    }

    history.push(a);
    if (!save()) {
        Activity dropped;
        history.pop(dropped);
        lastMessage = "Activity not recorded: cannot write " + activitiesPath + ".";
        return false;
    }
    nextNumber++;
    lastMessage = "Activity " + a.activityID + " recorded.";
    return true;
}

// Req 2: the most recent activities, newest first
void ActivityModule::displayRecent(int n) const {
    int total = history.size();
    if (total == 0) {
        std::cout << "\nNo activity history yet.\n";
        return;
    }
    if (n < 1) n = 1;
    if (n > total) n = total;

    const Activity** items = new const Activity * [n];
    int shown = history.toArray(items, n);

    std::cout << "\nRECENT ACTIVITIES (newest first, showing " << shown << " of " << total << ")\n";
    std::cout << std::string(112, '=') << "\n"
        << std::left
        << std::setw(4) << "No" << std::setw(6) << "ID" << std::setw(17) << "Time"
        << std::setw(8) << "Parcel" << std::setw(15) << "Type"
        << std::setw(21) << "Old status" << std::setw(21) << "New status"
        << std::setw(6) << "Req" << std::setw(6) << "Res" << "Reversed\n"
        << std::string(112, '-') << "\n";
    for (int i = 0; i < shown; i++) {
        const Activity& a = *items[i];
        std::cout << std::setw(4) << (i + 1) << std::setw(6) << a.activityID
            << std::setw(17) << a.timestamp << std::setw(8) << a.parcelID
            << std::setw(15) << fit(a.type, 14)
            << std::setw(21) << fit(a.oldStatus, 20) << std::setw(21) << fit(a.newStatus, 20)
            << std::setw(6) << a.requestID << std::setw(6) << a.resourceID
            << (a.reversed ? "Yes" : "No") << "\n";
    }
    std::cout << std::string(112, '=') << "\n";
    delete[] items;
}

// O(n): one pass over the whole stack, keeping only this parcel's activities.
// The output order stays newest first because the stack is walked top down.
int ActivityModule::collectForParcel(const std::string& parcelID, const Activity** out) const {
    int total = history.size();
    const Activity** all = new const Activity * [total > 0 ? total : 1];
    history.toArray(all, total);
    int found = 0;
    for (int i = 0; i < total; i++)
        if (all[i]->parcelID == parcelID) out[found++] = all[i];
    delete[] all;
    return found;
}

// Req 3: the history of one parcel
int ActivityModule::displayParcelHistory(const std::string& parcelID) const {
    std::string id = parcelID;
    if (!cleanParcelID(id)) return -1;

    int total = history.size();
    const Activity** list = new const Activity * [total > 0 ? total : 1];
    int n = collectForParcel(id, list);

    if (n == 0) {
        std::cout << "\nParcel " << id << " has no activity history.\n";
    }
    else {
        std::cout << "\nACTIVITY HISTORY OF " << id << " (newest first, " << n << " record"
            << (n == 1 ? "" : "s") << ")\n";
        printParcelRows(list, n);
    }
    delete[] list;
    return n;
}

// Req 4: review one parcel. Everything here comes from the log; the status is
// the last one RECORDED, so it is only as current as the modules that report.
int ActivityModule::reviewParcel(const std::string& parcelID) const {
    std::string id = parcelID;
    if (!cleanParcelID(id)) return -1;

    int total = history.size();
    const Activity** list = new const Activity * [total > 0 ? total : 1];
    int n = collectForParcel(id, list);

    if (n == 0) {
        std::cout << "\nParcel " << id << " has no activity history.\n";
        delete[] list;
        return 0;
    }

    int reversedCount = 0;
    std::string lastRequest = NONE, lastResource = NONE;     // newest linked ones
    for (int i = 0; i < n; i++) {
        if (list[i]->reversed) reversedCount++;
        if (lastRequest == NONE && list[i]->requestID != NONE) lastRequest = list[i]->requestID;
        if (lastResource == NONE && list[i]->resourceID != NONE) lastResource = list[i]->resourceID;
    }
    const Activity& newest = *list[0];
    const Activity& oldest = *list[n - 1];

    std::cout << "\nPARCEL REVIEW - " << id << "\n"
        << std::string(60, '-') << "\n"
        << "  Last logged status : " << newest.newStatus << "\n"
        << "  Latest activity    : " << newest.type << " (" << newest.activityID
        << ", " << newest.timestamp << ")" << (newest.reversed ? " - reversed" : "") << "\n"
        << "  First activity     : " << oldest.type << " (" << oldest.activityID
        << ", " << oldest.timestamp << ")\n"
        << "  Activities logged  : " << n << " (" << reversedCount << " reversed)\n"
        << "  Latest request     : " << lastRequest << "\n"
        << "  Latest resource    : " << lastResource << "\n"
        << std::string(60, '-') << "\n"
        << "  Status is the last one recorded in the log, not read live from the parcel.\n";

    std::cout << "\nACTIVITY HISTORY (newest first)\n";
    printParcelRows(list, n);
    delete[] list;
    return n;
}

// ============================================================================
//  ActivityModule - undo (Req 5)
// ============================================================================
void ActivityModule::connect(ParcelModule* parcelModule, ServiceModule* serviceModule,
    ResourceManager* resourceManager, const std::string& resourcesFile) {
    parcels = parcelModule;
    requests = serviceModule;
    resources = resourceManager;
    resourcesPath = resourcesFile;
}

// The newest activity still in effect. Activities that were already reversed,
// and the Undone records that reversed them, are passed over, so staff can undo
// several steps one after another. A live activity that cannot be undone is
// NEVER passed over: it is returned, and canUndo() refuses it.
Activity* ActivityModule::liveTarget(std::string& why, int* position) {
    if (position != nullptr) *position = 0;
    if (history.isEmpty()) {
        why = "There is no activity to undo (the history is empty).";
        return nullptr;
    }
    Activity* target = history.findFirst(isLive, position);
    if (target == nullptr)
        why = "Every activity has already been reversed. There is nothing left to undo.";
    return target;
}

const Activity* ActivityModule::undoTarget(std::string& why, int* position) {
    return liveTarget(why, position);
}

// Every rule, checked BEFORE anything is changed
bool ActivityModule::canUndo(const Activity& a, std::string& why) const {
    if (parcels == nullptr || requests == nullptr || resources == nullptr) {
        why = "the undo step is not connected to the parcel, request and resource modules";
        return false;
    }
    if (!isUndoableActivityType(a.type)) {
        why = a.type + " activities cannot be reversed";
        return false;
    }
    if (a.type == ACT_STATUS_UPDATED && isFinishedParcelStatus(a.newStatus)) {
        why = "it finished the parcel (" + a.newStatus + "), and a finished parcel is not reopened";
        return false;
    }

    Parcel p;
    if (!parcels->findParcel(a.parcelID, p)) {
        why = "parcel " + a.parcelID + " no longer exists";
        return false;
    }
    if (p.status != a.newStatus) {
        why = "parcel " + a.parcelID + " is now '" + p.status + "', not '" + a.newStatus
            + "': it changed after this activity";
        return false;
    }

    if (a.type != ACT_ASSIGNED) {                  // Sorted, StatusUpdated, Missing
        if (a.oldStatus == NONE || a.oldStatus == a.newStatus) {
            why = "it recorded no status change to restore";
            return false;
        }
        return true;
    }

    // Assigned: the request must still be Assigned, and the resource must still hold it
    if (a.requestID == NONE) {
        why = "it has no request recorded";
        return false;
    }
    ServiceRequest r;
    if (!requests->findRequest(a.requestID, r)) {
        why = "request " + a.requestID + " no longer exists";
        return false;
    }
    if (r.status != STATUS_ASSIGNED) {
        why = "request " + a.requestID + " is " + r.status + ", not Assigned";
        return false;
    }
    if (a.resourceID != NONE) {
        Resource* res = resources->findResource(a.resourceID);
        if (res == nullptr) {
            why = "resource " + a.resourceID + " no longer exists";
            return false;
        }
        if (res->status != OCCUPIED || res->currentAssignment != a.requestID) {
            why = "resource " + a.resourceID + " no longer holds request " + a.requestID;
            return false;
        }
    }
    return true;
}

// Order of work: validate everything, restore the parcel first (it is the step
// that can fail on a file write, and nothing else has changed yet), then free
// the resource and put the request back to Pending, then record the reversal.
bool ActivityModule::undoLatest(std::string& msg) {
    std::string why;
    Activity* t = liveTarget(why, nullptr);
    if (t == nullptr) { msg = why; return false; }

    Activity a = *t;                               // copy: used after logging
    std::string label = a.activityID + " (" + a.type + ", parcel " + a.parcelID + ")";
    if (!canUndo(a, why)) {
        msg = "Cannot undo " + label + ": " + why + ".";
        return false;
    }

    std::string done, detail;
    bool parcelChanged = (a.oldStatus != NONE && a.oldStatus != a.newStatus);

    // 1. the parcel goes back to its earlier status
    if (parcelChanged) {
        if (!parcels->restoreParcelStatus(a.parcelID, a.oldStatus, detail)) {
            msg = "Cannot undo " + label + ": " + detail;
            return false;                          // nothing has been changed
        }
        done = "parcel " + a.parcelID + " is " + a.oldStatus + " again";
    }

    // 2. an assignment also frees its resource and re-opens its request
    if (a.type == ACT_ASSIGNED) {
        if (a.resourceID != NONE) {
            // Task 3 prints a warning ("needs a new resource") that would only
            // confuse here, so its console output is swallowed for this call
            std::ostringstream sink;
            std::streambuf* saved = std::cout.rdbuf(sink.rdbuf());
            bool freed = resources->updateResourceStatus(a.resourceID, AVAILABLE);
            std::cout.rdbuf(saved);
            if (!freed) {
                if (parcelChanged) parcels->restoreParcelStatus(a.parcelID, a.newStatus, detail);
                msg = "Cannot undo " + label + ": resource " + a.resourceID + " could not be freed.";
                return false;
            }
            if (!resourcesPath.empty() && !resources->saveToCSV(resourcesPath))
                done += (done.empty() ? "" : ", ") + std::string("[Warning: resources.csv not saved]");
            done += (done.empty() ? "" : ", ") + std::string("resource ") + a.resourceID + " is Available";
        }
        // The Main.cpp hook normally puts the request back to Pending when the
        // resource is freed; if it did not, do it here
        ServiceRequest r;
        if (requests->findRequest(a.requestID, r) && r.status == STATUS_ASSIGNED) {
            if (!requests->restoreRequestStatus(a.requestID, STATUS_PENDING, detail)) {
                msg = "Undo of " + label + " is only partly done: " + done
                    + ", but request " + a.requestID + " could not be set to Pending (" + detail + ").";
                return false;
            }
        }
        done += (done.empty() ? "" : ", ") + std::string("request ") + a.requestID + " is Pending again";
    }

    // 3. record it: the original stays in the history, marked reversed
    t->reversed = true;
    bool logged = logActivity(a.parcelID, ACT_UNDONE, a.newStatus,
        parcelChanged ? a.oldStatus : a.newStatus, a.requestID, a.resourceID);

    msg = "Undone " + label + ": " + done + ".";
    if (!logged) msg += " [Warning: " + lastMessage + "]";
    return true;
}

// ============================================================================
//  ActivityModule - menu (console I/O)
// ============================================================================
void ActivityModule::showMenu() const {
    std::cout << "\n==============================================================\n"
        << "  PARCEL ACTIVITY & RECORD MANAGEMENT  (Task 4)\n"
        << "==============================================================\n"
        << "  1. Display recent activities\n"
        << "  2. Record an activity by hand\n"
        << "  3. History of one parcel\n"
        << "  4. Review a parcel\n"
        << "  5. Undo the most recent activity\n"
        << "  0. Back\n"
        << "--------------------------------------------------------------\n";
}

void ActivityModule::run() {
    int choice;
    do {
        showMenu();
        std::cout << "Enter choice: ";
        choice = readChoice();
        switch (choice) {
        case 1: menuRecent();  break;
        case 2: menuRecord();  break;
        case 3: menuParcelHistory(); break;
        case 4: menuReview();  break;
        case 5: menuUndo();    break;
        case 0: break;
        default: std::cout << "Invalid choice.\n"; break;
        }
    } while (choice != 0);
}

void ActivityModule::menuRecent() const {
    std::string s = ask("How many recent activities (1-50, Enter = 10): ");
    int n = 10;
    if (!s.empty()) {
        if (!allDigits(s) || s.size() > 3 || std::atoi(s.c_str()) < 1 || std::atoi(s.c_str()) > 50) {
            std::cout << "[Error] Enter a number from 1 to 50.\n";
            return;
        }
        n = std::atoi(s.c_str());
    }
    displayRecent(n);
}

void ActivityModule::menuParcelHistory() const {
    displayParcelHistory(ask("Parcel ID (e.g. P001): "));
}

void ActivityModule::menuReview() const {
    reviewParcel(ask("Parcel ID (e.g. P001): "));
}

// Shows what undo would reverse, says why if it cannot, and asks before doing it
void ActivityModule::menuUndo() {
    std::string why;
    int skipped = 0;
    const Activity* t = undoTarget(why, &skipped);
    if (t == nullptr) {
        std::cout << "[Info] " << why << "\n";
        return;
    }

    std::cout << "\nMost recent activity still in effect:\n"
        << "  " << t->activityID << "  " << t->timestamp << "  parcel " << t->parcelID
        << "  " << t->type << "\n"
        << "  parcel status " << t->oldStatus << " -> " << t->newStatus
        << "   request " << t->requestID << "   resource " << t->resourceID << "\n";
    if (skipped > 0)
        std::cout << "  (Newer activities were already reversed, so this is the next one in line.)\n";

    if (!canUndo(*t, why)) {
        std::cout << "[Cannot undo] " << why << ".\n";
        return;
    }
    std::string answer = ask("Undo this activity? (y/n): ");
    if (answer != "y" && answer != "Y") {
        std::cout << "[Info] Nothing changed.\n";
        return;
    }
    std::string msg;
    bool ok = undoLatest(msg);
    std::cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}

// Lets staff type an activity in. The other modules do not report to Task 4
// yet, so this is how the history is added to until they do.
void ActivityModule::menuRecord() {
    std::cout << "Types: Registered, Sorted, StatusUpdated, InfoUpdated, Missing, Removed,\n"
        << "       RequestCreated, Assigned, Completed, Cancelled, Undone\n";
    std::string parcelID = ask("Parcel ID (e.g. P001): ");
    std::string type = ask("Activity type: ");
    std::string oldStatus = ask("Parcel status BEFORE (- if none): ");
    std::string newStatus = ask("Parcel status AFTER: ");
    std::string requestID = ask("Request ID (Enter = none): ");
    std::string resourceID = ask("Resource ID (Enter = none): ");

    bool ok = logActivity(parcelID, type, oldStatus, newStatus, requestID, resourceID);
    std::cout << (ok ? "[OK] " : "[Error] ") << lastMessage << "\n";
}
