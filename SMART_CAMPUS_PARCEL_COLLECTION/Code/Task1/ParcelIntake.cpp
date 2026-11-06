// ============================================================================
// ParcelIntake.cpp
// Task 1 - Parcel Intake, Sorting and Processing Module
// ============================================================================
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS     // lets Visual Studio accept localtime()
#endif

#include "ParcelIntake.hpp"
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

    const int STATUS_COUNT = 11;
    const std::string ALL_STATUSES[STATUS_COUNT] = {
        PARCEL_REGISTERED, PARCEL_READY_FOR_COLLECTION, PARCEL_ASSIGNED_FOR_DELIVERY,
        PARCEL_SPECIAL_HANDLING, PARCEL_RETURN_TO_SENDER, PARCEL_ON_HOLD, PARCEL_MISSING,
        PARCEL_COLLECTED, PARCEL_DELIVERED, PARCEL_RETURNED, PARCEL_CANCELLED
    };

    const int PARCEL_TYPE_COUNT = 4;
    const std::string PARCEL_TYPES[PARCEL_TYPE_COUNT] = {
        "Standard", "Fragile", "Urgent", "Oversized"
    };

    const int COURIER_COUNT = 6;
    const std::string COURIERS[COURIER_COUNT] = {
        "J&T", "Pos Laju", "DHL", "Shopee Xpress", "Flash Express", "Ninja Van"
    };

    const std::string COUNTER = "Counter";      // delivery info for counter collection
    const std::string CSV_HEADER =
        "ParcelID,RecipientName,RecipientID,DeliveryInfo,ParcelType,Courier,ArrivalDateTime,Status";

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

    std::string toLower(std::string s) {
        for (size_t i = 0; i < s.size(); i++)
            s[i] = (char)std::tolower((unsigned char)s[i]);
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

    // ---- validation --------------------------------------------------------

    bool isKnownStatus(const std::string& s) {
        for (int i = 0; i < STATUS_COUNT; i++)
            if (ALL_STATUSES[i] == s) return true;
        return false;
    }

    // Completed parcels can never be processed again
    bool isCompletedStatus(const std::string& s) {
        return s == PARCEL_COLLECTED || s == PARCEL_DELIVERED ||
            s == PARCEL_RETURNED || s == PARCEL_CANCELLED;
    }

    // "P" followed by 3 to 6 digits, e.g. P001, P1234
    bool isValidParcelID(const std::string& id) {
        return id.size() >= 4 && id.size() <= 7 && id[0] == 'P' && allDigits(id.substr(1));
    }

    // Student: TP + 6 digits (TP012345). Staff: STAFF + 3 digits (STAFF021).
    bool isValidRecipientID(const std::string& id) {
        if (id.size() == 8 && id.compare(0, 2, "TP") == 0) return allDigits(id.substr(2));
        if (id.size() == 8 && id.compare(0, 5, "STAFF") == 0) return allDigits(id.substr(5));
        return false;
    }

    // Letters, spaces, apostrophes, hyphens and dots; 2 to 40 characters.
    bool isValidName(const std::string& name) {
        if (name.size() < 2 || name.size() > 40) return false;
        int letters = 0;
        for (size_t i = 0; i < name.size(); i++) {
            unsigned char c = (unsigned char)name[i];
            if (std::isalpha(c)) letters++;
            else if (c != ' ' && c != '\'' && c != '-' && c != '.') return false;
        }
        return letters >= 2;
    }

    // Free text that goes into the CSV: printable, no comma (it would split
    // the field), not too long.
    bool isValidText(const std::string& s, size_t maxLen) {
        if (s.empty() || s.size() > maxLen) return false;
        for (size_t i = 0; i < s.size(); i++) {
            unsigned char c = (unsigned char)s[i];
            if (c == ',' || c < 32 || c > 126) return false;
        }
        return true;
    }

    bool checkRecipient(const std::string& name, const std::string& id, std::string& msg) {
        if (!isValidName(name)) {
            msg = "Invalid recipient name '" + name + "': use 2-40 letters, spaces, ' - or . only.";
            return false;
        }
        if (!isValidRecipientID(id)) {
            msg = "Invalid recipient ID '" + id + "': expected TP + 6 digits (student) or STAFF + 3 digits (staff).";
            return false;
        }
        return true;
    }

    // "counter" in any case becomes "Counter"; anything else is a location
    std::string normaliseDeliveryInfo(const std::string& s) {
        std::string t = trim(s);
        return (toUpper(t) == "COUNTER") ? COUNTER : t;
    }

    // Matches a parcel type ignoring case. Returns "" if it is not one of ours.
    std::string normaliseParcelType(const std::string& s) {
        std::string t = toUpper(trim(s));
        for (int i = 0; i < PARCEL_TYPE_COUNT; i++)
            if (toUpper(PARCEL_TYPES[i]) == t) return PARCEL_TYPES[i];
        return "";
    }

    bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

    // Strict "YYYY-MM-DD HH:MM" check with real calendar dates
    bool isValidDateTime(const std::string& s) {
        if (s.size() != 16 || s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':')
            return false;
        const int digitPos[12] = { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15 };
        for (int i = 0; i < 12; i++)
            if (!std::isdigit((unsigned char)s[digitPos[i]])) return false;

        int year = std::atoi(s.substr(0, 4).c_str());
        int month = std::atoi(s.substr(5, 2).c_str());
        int day = std::atoi(s.substr(8, 2).c_str());
        int hour = std::atoi(s.substr(11, 2).c_str());
        int minute = std::atoi(s.substr(14, 2).c_str());

        const int daysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (year < 2000 || month < 1 || month > 12) return false;
        int maxDay = daysInMonth[month - 1] + ((month == 2 && isLeapYear(year)) ? 1 : 0);
        return day >= 1 && day <= maxDay && hour <= 23 && minute <= 59;
    }

    // ---- business rules ----------------------------------------------------

    // Which status may follow 'from' when staff update a parcel by hand.
    // (Registered parcels are routed through the intake queue instead.)
    bool isAllowedChange(const std::string& from, const std::string& to) {
        if (from == to || isCompletedStatus(from) || to == PARCEL_REGISTERED) return false;
        if (from == PARCEL_MISSING) return to == PARCEL_ON_HOLD;          // found again
        if (to == PARCEL_MISSING) return true;                              // any active parcel can go missing
        if (from == PARCEL_REGISTERED) return to == PARCEL_CANCELLED;       // otherwise sort it first
        if (from == PARCEL_RETURN_TO_SENDER)
            return to == PARCEL_RETURNED || to == PARCEL_ON_HOLD;
        if (to == PARCEL_RETURNED) return false;                            // only after ReturnToSender
        if (to == PARCEL_COLLECTED) return from == PARCEL_READY_FOR_COLLECTION;
        if (to == PARCEL_DELIVERED) return from == PARCEL_ASSIGNED_FOR_DELIVERY;
        return true;   // between the sorted statuses, OnHold, ReturnToSender and Cancelled
    }

    // Statuses where the delivery information can still change
    bool canChangeDelivery(const std::string& status) {
        return status == PARCEL_REGISTERED || status == PARCEL_READY_FOR_COLLECTION ||
            status == PARCEL_ASSIGNED_FOR_DELIVERY || status == PARCEL_SPECIAL_HANDLING ||
            status == PARCEL_ON_HOLD;
    }

    // Route that keeps a Task 2 service request valid. A Return request is
    // completed by Task 2 from OnHold, so the parcel is held until then.
    bool routeForService(const std::string& serviceType, ParcelRoute& route) {
        if (serviceType == "Collection") { route = ROUTE_COLLECTION; return true; }
        if (serviceType == "CampusDelivery" || serviceType == "ReDelivery") { route = ROUTE_DELIVERY; return true; }
        if (serviceType == "SpecialHandling") { route = ROUTE_SPECIAL_HANDLING; return true; }
        if (serviceType == "Return") { route = ROUTE_HOLD; return true; }
        return false;
    }

    // ---- console input -----------------------------------------------------

    std::string readLine(const std::string& prompt) {
        std::cout << prompt;
        std::string s;
        std::getline(std::cin, s);
        return trim(s);
    }

    // Re-asks until a whole number in [lo, hi] is typed. At end of input it
    // returns lo (0 = cancel / exit) so the program can never loop forever.
    int readInt(const std::string& prompt, int lo, int hi) {
        while (true) {
            std::string s = readLine(prompt);
            if (!std::cin) return lo;
            if (allDigits(s) && s.size() <= 4) {
                int v = std::atoi(s.c_str());
                if (v >= lo && v <= hi) return v;
            }
            std::cout << "  Invalid input. Please enter a number from " << lo << " to " << hi << ".\n";
        }
    }

    // Same as readInt, but pressing Enter picks 'def'
    int readIntDefault(const std::string& prompt, int lo, int hi, int def) {
        while (true) {
            std::string s = readLine(prompt);
            if (!std::cin) return lo;
            if (s.empty()) return def;
            if (allDigits(s) && s.size() <= 4) {
                int v = std::atoi(s.c_str());
                if (v >= lo && v <= hi) return v;
            }
            std::cout << "  Invalid input. Please enter a number from " << lo << " to " << hi << ".\n";
        }
    }

    bool confirm(const std::string& prompt) {
        std::string answer = readLine(prompt + " (y/n): ");
        return answer == "y" || answer == "Y";
    }

    // ---- console output ----------------------------------------------------

    void printTableHeader() {
        std::cout << std::left
            << std::setw(5) << "No"
            << std::setw(8) << "Parcel"
            << std::setw(20) << "Recipient"
            << std::setw(10) << "Rec. ID"
            << std::setw(16) << "Delivery"
            << std::setw(11) << "Type"
            << std::setw(15) << "Courier"
            << std::setw(18) << "Arrived"
            << "Status\n"
            << std::string(122, '-') << "\n";
    }

    void printTableRow(int no, const Parcel& p) {
        std::cout << std::left
            << std::setw(5) << no
            << std::setw(8) << p.parcelID
            << std::setw(20) << fit(p.recipientName, 19)
            << std::setw(10) << p.recipientID
            << std::setw(16) << fit(p.deliveryInfo, 15)
            << std::setw(11) << p.parcelType
            << std::setw(15) << fit(p.courier, 14)
            << std::setw(18) << p.arrivalDateTime
            << p.status << "\n";
    }

} // end of helper namespace

