// ============================================================================
// ServiceRequest.cpp
// Task 2 - Parcel Service and Collection Management Module
// ============================================================================
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS     // lets Visual Studio accept localtime()
#endif

#include "ServiceRequest.hpp"
#include <cctype>
#include <cstdlib>

// ============================================================================
//  Small helpers
// ============================================================================
static const int SERVICE_TYPE_COUNT = 5;
static const string SERVICE_TYPES[SERVICE_TYPE_COUNT] = {
    SERVICE_COLLECTION, SERVICE_CAMPUS_DELIVERY, SERVICE_SPECIAL_HANDLING,
    SERVICE_REDELIVERY, SERVICE_RETURN
};

static string trim(const string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static string toUpper(string s)
{
    for (size_t i = 0; i < s.size(); i++)
        s[i] = (char)toupper((unsigned char)s[i]);
    return s;
}

// Accepts an optional sign followed by 1-9 digits only.
static bool parseInt(const string& s, int& out)
{
    size_t i = 0;
    if (s.empty()) return false;
    if (s[0] == '-' || s[0] == '+') i = 1;
    if (i >= s.size() || s.size() - i > 9) return false;
    for (size_t j = i; j < s.size(); j++)
        if (!isdigit((unsigned char)s[j])) return false;
    out = atoi(s.c_str());
    return true;
}

// Splits a CSV line on commas into fields[] (at most maxFields stored).
// Returns the REAL number of fields so callers can detect malformed rows.
static int splitCSV(const string& line, string fields[], int maxFields)
{
    int n = 0;
    string cur;
    for (size_t i = 0; i < line.size(); i++)
    {
        if (line[i] == ',')
        {
            if (n < maxFields) fields[n] = trim(cur);
            n++;
            cur.clear();
        }
        else cur += line[i];
    }
    if (n < maxFields) fields[n] = trim(cur);
    n++;
    return n;
}

static string fit(const string& s, size_t width)   // truncate for table columns
{
    return s.size() > width ? s.substr(0, width) : s;
}

static string currentTimestamp()
{
    time_t t = time(nullptr);
    tm* lt = localtime(&t);
    char buf[32] = "";
    if (lt != nullptr) strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", lt);
    return string(buf);
}

static bool isValidServiceType(const string& s)
{
    for (int i = 0; i < SERVICE_TYPE_COUNT; i++)
        if (SERVICE_TYPES[i] == s) return true;
    return false;
}

static bool isValidStatus(const string& s)
{
    return s == STATUS_PENDING || s == STATUS_ASSIGNED ||
        s == STATUS_COMPLETED || s == STATUS_CANCELLED;
}

// ---- Business rules shared with Task 1 (agree on these with the team) ------

// Which parcel statuses allow a given service to be requested / processed.
// Terminal parcel statuses (Collected, Delivered, Returned, Cancelled, Missing)
// are never allowed, which blocks "processing an already completed parcel".
static bool parcelAllowsService(const string& service, const string& parcelStatus)
{
    bool registered = (parcelStatus == "Registered");
    if (service == SERVICE_COLLECTION)
        return registered || parcelStatus == "ReadyForCollection";
    if (service == SERVICE_CAMPUS_DELIVERY)
        return registered || parcelStatus == "AssignedForDelivery";
    if (service == SERVICE_SPECIAL_HANDLING)
        return registered || parcelStatus == "SpecialHandling";
    if (service == SERVICE_REDELIVERY)
        return registered || parcelStatus == "OnHold" || parcelStatus == "AssignedForDelivery";
    if (service == SERVICE_RETURN)
        return registered || parcelStatus == "ReadyForCollection" || parcelStatus == "OnHold";
    return false;
}

// Parcel status after a request of this type is COMPLETED.
static string parcelStatusAfterCompletion(const string& service)
{
    if (service == SERVICE_COLLECTION)       return "Collected";
    if (service == SERVICE_CAMPUS_DELIVERY)  return "Delivered";
    if (service == SERVICE_REDELIVERY)       return "Delivered";
    if (service == SERVICE_RETURN)           return "Returned";
    return "ReadyForCollection";             // SpecialHandling finished
}

// Parcel status change when a request becomes ASSIGNED ("" = no change).
static string parcelStatusOnAssignment(const string& service)
{
    if (service == SERVICE_CAMPUS_DELIVERY || service == SERVICE_REDELIVERY)
        return "AssignedForDelivery";
    if (service == SERVICE_SPECIAL_HANDLING)
        return "SpecialHandling";
    return "";
}

// ============================================================================
//  RequestStore
// ============================================================================
RequestStore::RequestStore() : items(nullptr), count(0), capacity(0) {}

RequestStore::~RequestStore() { delete[] items; }

void RequestStore::grow()
{
    int newCap = (capacity == 0) ? 16 : capacity * 2;
    ServiceRequest* bigger = new ServiceRequest[newCap];
    for (int i = 0; i < count; i++) bigger[i] = items[i];
    delete[] items;
    items = bigger;
    capacity = newCap;
}

int RequestStore::add(const ServiceRequest& r)
{
    if (count == capacity) grow();
    items[count] = r;
    return count++;
}

int RequestStore::findByID(const string& id) const
{
    for (int i = 0; i < count; i++)
        if (items[i].requestID == id) return i;
    return -1;
}

int RequestStore::findActiveByParcel(const string& parcelID) const
{
    for (int i = 0; i < count; i++)
        if (items[i].parcelID == parcelID &&
            (items[i].status == STATUS_PENDING || items[i].status == STATUS_ASSIGNED))
            return i;
    return -1;
}

string RequestStore::nextID() const
{
    int maxNum = 0, n = 0;
    for (int i = 0; i < count; i++)
    {
        const string& id = items[i].requestID;
        if (id.size() > 1 && toupper((unsigned char)id[0]) == 'R' &&
            parseInt(id.substr(1), n) && n > maxNum)
            maxNum = n;
    }
    ostringstream ss;
    ss << "R" << setw(3) << setfill('0') << (maxNum + 1);
    return ss.str();
}

// ============================================================================
//  RequestQueue  (priority queue: array in arrival order, as taught in lecture)
// ============================================================================
RequestQueue::RequestQueue(const RequestStore* s)
    : store(s), arr(nullptr), count(0), capacity(0) {
}

RequestQueue::~RequestQueue() { delete[] arr; }

// The priority rule lives here, in ONE place.
bool RequestQueue::higher(int a, int b) const
{
    const ServiceRequest& x = store->at(a);
    const ServiceRequest& y = store->at(b);
    if (x.urgency != y.urgency)         return x.urgency > y.urgency;         // 1
    if (x.requestTime != y.requestTime) return x.requestTime < y.requestTime; // 2
    if (x.requestID.size() != y.requestID.size())                             // 3
        return x.requestID.size() < y.requestID.size();
    return x.requestID < y.requestID;
}

void RequestQueue::grow()
{
    int newCap = (capacity == 0) ? 16 : capacity * 2;
    int* bigger = new int[newCap];
    for (int i = 0; i < count; i++) bigger[i] = arr[i];
    delete[] arr;
    arr = bigger;
    capacity = newCap;
}

// enqueue: add at the rear in arrival order, NO sorting -> O(1)
void RequestQueue::enqueue(int storeIndex)
{
    if (count == capacity) grow();
    arr[count++] = storeIndex;
}

// Scan EVERY item for the highest-priority one -> O(n)
int RequestQueue::findTopPos() const
{
    if (count == 0) return -1;
    int best = 0;
    for (int i = 1; i < count; i++)
        if (higher(arr[i], arr[best])) best = i;
    return best;
}

int RequestQueue::peek() const
{
    int pos = findTopPos();
    return (pos < 0) ? -1 : arr[pos];
}

void RequestQueue::removeAt(int pos)
{
    for (int i = pos; i < count - 1; i++)
        arr[i] = arr[i + 1];                       // shift the rest left
    count--;
}

// dequeue: scan all, find highest priority, remove it -> O(n)
int RequestQueue::dequeue()
{
    int pos = findTopPos();
    if (pos < 0) return -1;
    int result = arr[pos];
    removeAt(pos);
    return result;
}

// Cancelling a request that is not at the top: find it, then shift -> O(n)
bool RequestQueue::remove(int storeIndex)
{
    for (int i = 0; i < count; i++)
        if (arr[i] == storeIndex) { removeAt(i); return true; }
    return false;
}

// Priority order for display: repeatedly pick the best item from a COPY
// (selection style, O(n^2)) so the real queue is never disturbed.
void RequestQueue::snapshotSorted(int* out) const
{
    int n = count;
    int* tmp = new int[n > 0 ? n : 1];
    for (int i = 0; i < n; i++) tmp[i] = arr[i];

    for (int k = 0; k < count; k++)
    {
        int best = 0;
        for (int i = 1; i < n; i++)
            if (higher(tmp[i], tmp[best])) best = i;
        out[k] = tmp[best];
        for (int i = best; i < n - 1; i++) tmp[i] = tmp[i + 1];
        n--;
    }
    delete[] tmp;
}

// ============================================================================
//  ParcelTable  (reads / rewrites parcels.csv)
// ============================================================================
ParcelTable::ParcelTable() : items(nullptr), count(0), capacity(0) {}

ParcelTable::~ParcelTable() { delete[] items; }

void ParcelTable::grow()
{
    int newCap = (capacity == 0) ? 64 : capacity * 2;
    ParcelRecord* bigger = new ParcelRecord[newCap];
    for (int i = 0; i < count; i++) bigger[i] = items[i];
    delete[] items;
    items = bigger;
    capacity = newCap;
}

bool ParcelTable::load(const string& file)
{
    ifstream in(file.c_str());
    if (!in) return false;

    delete[] items;
    items = nullptr; count = 0; capacity = 0;

    string line, f[8];
    bool header = true;
    while (getline(in, line))
    {
        if (header) { header = false; continue; }
        line = trim(line);
        if (line.empty()) continue;
        if (splitCSV(line, f, 8) != 8) continue;   // skip malformed rows

        if (count == capacity) grow();
        ParcelRecord p;
        p.parcelID = f[0];     p.recipientName = f[1]; p.recipientID = f[2];
        p.deliveryInfo = f[3]; p.parcelType = f[4];    p.courier = f[5];
        p.arrivalDateTime = f[6]; p.status = f[7];
        items[count++] = p;
    }
    return true;
}

bool ParcelTable::save(const string& file) const
{
    ofstream out(file.c_str());
    if (!out) return false;
    out << "ParcelID,RecipientName,RecipientID,DeliveryInfo,ParcelType,Courier,ArrivalDateTime,Status\n";
    for (int i = 0; i < count; i++)
        out << items[i].parcelID << ',' << items[i].recipientName << ','
        << items[i].recipientID << ',' << items[i].deliveryInfo << ','
        << items[i].parcelType << ',' << items[i].courier << ','
        << items[i].arrivalDateTime << ',' << items[i].status << '\n';
    return out.good();
}

int ParcelTable::find(const string& parcelID) const
{
    for (int i = 0; i < count; i++)
        if (items[i].parcelID == parcelID) return i;
    return -1;
}

// ============================================================================
//  Console input / output helpers
// ============================================================================
static string readLine(const string& prompt)
{
    cout << prompt;
    string s;
    getline(cin, s);
    return trim(s);
}

// Re-asks until a whole number in [lo, hi] is typed. At end of input it
// returns lo (menu choice 0 = exit) so the program can never loop forever.
static int readInt(const string& prompt, int lo, int hi)
{
    while (true)
    {
        string s = readLine(prompt);
        if (!cin) return lo;
        int v;
        if (parseInt(s, v) && v >= lo && v <= hi) return v;
        cout << "  Invalid input. Please enter a number from " << lo << " to " << hi << ".\n";
    }
}

static void printTableHeader()
{
    cout << left
        << setw(5) << "No"
        << setw(10) << "RequestID"
        << setw(10) << "ParcelID"
        << setw(17) << "Service"
        << setw(5) << "Urg"
        << setw(18) << "RequestTime"
        << setw(11) << "Status"
        << setw(20) << "Recipient"
        << setw(16) << "Delivery Info" << "\n"
        << string(112, '-') << "\n";
}

// parcels may be nullptr (parcels.csv unreadable) - recipient columns show "-"
static void printTableRow(int no, const ServiceRequest& r, const ParcelTable* parcels)
{
    string name = "-", info = "-";
    if (parcels != nullptr)
    {
        int p = parcels->find(r.parcelID);
        if (p >= 0) { name = parcels->at(p).recipientName; info = parcels->at(p).deliveryInfo; }
        else { name = "(parcel missing)"; }
    }
    cout << left
        << setw(5) << no
        << setw(10) << r.requestID
        << setw(10) << r.parcelID
        << setw(17) << r.serviceType
        << setw(5) << r.urgency
        << setw(18) << r.requestTime
        << setw(11) << r.status
        << setw(20) << fit(name, 19)
        << setw(16) << fit(info, 15) << "\n";
}

// ============================================================================
//  ServiceModule - construction, file I/O
// ============================================================================
ServiceModule::ServiceModule(const string& requestsFile, const string& parcelsFile)
    : store(), pendingQueue(&store), requestsPath(requestsFile), parcelsPath(parcelsFile) {
}

// Validates one CSV row of requests.csv. Bad rows are skipped, never crash.
static bool parseRequestRow(const string& line, ServiceRequest& r, string& reason)
{
    string f[6];
    if (splitCSV(line, f, 6) != 6) { reason = "expected 6 fields"; return false; }
    for (int i = 0; i < 6; i++)
        if (f[i].empty()) { reason = "missing field"; return false; }
    if (!isValidServiceType(f[2])) { reason = "unknown service type '" + f[2] + "'"; return false; }
    int u;
    if (!parseInt(f[3], u) || u < URGENCY_MIN || u > URGENCY_MAX)
    {
        reason = "urgency must be 1-3"; return false;
    }
    if (!isValidStatus(f[5])) { reason = "unknown status '" + f[5] + "'"; return false; }

    r.requestID = toUpper(f[0]);  r.parcelID = toUpper(f[1]);
    r.serviceType = f[2];         r.urgency = u;
    r.requestTime = f[4];         r.status = f[5];
    return true;
}

bool ServiceModule::load()
{
    ifstream in(requestsPath.c_str());
    if (!in)
    {
        cout << "[Warning] " << requestsPath
            << " not found. Starting with an empty request list.\n";
        return false;
    }

    string line;
    int lineNo = 0, loaded = 0, skipped = 0;
    while (getline(in, line))
    {
        lineNo++;
        if (lineNo == 1) continue;                 // header row
        line = trim(line);
        if (line.empty()) continue;

        ServiceRequest r;
        string reason;
        if (!parseRequestRow(line, r, reason))
        {
            cout << "[Skipped] line " << lineNo << ": " << reason << "\n";
            skipped++;
            continue;
        }
        if (store.findByID(r.requestID) >= 0)      // duplicate Request ID
        {
            cout << "[Skipped] line " << lineNo << ": duplicate Request ID " << r.requestID << "\n";
            skipped++;
            continue;
        }
        int idx = store.add(r);
        if (r.status == STATUS_PENDING) pendingQueue.enqueue(idx);   // only Pending is queued
        loaded++;
    }
    cout << "Loaded " << loaded << " request(s), " << pendingQueue.size()
        << " pending in the priority queue, " << skipped << " row(s) skipped.\n";
    return true;
}

bool ServiceModule::save() const
{
    ofstream out(requestsPath.c_str());
    if (!out) return false;
    out << "RequestID,ParcelID,ServiceType,Urgency,RequestTime,Status\n";
    for (int i = 0; i < store.size(); i++)
    {
        const ServiceRequest& r = store.at(i);
        out << r.requestID << ',' << r.parcelID << ',' << r.serviceType << ','
            << r.urgency << ',' << r.requestTime << ',' << r.status << '\n';
    }
    return out.good();
}

void ServiceModule::persist(string& msg) const
{
    if (!save()) msg += " [Warning: could not write " + requestsPath + "]";
}

// Single place that changes a request's status; keeps the queue membership
// rule true: "a request is in the queue if and only if it is Pending".
void ServiceModule::applyStatus(int idx, const string& newStatus)
{
    string old = store.at(idx).status;
    if (old == STATUS_PENDING && newStatus != STATUS_PENDING) pendingQueue.remove(idx);
    store.at(idx).status = newStatus;
    if (old != STATUS_PENDING && newStatus == STATUS_PENDING) pendingQueue.enqueue(idx);
}

// ============================================================================
//  ServiceModule - core operations (integration API)
// ============================================================================
bool ServiceModule::findRequest(const string& requestID, ServiceRequest& out) const
{
    int idx = store.findByID(toUpper(trim(requestID)));
    if (idx < 0) return false;
    out = store.at(idx);
    return true;
}

bool ServiceModule::peekNext(ServiceRequest& out) const
{
    int idx = pendingQueue.peek();
    if (idx < 0) return false;
    out = store.at(idx);
    return true;
}

// Req 1: create a request (validation first, state change last)
bool ServiceModule::createRequest(const string& parcelIDIn, const string& recipientIDIn,
    const string& serviceTypeIn, int urgency, string& msg)
{
    string parcelID = toUpper(trim(parcelIDIn));
    string recipientID = toUpper(trim(recipientIDIn));
    string serviceType = trim(serviceTypeIn);

    if (parcelID.empty() || recipientID.empty() || serviceType.empty())
    {
        msg = "Incomplete request: Parcel ID, Recipient ID and Service Type are required."; return false;
    }
    if (!isValidServiceType(serviceType))
    {
        msg = "Invalid service type '" + serviceType + "'."; return false;
    }
    if (urgency < URGENCY_MIN || urgency > URGENCY_MAX)
    {
        msg = "Urgency must be between 1 (low) and 3 (high)."; return false;
    }

    ParcelTable parcels;
    if (!parcels.load(parcelsPath))
    {
        msg = "Cannot read " + parcelsPath + ". Request not created."; return false;
    }

    int p = parcels.find(parcelID);
    if (p < 0)
    {
        msg = "Parcel " + parcelID + " does not exist."; return false;
    }

    const ParcelRecord& parcel = parcels.at(p);
    if (toUpper(parcel.recipientID) != recipientID)
    {
        msg = "Incorrect recipient: " + recipientID + " is not the registered recipient of " + parcelID + "."; return false;
    }

    if (!parcelAllowsService(serviceType, parcel.status))
    {
        msg = "Parcel " + parcelID + " has status '" + parcel.status + "', which does not allow a "
            + serviceType + " request."; return false;
    }

    int existing = store.findActiveByParcel(parcelID);
    if (existing >= 0)
    {
        msg = "Duplicate request: parcel " + parcelID + " already has active request "
            + store.at(existing).requestID + "."; return false;
    }

    ServiceRequest r;
    r.requestID = store.nextID();
    r.parcelID = parcelID;
    r.serviceType = serviceType;
    r.urgency = urgency;
    r.requestTime = currentTimestamp();
    r.status = STATUS_PENDING;

    int idx = store.add(r);
    pendingQueue.enqueue(idx);
    msg = "Request " + r.requestID + " created for parcel " + parcelID + " (Pending).";
    persist(msg);
    return true;
}

// Hand-over step 2: can this Pending request safely be given to a resource?
bool ServiceModule::validateForAssignment(const string& requestID, string& msg) const
{
    string id = toUpper(trim(requestID));
    int idx = store.findByID(id);
    if (idx < 0) { msg = "Request ID '" + id + "' does not exist."; return false; }

    const ServiceRequest& r = store.at(idx);
    if (r.status == STATUS_ASSIGNED) { msg = "Request " + id + " is already assigned."; return false; }
    if (r.status == STATUS_COMPLETED) { msg = "Request " + id + " is already completed."; return false; }
    if (r.status == STATUS_CANCELLED) { msg = "Request " + id + " was cancelled and cannot be assigned."; return false; }

    ParcelTable parcels;
    if (!parcels.load(parcelsPath))
    {
        msg = "Cannot read " + parcelsPath + ". Request " + id + " cannot be assigned."; return false;
    }

    int p = parcels.find(r.parcelID);
    if (p < 0)
    {
        msg = "Request " + id + " refers to missing parcel " + r.parcelID
            + ". Cancel it (menu 7) before continuing."; return false;
    }

    if (!parcelAllowsService(r.serviceType, parcels.at(p).status))
    {
        msg = "Request " + id + " cannot be processed: parcel " + r.parcelID
            + " has status '" + parcels.at(p).status + "'. Cancel it (menu 7) or correct the parcel."; return false;
    }

    return true;
}

// Hand-over step 4: called AFTER a resource has been allocated (by Task 3).
// Pending -> Assigned, the request leaves the priority queue, parcel updated.
bool ServiceModule::markAssigned(const string& requestID, string& msg)
{
    if (!validateForAssignment(requestID, msg)) return false;

    int idx = store.findByID(toUpper(trim(requestID)));
    ServiceRequest& r = store.at(idx);

    // the parcel moves to its next stage (e.g. AssignedForDelivery)
    string newParcelStatus = parcelStatusOnAssignment(r.serviceType);
    if (!newParcelStatus.empty())
    {
        ParcelTable parcels;
        if (!parcels.load(parcelsPath))
        {
            msg = "Cannot read " + parcelsPath + ". Request not marked as assigned."; return false;
        }
        int p = parcels.find(r.parcelID);
        if (p >= 0 && parcels.at(p).status != newParcelStatus)
        {
            parcels.at(p).status = newParcelStatus;
            if (!parcels.save(parcelsPath))
            {
                msg = "Cannot update " + parcelsPath + ". Request not marked as assigned."; return false;
            }
        }
    }

    applyStatus(idx, STATUS_ASSIGNED);             // also removes it from the queue
    msg = "Request " + r.requestID + " marked as Assigned (Pending -> Assigned).";
    persist(msg);
    return true;
}

// Req 4: complete an Assigned request and update the parcel
bool ServiceModule::completeRequest(const string& requestID, const string& collectorID, string& msg)
{
    string id = toUpper(trim(requestID));
    int idx = store.findByID(id);
    if (idx < 0) { msg = "Request ID '" + id + "' does not exist."; return false; }

    ServiceRequest& r = store.at(idx);
    if (r.status == STATUS_COMPLETED) { msg = "Request " + id + " is already completed."; return false; }
    if (r.status == STATUS_CANCELLED) { msg = "Request " + id + " was cancelled and cannot be processed."; return false; }
    if (r.status == STATUS_PENDING)
    {
        msg = "Request " + id + " is still Pending. Mark it as Assigned first (menu 5)."; return false;
    }

    ParcelTable parcels;
    if (!parcels.load(parcelsPath))
    {
        msg = "Cannot read " + parcelsPath + ". Request not completed."; return false;
    }
    int p = parcels.find(r.parcelID);
    if (p < 0) { msg = "Parcel " + r.parcelID + " no longer exists. Cancel the request instead."; return false; }

    ParcelRecord& parcel = parcels.at(p);
    if (!parcelAllowsService(r.serviceType, parcel.status))
    {
        msg = "Parcel " + parcel.parcelID + " has status '" + parcel.status
            + "' and cannot be processed for " + r.serviceType + "."; return false;
    }

    // Collection must be handed to the registered recipient
    if (r.serviceType == SERVICE_COLLECTION &&
        (trim(collectorID).empty() || toUpper(trim(collectorID)) != toUpper(parcel.recipientID)))
    {
        msg = "Incorrect collection information: ID does not match the parcel's recipient. Parcel not released.";
        return false;
    }

    parcel.status = parcelStatusAfterCompletion(r.serviceType);
    if (!parcels.save(parcelsPath))
    {
        msg = "Cannot update " + parcelsPath + ". Request not completed."; return false;
    }

    r.status = STATUS_COMPLETED;                   // Assigned items are not in the queue
    msg = "Request " + id + " completed. Parcel " + parcel.parcelID + " is now '" + parcel.status + "'.";
    persist(msg);
    return true;
}

// Req 2 + 5: cancel a Pending or Assigned request
bool ServiceModule::cancelRequest(const string& requestID, string& msg)
{
    string id = toUpper(trim(requestID));
    int idx = store.findByID(id);
    if (idx < 0) { msg = "Request ID '" + id + "' does not exist."; return false; }

    ServiceRequest& r = store.at(idx);
    if (r.status == STATUS_COMPLETED) { msg = "Request " + id + " is completed and cannot be cancelled."; return false; }
    if (r.status == STATUS_CANCELLED) { msg = "Request " + id + " is already cancelled."; return false; }

    bool wasAssigned = (r.status == STATUS_ASSIGNED);

    // A cancelled delivery leaves the parcel waiting for a new decision
    string note;
    ParcelTable parcels;
    if (parcels.load(parcelsPath))
    {
        int p = parcels.find(r.parcelID);
        if (p >= 0 && parcels.at(p).status == "AssignedForDelivery" &&
            (r.serviceType == SERVICE_CAMPUS_DELIVERY || r.serviceType == SERVICE_REDELIVERY))
        {
            parcels.at(p).status = "OnHold";
            if (parcels.save(parcelsPath)) note = " Parcel " + r.parcelID + " set to OnHold.";
        }
    }

    applyStatus(idx, STATUS_CANCELLED);            // also removes it from the queue
    msg = "Request " + id + " cancelled." + note;
    if (wasAssigned) msg += " Release any resource assigned to it (Task 3).";
    persist(msg);
    return true;
}

// Used by Task 4 (undo). Bypasses the normal flow but keeps the queue correct.
bool ServiceModule::restoreRequestStatus(const string& requestID, const string& previousStatus, string& msg)
{
    string id = toUpper(trim(requestID));
    int idx = store.findByID(id);
    if (idx < 0) { msg = "Request ID '" + id + "' does not exist."; return false; }
    if (!isValidStatus(previousStatus)) { msg = "Invalid status '" + previousStatus + "'."; return false; }
    if (store.at(idx).status == previousStatus)
    {
        msg = "Request " + id + " is already '" + previousStatus + "'."; return false;
    }

    applyStatus(idx, previousStatus);
    msg = "Request " + id + " restored to '" + previousStatus + "'.";
    persist(msg);
    return true;
}

// ============================================================================
//  ServiceModule - menu (console I/O)
// ============================================================================
void ServiceModule::showMenu() const
{
    cout << "\n==============================================================\n"
        << "  PARCEL SERVICE & COLLECTION MANAGEMENT  (Task 2)\n"
        << "==============================================================\n"
        << "  1. Create service request\n"
        << "  2. Display active requests (priority order)\n"
        << "  3. View request details\n"
        << "  4. Show next request to be handled\n"
        << "  5. Mark request as Assigned  (Pending  -> Assigned)\n"
        << "  6. Complete assigned request (Assigned -> Completed)\n"
        << "  7. Cancel request\n"
        << "  8. Display all request records\n"
        << "  0. Save and exit\n"
        << "--------------------------------------------------------------\n";
}

void ServiceModule::run()
{
    int choice;
    do
    {
        showMenu();
        choice = readInt("Enter choice: ", 0, 8);
        switch (choice)
        {
        case 1: menuCreate();        break;
        case 2: menuDisplayActive(); break;
        case 3: menuView();          break;
        case 4: menuNext();          break;
        case 5: menuMarkAssigned();   break;
        case 6: menuComplete();      break;
        case 7: menuCancel();        break;
        case 8: menuDisplayAll();    break;
        default: break;
        }
    } while (choice != 0);

    if (save()) cout << "\n" << requestsPath << " saved. Goodbye.\n";
    else        cout << "\n[Warning] could not save " << requestsPath << ".\n";
}

void ServiceModule::menuCreate()
{
    cout << "\n--- Create Service Request ---\n";
    string parcelID = toUpper(readLine("Parcel ID               : "));
    string recipientID = toUpper(readLine("Recipient ID (confirm)  : "));

    cout << "Service type:\n";
    for (int i = 0; i < SERVICE_TYPE_COUNT; i++)
        cout << "  " << (i + 1) << ". " << SERVICE_TYPES[i] << "\n";
    int t = readInt("Choose service type (1-5): ", 1, SERVICE_TYPE_COUNT);

    int urgency = readInt("Urgency (1 = low ... 3 = high): ", URGENCY_MIN, URGENCY_MAX);

    string msg;
    bool ok = createRequest(parcelID, recipientID, SERVICE_TYPES[t - 1], urgency, msg);
    cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}

void ServiceModule::menuDisplayActive() const
{
    int pending = pendingQueue.size(), assigned = 0;
    for (int i = 0; i < store.size(); i++)
        if (store.at(i).status == STATUS_ASSIGNED) assigned++;

    if (pending == 0 && assigned == 0)
    {
        cout << "\nNo active service requests.\n";
        return;
    }

    ParcelTable parcels;
    bool parcelsOk = parcels.load(parcelsPath);
    const ParcelTable* pt = parcelsOk ? &parcels : nullptr;

    cout << "\n=== Pending requests - in the order they will be handled ===\n";
    if (pending == 0) cout << "None.\n";
    else
    {
        int* order = new int[pending];
        pendingQueue.snapshotSorted(order);
        printTableHeader();
        for (int i = 0; i < pending; i++) printTableRow(i + 1, store.at(order[i]), pt);
        delete[] order;
    }

    cout << "\n=== In progress (Assigned) ===\n";
    if (assigned == 0) cout << "None.\n";
    else
    {
        printTableHeader();
        int n = 0;
        for (int i = 0; i < store.size(); i++)
            if (store.at(i).status == STATUS_ASSIGNED) printTableRow(++n, store.at(i), pt);
    }
}

void ServiceModule::menuView() const
{
    string id = toUpper(readLine("\nEnter Request ID: "));
    int idx = store.findByID(id);
    if (idx < 0) { cout << "[Error] Request ID '" << id << "' does not exist.\n"; return; }

    const ServiceRequest& r = store.at(idx);
    cout << "\n--- Request " << r.requestID << " ---\n"
        << "Parcel ID    : " << r.parcelID << "\n"
        << "Service type : " << r.serviceType << "\n"
        << "Urgency      : " << r.urgency << "\n"
        << "Request time : " << r.requestTime << "\n"
        << "Status       : " << r.status << "\n";

    if (r.status == STATUS_PENDING)
    {
        int n = pendingQueue.size();
        int* order = new int[n];
        pendingQueue.snapshotSorted(order);
        for (int i = 0; i < n; i++)
            if (order[i] == idx) { cout << "Queue position: " << (i + 1) << " of " << n << "\n"; break; }
        delete[] order;
    }

    ParcelTable parcels;
    if (parcels.load(parcelsPath) && parcels.find(r.parcelID) >= 0)
    {
        const ParcelRecord& p = parcels.at(parcels.find(r.parcelID));
        cout << "\nParcel details\n"
            << "  Recipient   : " << p.recipientName << " (" << p.recipientID << ")\n"
            << "  Delivery    : " << p.deliveryInfo << "\n"
            << "  Type/Courier: " << p.parcelType << " / " << p.courier << "\n"
            << "  Arrived     : " << p.arrivalDateTime << "\n"
            << "  Status      : " << p.status << "\n";
    }
    else cout << "\n(Parcel details unavailable - parcel missing or " << parcelsPath << " unreadable)\n";
}

void ServiceModule::menuNext() const
{
    ServiceRequest r;
    if (!peekNext(r)) { cout << "\n[Info] No pending service requests.\n"; return; }

    ParcelTable parcels;
    bool ok = parcels.load(parcelsPath);
    cout << "\nNext request to be handled:\n";
    printTableHeader();
    printTableRow(1, r, ok ? &parcels : nullptr);
    cout << "\nRule: highest urgency first, then earliest request time, then smallest Request ID.\n";
}

void ServiceModule::menuMarkAssigned()
{
    cout << "\n(Use this after a counter / delivery resource has been allocated in Task 3.)\n";
    string id = toUpper(readLine("Request ID to mark as Assigned: "));
    string msg;
    bool ok = markAssigned(id, msg);
    cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}

void ServiceModule::menuComplete()
{
    string id = toUpper(readLine("\nRequest ID to complete: "));
    ServiceRequest r;
    string collector;
    // Only a Collection that is really in progress needs the ID check
    if (findRequest(id, r) && r.serviceType == SERVICE_COLLECTION && r.status == STATUS_ASSIGNED)
        collector = toUpper(readLine("Collector's Recipient ID (checked at the counter): "));

    string msg;
    bool ok = completeRequest(id, collector, msg);
    cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}

void ServiceModule::menuCancel()
{
    string id = toUpper(readLine("\nRequest ID to cancel: "));
    ServiceRequest r;
    if (findRequest(id, r) &&
        (r.status == STATUS_PENDING || r.status == STATUS_ASSIGNED))
    {
        string answer = readLine("Cancel " + r.requestID + " (" + r.serviceType + ", parcel "
            + r.parcelID + ")? (y/n): ");
        if (answer != "y" && answer != "Y") { cout << "[Info] Cancellation aborted.\n"; return; }
    }
    string msg;
    bool ok = cancelRequest(id, msg);
    cout << (ok ? "[OK] " : "[Error] ") << msg << "\n";
}

void ServiceModule::menuDisplayAll() const
{
    if (store.size() == 0) { cout << "\n[Info] No request records.\n"; return; }
    ParcelTable parcels;
    bool ok = parcels.load(parcelsPath);
    cout << "\n=== All request records (" << store.size() << ") ===\n";
    printTableHeader();
    for (int i = 0; i < store.size(); i++)
        printTableRow(i + 1, store.at(i), ok ? &parcels : nullptr);
}