// ============================================================================
//  Routes
// ============================================================================
std::string parcelRouteStatus(ParcelRoute route) {
    switch (route) {
    case ROUTE_COLLECTION:       return PARCEL_READY_FOR_COLLECTION;
    case ROUTE_DELIVERY:         return PARCEL_ASSIGNED_FOR_DELIVERY;
    case ROUTE_SPECIAL_HANDLING: return PARCEL_SPECIAL_HANDLING;
    case ROUTE_RETURN:           return PARCEL_RETURN_TO_SENDER;
    case ROUTE_HOLD:             return PARCEL_ON_HOLD;
    }
    return "";
}

std::string parcelRouteLabel(ParcelRoute route) {
    switch (route) {
    case ROUTE_COLLECTION:       return "Recipient collection";
    case ROUTE_DELIVERY:         return "Campus delivery";
    case ROUTE_SPECIAL_HANDLING: return "Special handling";
    case ROUTE_RETURN:           return "Return to sender";
    case ROUTE_HOLD:             return "Hold for investigation";
    }
    return "";
}

// ============================================================================
//  ParcelStore  (dynamic array kept in Parcel ID order)
// ============================================================================

// Does Parcel ID a come before b? Shorter IDs first, so P999 comes before P1000.
static bool idBefore(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}

ParcelStore::ParcelStore() : items(nullptr), count(0), capacity(0) {}

ParcelStore::~ParcelStore() { delete[] items; }

void ParcelStore::grow() {
    int newCap = (capacity == 0) ? 64 : capacity * 2;
    Parcel* bigger = new Parcel[newCap];
    for (int i = 0; i < count; i++) bigger[i] = items[i];
    delete[] items;
    items = bigger;
    capacity = newCap;
}

// Binary search: halve the search range each step -> O(log n)
int ParcelStore::find(const std::string& parcelID) const {
    int low = 0, high = count - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (items[mid].parcelID == parcelID) return mid;
        if (idBefore(items[mid].parcelID, parcelID)) low = mid + 1;   // target is to the right
        else high = mid - 1;                                         // target is to the left
    }
    return -1;
}

// Binary search for the first slot whose ID is not smaller, then shift the
// rest one place right. A new largest ID lands at the end with no shifting.
int ParcelStore::insert(const Parcel& p) {
    int low = 0, high = count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (idBefore(items[mid].parcelID, p.parcelID)) low = mid + 1;
        else high = mid;
    }
    if (low < count && items[low].parcelID == p.parcelID) return -1;   // duplicate Parcel ID

    if (count == capacity) grow();
    for (int i = count; i > low; i--) items[i] = items[i - 1];
    items[low] = p;
    count++;
    return low;
}

void ParcelStore::removeAt(int i) {
    if (i < 0 || i >= count) return;
    for (int j = i; j < count - 1; j++) items[j] = items[j + 1];
    count--;
}

void ParcelStore::swapWith(ParcelStore& other) {
    Parcel* tItems = items; items = other.items; other.items = tItems;
    int tCount = count; count = other.count; other.count = tCount;
    int tCap = capacity; capacity = other.capacity; other.capacity = tCap;
}

// Next free ID = highest number in use + 1 (P041 after P040)
std::string ParcelStore::nextID() const {
    int maxNum = 0;
    for (int i = 0; i < count; i++) {
        int n = std::atoi(items[i].parcelID.substr(1).c_str());
        if (n > maxNum) maxNum = n;
    }
    std::ostringstream ss;
    ss << "P" << std::setw(3) << std::setfill('0') << (maxNum + 1);
    return ss.str();
}

// ============================================================================
//  IntakeQueue  (FIFO queue on a singly linked list)
// ============================================================================
IntakeQueue::IntakeQueue() : front(nullptr), rear(nullptr), count(0) {}

IntakeQueue::~IntakeQueue() { clear(); }

// enqueue: link a new node after the rear -> O(1)
void IntakeQueue::enqueue(const std::string& parcelID) {
    Node* node = new Node;
    node->parcelID = parcelID;
    node->next = nullptr;
    if (rear == nullptr) front = node;    // queue was empty: node is also the front
    else rear->next = node;
    rear = node;
    count++;
}

// dequeue: unlink the front node -> O(1)
bool IntakeQueue::dequeue(std::string& parcelID) {
    if (front == nullptr) return false;
    Node* old = front;
    parcelID = old->parcelID;
    front = old->next;
    if (front == nullptr) rear = nullptr; // queue is now empty
    delete old;
    count--;
    return true;
}

bool IntakeQueue::peek(std::string& parcelID) const {
    if (front == nullptr) return false;
    parcelID = front->parcelID;
    return true;
}

// remove: walk to the node, then link its neighbours together -> O(n).
// Used only for exceptions (a waiting parcel is cancelled or goes missing).
bool IntakeQueue::remove(const std::string& parcelID) {
    Node* prev = nullptr;
    Node* cur = front;
    while (cur != nullptr && cur->parcelID != parcelID) {
        prev = cur;
        cur = cur->next;
    }
    if (cur == nullptr) return false;

    if (prev == nullptr) front = cur->next;   // removing the front
    else prev->next = cur->next;
    if (cur == rear) rear = prev;             // removing the rear
    delete cur;
    count--;
    return true;
}

int IntakeQueue::position(const std::string& parcelID) const {
    int pos = 1;
    for (Node* cur = front; cur != nullptr; cur = cur->next, pos++)
        if (cur->parcelID == parcelID) return pos;
    return 0;
}

int IntakeQueue::toArray(std::string* out, int max) const {
    int n = 0;
    for (Node* cur = front; cur != nullptr && n < max; cur = cur->next)
        out[n++] = cur->parcelID;
    return n;
}

void IntakeQueue::clear() {
    while (front != nullptr) {
        Node* next = front->next;
        delete front;
        front = next;
    }
    rear = nullptr;
    count = 0;
}

// ============================================================================
//  ParcelModule - file I/O and keeping in step with the other modules
// ============================================================================
ParcelModule::ParcelModule(const std::string& parcelsFile, const std::string& requestsFile)
    : parcelsPath(parcelsFile), requestsPath(requestsFile) {
}

// Checks one row of parcels.csv. Bad rows are skipped, never crash.
static bool parseParcelRow(const std::string& line, Parcel& p, std::string& reason) {
    std::string f[8];
    if (splitCSV(line, f, 8) != 8) { reason = "expected 8 fields"; return false; }
    for (int i = 0; i < 8; i++)
        if (f[i].empty()) { reason = "missing field"; return false; }

    std::string id = toUpper(f[0]);
    if (!isValidParcelID(id)) { reason = "invalid Parcel ID '" + f[0] + "'"; return false; }
    if (!isKnownStatus(f[7])) { reason = "unknown status '" + f[7] + "'"; return false; }

    p.parcelID = id;               p.recipientName = f[1];
    p.recipientID = toUpper(f[2]); p.deliveryInfo = f[3];
    p.parcelType = f[4];           p.courier = f[5];
    p.arrivalDateTime = f[6];      p.status = f[7];
    return true;
}

bool ParcelModule::readFile(ParcelStore& into, bool verbose, int& loaded, int& skipped) const {
    loaded = skipped = 0;
    std::ifstream in(parcelsPath.c_str());
    if (!in) return false;

    std::string line, reason;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo++;
        if (lineNo == 1) continue;                 // header row
        line = trim(line);
        if (line.empty()) continue;

        Parcel p;
        if (!parseParcelRow(line, p, reason)) {
            if (verbose) std::cout << "[Skipped] parcels.csv line " << lineNo << ": " << reason << "\n";
            skipped++;
            continue;
        }
        if (into.insert(p) < 0) {                  // binary insertion rejects duplicates
            if (verbose) std::cout << "[Skipped] parcels.csv line " << lineNo
                << ": duplicate Parcel ID " << p.parcelID << "\n";
            skipped++;
            continue;
        }
        loaded++;
    }
    return true;
}

// Reads parcels.csv into memory and repairs the intake queue WITHOUT
// changing its order. If the file cannot be read, memory is left as it is.
bool ParcelModule::reload(bool verbose) {
    ParcelStore fresh;
    int loaded = 0, skipped = 0;
    if (!readFile(fresh, verbose, loaded, skipped)) return false;
    store.swapWith(fresh);

    int n = store.size();
    bool* queued = new bool[n > 0 ? n : 1]();   // queued[i]: parcel i is in the queue

    // Step 1: one full rotation through the queue. Each parcel is dequeued
    // and joins the rear again only if it still exists and is still
    // Registered (Task 2 or another module may have moved it on).
    int waiting = intake.size();
    for (int i = 0; i < waiting; i++) {
        std::string id;
        intake.dequeue(id);
        int idx = store.find(id);
        if (idx >= 0 && !queued[idx] && store.at(idx).status == PARCEL_REGISTERED) {
            queued[idx] = true;
            intake.enqueue(id);
        }
    }
    // Step 2: Registered parcels that are not queued yet (first load, or
    // added from outside) join the rear in Parcel ID (= registration) order.
    for (int i = 0; i < n; i++)
        if (!queued[i] && store.at(i).status == PARCEL_REGISTERED)
            intake.enqueue(store.at(i).parcelID);
    delete[] queued;

    if (verbose)
        std::cout << "Loaded " << loaded << " parcel(s), " << intake.size()
        << " waiting in the intake queue, " << skipped << " row(s) skipped.\n";
    return true;
}

bool ParcelModule::load() {
    if (reload(true)) return true;
    std::cout << "[Warning] " << parcelsPath << " not found. Starting with no parcels.\n";
    return false;
}

bool ParcelModule::save() const {
    std::ofstream out(parcelsPath.c_str());
    if (!out) return false;
    out << CSV_HEADER << "\n";
    for (int i = 0; i < store.size(); i++) {
        const Parcel& p = store.at(i);
        out << p.parcelID << ',' << p.recipientName << ',' << p.recipientID << ','
            << p.deliveryInfo << ',' << p.parcelType << ',' << p.courier << ','
            << p.arrivalDateTime << ',' << p.status << '\n';
    }
    return out.good();
}

// Every change is saved straight away. If the file cannot be written, the
// change is dropped by re-reading the file, so memory never disagrees with it.
bool ParcelModule::commit() {
    if (save()) return true;
    reload(false);
    return false;
}

// Looks in Task 2's requests.csv (read only) for a Pending or Assigned
// request on this parcel. Columns: RequestID,ParcelID,ServiceType,Urgency,RequestTime,Status
bool ParcelModule::findActiveRequest(const std::string& parcelID, std::string& requestID,
    std::string& serviceType, std::string& requestStatus) const {
    std::ifstream in(requestsPath.c_str());
    if (!in) return false;

    std::string line, f[6];
    bool header = true;
    while (std::getline(in, line)) {
        if (header) { header = false; continue; }
        if (splitCSV(trim(line), f, 6) != 6) continue;
        if (toUpper(f[1]) == parcelID && (f[5] == "Pending" || f[5] == "Assigned")) {
            requestID = toUpper(f[0]);
            serviceType = f[2];
            requestStatus = f[5];
            return true;
        }
    }
    return false;
}

// ============================================================================
//  ParcelModule - core operations (integration API)
// ============================================================================

// Req 1: register a new parcel (validation first, state change last)
bool ParcelModule::registerParcel(const Parcel& input, std::string& newID, std::string& msg) {
    Parcel p;
    p.recipientName = trim(input.recipientName);
    p.recipientID = toUpper(trim(input.recipientID));
    p.deliveryInfo = normaliseDeliveryInfo(input.deliveryInfo);
    p.parcelType = normaliseParcelType(input.parcelType);
    p.courier = trim(input.courier);
    p.arrivalDateTime = trim(input.arrivalDateTime);
    if (p.arrivalDateTime.empty()) p.arrivalDateTime = currentTimestamp();

    // 1. Missing information
    if (p.recipientName.empty() || p.recipientID.empty() || p.deliveryInfo.empty() ||
        trim(input.parcelType).empty() || p.courier.empty()) {
        msg = "Incomplete registration: recipient name, recipient ID, delivery information, "
            "parcel type and courier are all required.";
        return false;
    }
    // 2. Invalid information
    if (!checkRecipient(p.recipientName, p.recipientID, msg)) return false;
    if (!isValidText(p.deliveryInfo, 30)) {
        msg = "Invalid delivery information '" + p.deliveryInfo + "' (max 30 characters, no commas).";
        return false;
    }
    if (p.parcelType.empty()) {
        msg = "Invalid parcel type '" + trim(input.parcelType) + "' (Standard, Fragile, Urgent or Oversized).";
        return false;
    }
    if (!isValidText(p.courier, 20)) {
        msg = "Invalid courier name '" + p.courier + "' (max 20 characters, no commas).";
        return false;
    }
    if (!isValidDateTime(p.arrivalDateTime)) {
        msg = "Invalid arrival date/time '" + p.arrivalDateTime + "' (use YYYY-MM-DD HH:MM).";
        return false;
    }
    if (p.arrivalDateTime > currentTimestamp()) {
        msg = "Arrival date/time " + p.arrivalDateTime + " is in the future.";
        return false;
    }

    sync();

    // 3. One linear pass over the records:
    //    - a recipient ID must always belong to the same person
    //    - the same parcel must not be registered twice (scanned twice)
    for (int i = 0; i < store.size(); i++) {
        const Parcel& old = store.at(i);
        if (old.recipientID != p.recipientID) continue;
        if (toLower(old.recipientName) != toLower(p.recipientName)) {
            msg = "Invalid recipient information: " + p.recipientID + " belongs to '"
                + old.recipientName + "' (see parcel " + old.parcelID + ").";
            return false;
        }
        if (old.courier == p.courier && old.parcelType == p.parcelType &&
            old.deliveryInfo == p.deliveryInfo && old.arrivalDateTime == p.arrivalDateTime) {
            msg = "Duplicate registration: this parcel is already registered as "
                + old.parcelID + " (" + old.status + ").";
            return false;
        }
    }

    p.parcelID = store.nextID();
    p.status = PARCEL_REGISTERED;                   // initial status
    if (store.insert(p) < 0) {                      // safety net, should never happen
        msg = "Duplicate Parcel ID " + p.parcelID + ". Parcel not registered.";
        return false;
    }
    intake.enqueue(p.parcelID);                     // joins the rear of the intake queue
    if (!commit()) {
        msg = "Cannot write " + parcelsPath + ". Parcel not registered.";
        return false;
    }

    newID = p.parcelID;
    msg = "Parcel " + p.parcelID + " registered (Registered). It is number "
        + std::to_string(intake.size()) + " in the intake queue.";
    return true;
}

// Req 2: locate a parcel by ID (binary search)
bool ParcelModule::findParcel(const std::string& parcelID, Parcel& out) {
    sync();
    int idx = store.find(toUpper(trim(parcelID)));
    if (idx < 0) return false;
    out = store.at(idx);
    return true;
}

int ParcelModule::waitingCount() {
    sync();
    return intake.size();
}

// Req 3: the parcel at the front of the intake queue (queue unchanged)
bool ParcelModule::peekNextParcel(Parcel& out) {
    sync();
    std::string id;
    if (!intake.peek(id)) return false;
    out = store.at(store.find(id));    // sync() guarantees every queued ID exists
    return true;
}

// The sorting rule, in ONE place
ParcelRoute ParcelModule::suggestRoute(const Parcel& p, std::string& reason) const {
    std::string requestID, serviceType, requestStatus;
    ParcelRoute route;
    if (findActiveRequest(p.parcelID, requestID, serviceType, requestStatus) &&
        routeForService(serviceType, route)) {
        reason = "matches its active request " + requestID + " (" + serviceType + ")";
        return route;
    }
    if (p.parcelType == "Fragile") {
        reason = "fragile parcel";
        return ROUTE_SPECIAL_HANDLING;
    }
    if (p.deliveryInfo == COUNTER) {
        reason = "recipient collects at the counter";
        return ROUTE_COLLECTION;
    }
    reason = "deliver to " + p.deliveryInfo;
    return ROUTE_DELIVERY;
}

// Req 3: dequeue the front parcel and route it. 'expectedID' guards against
// the front having changed since staff looked at it ("" = no check).
bool ParcelModule::processNextParcel(const std::string& expectedID, ParcelRoute route,
    Parcel& sorted, std::string& msg) {
    if ((int)route < 0 || (int)route >= ROUTE_COUNT) {
        msg = "Invalid route.";
        return false;
    }
    sync();
    std::string id;
    if (!intake.peek(id)) {
        msg = "No parcels are waiting to be sorted (the intake queue is empty).";
        return false;
    }
    std::string expected = toUpper(trim(expectedID));
    if (!expected.empty() && expected != id) {
        msg = "Parcel " + expected + " is no longer at the front of the intake queue (front is "
            + id + "). Please try again.";
        return false;
    }

    int idx = store.find(id);
    intake.dequeue(id);                             // O(1): leaves the queue
    std::string newStatus = parcelRouteStatus(route);
    store.at(idx).status = newStatus;               // Registered -> next stage
    if (!commit()) {
        msg = "Cannot write " + parcelsPath + ". Parcel " + id + " not sorted.";
        return false;
    }
    sorted = store.at(idx);

    msg = "Parcel " + id + " sorted for " + parcelRouteLabel(route) + ": Registered -> "
        + newStatus + ". " + std::to_string(intake.size()) + " parcel(s) still waiting.";

    // Warn if an existing Task 2 request no longer fits the chosen route
    std::string requestID, serviceType, requestStatus;
    ParcelRoute expectedRoute;
    if (findActiveRequest(id, requestID, serviceType, requestStatus) &&
        routeForService(serviceType, expectedRoute) && expectedRoute != route)
        msg += " Note: request " + requestID + " (" + serviceType
        + ") does not match this route - review it in Task 2.";
    return true;
}

int ParcelModule::suggestedUrgency(const Parcel& p) const {
    if (p.parcelType == "Urgent")  return 3;
    if (p.parcelType == "Fragile") return 2;
    return 1;
}

// Req 4: change the status by hand, following the allowed transitions
bool ParcelModule::updateStatus(const std::string& parcelID, const std::string& newStatusIn,
    std::string& msg) {
    std::string id = toUpper(trim(parcelID));
    std::string newStatus = trim(newStatusIn);
    if (id.empty()) { msg = "Please enter a Parcel ID."; return false; }

    sync();
    int idx = store.find(id);
    if (idx < 0) { msg = "Parcel " + id + " does not exist."; return false; }
    if (!isKnownStatus(newStatus)) { msg = "Unknown status '" + newStatus + "'."; return false; }

    Parcel& p = store.at(idx);
    std::string oldStatus = p.status;
    if (isCompletedStatus(oldStatus)) {
        msg = "Parcel " + id + " is already completed (" + oldStatus + ") and cannot be processed again.";
        return false;
    }
    if (oldStatus == newStatus) { msg = "Parcel " + id + " is already " + oldStatus + "."; return false; }
    if (oldStatus == PARCEL_REGISTERED && newStatus != PARCEL_CANCELLED && newStatus != PARCEL_MISSING) {
        msg = "Parcel " + id + " has not been sorted yet (number " + std::to_string(intake.position(id))
            + " in the intake queue). Sort it to route it to " + newStatus + ".";
        return false;
    }
    if (!isAllowedChange(oldStatus, newStatus)) {
        msg = "Invalid status change for " + id + ": " + oldStatus + " -> " + newStatus + ".";
        return false;
    }

    // Keep Task 2 in step: a parcel with an active request is finished there
    std::string requestID, serviceType, requestStatus, note;
    if (findActiveRequest(id, requestID, serviceType, requestStatus)) {
        if (newStatus == PARCEL_COLLECTED || newStatus == PARCEL_DELIVERED || newStatus == PARCEL_RETURNED) {
            msg = "Parcel " + id + " has active request " + requestID + " (" + serviceType + ", "
                + requestStatus + "). Complete it in Task 2 so both records stay in step.";
            return false;
        }
        note = " Request " + requestID + " (" + serviceType + ", " + requestStatus
            + ") is still active - review it in Task 2.";
    }

    if (oldStatus == PARCEL_REGISTERED) intake.remove(id);   // keeps the queue invariant
    p.status = newStatus;
    if (!commit()) { msg = "Cannot write " + parcelsPath + ". Status not changed."; return false; }

    msg = "Parcel " + id + ": " + oldStatus + " -> " + newStatus + "." + note;
    return true;
}

bool ParcelModule::updateDeliveryInfo(const std::string& parcelID, const std::string& newInfoIn,
    std::string& msg) {
    std::string id = toUpper(trim(parcelID));
    std::string newInfo = normaliseDeliveryInfo(newInfoIn);

    sync();
    int idx = store.find(id);
    if (idx < 0) { msg = "Parcel " + id + " does not exist."; return false; }
    Parcel& p = store.at(idx);
    if (!canChangeDelivery(p.status)) {
        msg = "Delivery information of " + id + " cannot change while it is " + p.status + ".";
        return false;
    }
    if (!isValidText(newInfo, 30)) {
        msg = "Invalid delivery information '" + newInfo + "' (max 30 characters, no commas).";
        return false;
    }
    if (newInfo == p.deliveryInfo) { msg = "Parcel " + id + " already has delivery information '" + newInfo + "'."; return false; }

    std::string oldInfo = p.deliveryInfo;
    p.deliveryInfo = newInfo;
    if (!commit()) { msg = "Cannot write " + parcelsPath + ". Delivery information not changed."; return false; }

    msg = "Parcel " + id + " delivery information: '" + oldInfo + "' -> '" + newInfo + "'.";
    if (p.status == PARCEL_READY_FOR_COLLECTION && newInfo != COUNTER)
        msg += " Change its status to AssignedForDelivery if it should now be delivered.";
    if (p.status == PARCEL_ASSIGNED_FOR_DELIVERY && newInfo == COUNTER)
        msg += " Change its status to ReadyForCollection if it will now be collected.";
    return true;
}

// Exception: a parcel is reported missing (it leaves the queue if waiting)
bool ParcelModule::reportMissing(const std::string& parcelID, std::string& msg) {
    return updateStatus(parcelID, PARCEL_MISSING, msg);
}

// Removing deletes the record, so it is only allowed for a parcel that was
// registered by mistake (still Registered) or is Cancelled, and that no
// active Task 2 request still points at.
bool ParcelModule::removeParcel(const std::string& parcelID, std::string& msg) {
    std::string id = toUpper(trim(parcelID));
    sync();
    int idx = store.find(id);
    if (idx < 0) { msg = "Parcel " + id + " does not exist."; return false; }

    std::string status = store.at(idx).status;
    if (status != PARCEL_REGISTERED && status != PARCEL_CANCELLED) {
        msg = "Parcel " + id + " is " + status + ". Only Registered (entered by mistake) or "
            "Cancelled parcels can be removed; other records are kept as history.";
        return false;
    }
    std::string requestID, serviceType, requestStatus;
    if (findActiveRequest(id, requestID, serviceType, requestStatus)) {
        msg = "Parcel " + id + " still has active request " + requestID + " (" + serviceType + ", "
            + requestStatus + "). Cancel it in Task 2 first.";
        return false;
    }

    if (status == PARCEL_REGISTERED) intake.remove(id);
    store.removeAt(idx);
    if (!commit()) { msg = "Cannot write " + parcelsPath + ". Parcel not removed."; return false; }
    msg = "Parcel " + id + " removed.";
    return true;
}

// For Task 4 (undo). Bypasses the transition rules but keeps the queue right.
bool ParcelModule::restoreParcelStatus(const std::string& parcelID, const std::string& previousStatus,
    std::string& msg) {
    std::string id = toUpper(trim(parcelID));
    sync();
    int idx = store.find(id);
    if (idx < 0) { msg = "Parcel " + id + " does not exist."; return false; }
    if (!isKnownStatus(previousStatus)) { msg = "Invalid status '" + previousStatus + "'."; return false; }

    Parcel& p = store.at(idx);
    if (p.status == previousStatus) { msg = "Parcel " + id + " is already " + previousStatus + "."; return false; }

    if (p.status == PARCEL_REGISTERED) intake.remove(id);
    if (previousStatus == PARCEL_REGISTERED) intake.enqueue(id);
    p.status = previousStatus;
    if (!commit()) { msg = "Cannot write " + parcelsPath + ". Status not restored."; return false; }
    msg = "Parcel " + id + " restored to " + previousStatus + ".";
    return true;
}

// ============================================================================
//  ParcelModule - menu (console I/O)
// ============================================================================
void ParcelModule::showMenu() const {
    std::cout << "\n==============================================================\n"
        << "  PARCEL INTAKE, SORTING & PROCESSING  (Task 1)\n"
        << "==============================================================\n"
        << "   1. Register a new parcel\n"
        << "   2. Display parcel records\n"
        << "   3. Search for parcels\n"
        << "   4. View parcel details\n"
        << "   5. Display intake queue (" << intake.size() << " waiting to be sorted)\n"
        << "   6. Sort and process the next parcel\n"
        << "   7. Update parcel status\n"
        << "   8. Update delivery information\n"
        << "   9. Report a parcel as missing\n"
        << "  10. Remove a parcel record\n"
        << "   0. Back to main menu\n"
        << "--------------------------------------------------------------\n";
}

void ParcelModule::run() {
    int choice;
    do {
        sync();                            // pick up changes made by other modules
        showMenu();
        choice = readInt("Enter choice: ", 0, 10);
        switch (choice) {
        case 1:  menuRegister();       break;
        case 2:  menuDisplay();        break;
        case 3:  menuSearch();         break;
        case 4:  menuView();           break;
        case 5:  menuDisplayQueue();   break;
        case 6:  menuSort();           break;
        case 7:  menuUpdateStatus();   break;
        case 8:  menuUpdateDelivery(); break;
        case 9:  menuReportMissing();  break;
        case 10: menuRemove();         break;
        default: break;
        }
    } while (choice != 0);
    std::cout << "\nAll parcel changes are already saved to " << parcelsPath << ".\n";
}

void ParcelModule::printDetails(const Parcel& p) const {
    std::cout << "Parcel ID     : " << p.parcelID << "\n"
        << "Recipient     : " << p.recipientName << " (" << p.recipientID << ")\n"
        << "Delivery info : " << p.deliveryInfo << "\n"
        << "Parcel type   : " << p.parcelType << "\n"
        << "Courier       : " << p.courier << "\n"
        << "Arrived       : " << p.arrivalDateTime << "\n"
        << "Status        : " << p.status << "\n";

    std::string requestID, serviceType, requestStatus;
    if (findActiveRequest(p.parcelID, requestID, serviceType, requestStatus))
        std::cout << "Service req.  : " << requestID << " (" << serviceType << ", " << requestStatus << ")\n";
}

void ParcelModule::menuRegister() {
    std::cout << "\n--- Register New Parcel ---\n";
    Parcel in;
    std::string msg;
    in.recipientName = readLine("Recipient name                      : ");
    in.recipientID = toUpper(readLine("Recipient ID (TPxxxxxx or STAFFxxx) : "));
    if (in.recipientName.empty() || in.recipientID.empty()) {
        std::cout << "[Error] Incomplete registration: recipient name and ID are required.\n";
        return;
    }
    if (!checkRecipient(in.recipientName, in.recipientID, msg)) {   // fail early, before more questions
        std::cout << "[Error] " << msg << "\n";
        return;
    }

    std::cout << "Delivery option:\n  1. Collect at a counter\n  2. Deliver to a campus location\n";
    int d = readInt("Choose (1-2, 0 = cancel): ", 0, 2);
    if (d == 0) { std::cout << "[Info] Registration cancelled.\n"; return; }
    in.deliveryInfo = (d == 1) ? COUNTER : readLine("Campus location (e.g. Block A L3)   : ");

    std::cout << "Parcel type:\n";
    for (int i = 0; i < PARCEL_TYPE_COUNT; i++)
        std::cout << "  " << (i + 1) << ". " << PARCEL_TYPES[i] << "\n";
    int t = readInt("Choose (1-4, 0 = cancel): ", 0, PARCEL_TYPE_COUNT);
    if (t == 0) { std::cout << "[Info] Registration cancelled.\n"; return; }
    in.parcelType = PARCEL_TYPES[t - 1];

    std::cout << "Courier:\n";
    for (int i = 0; i < COURIER_COUNT; i++)
        std::cout << "  " << (i + 1) << ". " << COURIERS[i] << "\n";
    std::cout << "  " << (COURIER_COUNT + 1) << ". Other\n";
    int c = readInt("Choose (1-7, 0 = cancel): ", 0, COURIER_COUNT + 1);
    if (c == 0) { std::cout << "[Info] Registration cancelled.\n"; return; }
    in.courier = (c <= COURIER_COUNT) ? COURIERS[c - 1] : readLine("Courier name                        : ");

    in.arrivalDateTime = readLine("Arrival (YYYY-MM-DD HH:MM, Enter = now): ");

    std::string newID;
    bool ok = registerParcel(in, newID, msg);
    std::cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}

void ParcelModule::menuDisplay() {
    if (store.size() == 0) { std::cout << "\n[Info] No parcel records.\n"; return; }

    std::cout << "\nShow:\n  1. All parcels\n  2. Parcels with one status\n";
    int mode = readInt("Choose (1-2, 0 = cancel): ", 0, 2);
    if (mode == 0) return;

    std::string filter;
    if (mode == 2) {
        for (int i = 0; i < STATUS_COUNT; i++)
            std::cout << "  " << std::setw(3) << std::right << (i + 1) << ". " << ALL_STATUSES[i] << "\n";
        int s = readInt("Status (1-11, 0 = cancel): ", 0, STATUS_COUNT);
        if (s == 0) return;
        filter = ALL_STATUSES[s - 1];
    }

    int shown = 0;
    for (int i = 0; i < store.size(); i++) {
        if (!filter.empty() && store.at(i).status != filter) continue;
        if (shown == 0) {
            std::cout << "\n=== " << (filter.empty() ? "All parcels" : filter + " parcels") << " ===\n";
            printTableHeader();
        }
        printTableRow(++shown, store.at(i));
    }
    if (shown == 0) { std::cout << "\n[Info] No parcels with status " << filter << ".\n"; return; }

    std::cout << shown << " parcel(s).\n";
    if (filter.empty()) {                       // summary line: count per status
        std::cout << "By status:";
        for (int s = 0; s < STATUS_COUNT; s++) {
            int n = 0;
            for (int i = 0; i < store.size(); i++)
                if (store.at(i).status == ALL_STATUSES[s]) n++;
            if (n > 0) std::cout << "  " << ALL_STATUSES[s] << " " << n;
        }
        std::cout << "\n";
    }
}

void ParcelModule::menuSearch() {
    std::cout << "\nSearch by:\n"
        << "  1. Parcel ID\n"
        << "  2. Recipient ID (all parcels of one person)\n"
        << "  3. Recipient name (part of the name is enough)\n";
    int mode = readInt("Choose (1-3, 0 = cancel): ", 0, 3);
    if (mode == 0) return;

    if (mode == 1) {                            // binary search on the sorted store
        std::string id = toUpper(readLine("Parcel ID: "));
        int idx = store.find(id);
        if (idx < 0) { std::cout << "[Info] Parcel " << id << " does not exist.\n"; return; }
        printTableHeader();
        printTableRow(1, store.at(idx));
        return;
    }

    std::string key = (mode == 2) ? toUpper(readLine("Recipient ID: "))
        : toLower(readLine("Recipient name: "));
    if (key.empty()) { std::cout << "[Error] Please enter something to search for.\n"; return; }

    int found = 0;                              // linear search: not the sort key
    for (int i = 0; i < store.size(); i++) {
        const Parcel& p = store.at(i);
        bool match = (mode == 2) ? (p.recipientID == key)
            : (toLower(p.recipientName).find(key) != std::string::npos);
        if (!match) continue;
        if (found == 0) printTableHeader();
        printTableRow(++found, p);
    }
    if (found == 0) std::cout << "[Info] No parcel found for '" << key << "'.\n";
    else std::cout << found << " parcel(s) found.\n";
}

void ParcelModule::menuView() {
    std::string id = toUpper(readLine("\nParcel ID: "));
    int idx = store.find(id);
    if (idx < 0) { std::cout << "[Error] Parcel " << id << " does not exist.\n"; return; }

    const Parcel& p = store.at(idx);
    std::cout << "\n--- Parcel " << p.parcelID << " ---\n";
    printDetails(p);

    if (p.status == PARCEL_REGISTERED) {
        std::string reason;
        ParcelRoute route = suggestRoute(p, reason);
        std::cout << "Intake queue  : number " << intake.position(p.parcelID) << " of " << intake.size() << "\n"
            << "Suggested     : " << parcelRouteLabel(route) << " (" << reason << ")\n";
        return;
    }
    if (isCompletedStatus(p.status)) {
        std::cout << "Next actions  : none (parcel is completed)\n";
        return;
    }
    std::cout << "Next actions  :";
    for (int i = 0; i < STATUS_COUNT; i++)
        if (isAllowedChange(p.status, ALL_STATUSES[i])) std::cout << " " << ALL_STATUSES[i];
    std::cout << "\n";
}

void ParcelModule::menuDisplayQueue() {
    int n = intake.size();
    if (n == 0) { std::cout << "\n[Info] No parcels are waiting to be sorted.\n"; return; }

    std::string* ids = new std::string[n];
    intake.toArray(ids, n);
    std::cout << "\n=== Intake queue: " << n << " parcel(s), front first ===\n";
    printTableHeader();
    for (int i = 0; i < n; i++) printTableRow(i + 1, store.at(store.find(ids[i])));
    std::cout << "Next to be sorted: " << ids[0] << "\n";
    delete[] ids;
}

void ParcelModule::menuSort() {
    Parcel sorted;
    if (sortNextParcel(sorted) && sorted.status != PARCEL_RETURN_TO_SENDER && sorted.status != PARCEL_ON_HOLD)
        std::cout << "[Info] Create its service request in Task 2 when it is needed.\n";
}

// Shared by the Task 1 menu and Main.cpp's integrated option
bool ParcelModule::sortNextParcel(Parcel& sorted) {
    Parcel p;
    if (!peekNextParcel(p)) {
        std::cout << "\n[Info] No parcels are waiting to be sorted (the intake queue is empty).\n";
        return false;
    }

    std::cout << "\n--- Next parcel in the intake queue (" << intake.size() << " waiting) ---\n";
    printDetails(p);

    std::string reason;
    ParcelRoute suggested = suggestRoute(p, reason);
    std::cout << "\nSuggested route: " << parcelRouteLabel(suggested) << " (" << reason << ")\n";
    for (int i = 0; i < ROUTE_COUNT; i++)
        std::cout << "  " << (i + 1) << ". " << std::left << std::setw(24)
        << parcelRouteLabel((ParcelRoute)i) << "-> " << parcelRouteStatus((ParcelRoute)i) << "\n";
    std::cout << "  0. Cancel (parcel stays at the front)\n";

    int choice = readIntDefault("Choose route (Enter = " + std::to_string((int)suggested + 1) + "): ",
        0, ROUTE_COUNT, (int)suggested + 1);
    if (choice == 0) {
        std::cout << "[Info] Sorting cancelled. " << p.parcelID << " is still at the front.\n";
        return false;
    }

    std::string msg;
    bool ok = processNextParcel(p.parcelID, (ParcelRoute)(choice - 1), sorted, msg);
    std::cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
    return ok;
}

void ParcelModule::menuUpdateStatus() {
    std::string id = toUpper(readLine("\nParcel ID: "));
    int idx = store.find(id);
    if (idx < 0) { std::cout << "[Error] Parcel " << id << " does not exist.\n"; return; }

    std::string current = store.at(idx).status;
    if (isCompletedStatus(current)) {
        std::cout << "[Error] Parcel " << id << " is already completed (" << current
            << ") and cannot be processed again.\n";
        return;
    }

    // List only the statuses this parcel may move to
    std::string options[STATUS_COUNT];
    int n = 0;
    for (int i = 0; i < STATUS_COUNT; i++)
        if (isAllowedChange(current, ALL_STATUSES[i])) options[n++] = ALL_STATUSES[i];

    std::cout << "Current status: " << current << "\n";
    if (current == PARCEL_REGISTERED)
        std::cout << "(Not sorted yet - use 'Sort and process the next parcel' to route it.)\n";
    for (int i = 0; i < n; i++) std::cout << "  " << (i + 1) << ". " << options[i] << "\n";
    int c = readInt("New status (0 = cancel): ", 0, n);
    if (c == 0) return;

    std::string msg;
    bool ok = updateStatus(id, options[c - 1], msg);
    std::cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}

void ParcelModule::menuUpdateDelivery() {
    std::string id = toUpper(readLine("\nParcel ID: "));
    int idx = store.find(id);
    if (idx < 0) { std::cout << "[Error] Parcel " << id << " does not exist.\n"; return; }
    std::cout << "Current delivery information: " << store.at(idx).deliveryInfo
        << " (status " << store.at(idx).status << ")\n"
        << "  1. Collect at a counter\n  2. Deliver to a campus location\n";
    int d = readInt("Choose (1-2, 0 = cancel): ", 0, 2);
    if (d == 0) return;
    std::string info = (d == 1) ? COUNTER : readLine("Campus location (e.g. Block A L3): ");

    std::string msg;
    bool ok = updateDeliveryInfo(id, info, msg);
    std::cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}

void ParcelModule::menuReportMissing() {
    std::string id = toUpper(readLine("\nParcel ID reported missing: "));
    int idx = store.find(id);
    if (idx < 0) { std::cout << "[Error] Parcel " << id << " does not exist.\n"; return; }

    const Parcel& p = store.at(idx);
    if (!confirm("Mark " + p.parcelID + " (" + p.recipientName + ", " + p.status + ") as Missing?")) {
        std::cout << "[Info] Nothing changed.\n";
        return;
    }
    std::string msg;
    bool ok = reportMissing(id, msg);
    std::cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
    if (ok) std::cout << "[Info] If it is found, set its status to OnHold for investigation.\n";
}

void ParcelModule::menuRemove() {
    std::string id = toUpper(readLine("\nParcel ID to remove: "));
    int idx = store.find(id);
    if (idx < 0) { std::cout << "[Error] Parcel " << id << " does not exist.\n"; return; }

    const Parcel& p = store.at(idx);
    if (!confirm("Permanently remove " + p.parcelID + " (" + p.recipientName + ", " + p.status + ")?")) {
        std::cout << "[Info] Nothing removed.\n";
        return;
    }
    std::string msg;
    bool ok = removeParcel(id, msg);
    std::cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}
