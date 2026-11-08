// ============================================================================
// Task 5 - Parcel Category Organisation Module
// ============================================================================
#include "ParcelCategoryTree.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
    const char* PARCEL_HEADER = "ParcelID,RecipientName,RecipientID,DeliveryInfo,ParcelType,Courier,ArrivalDateTime,Status";

    // Trims leading and trailing whitespace from one field.
    std::string trimText(std::string text) {
        std::size_t first = 0;
        while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) first++;
        std::size_t last = text.size();
        while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) last--;
        return text.substr(first, last - first);
    }

    // Splits the CSV format used by the project data files.
    bool splitParcelRow(std::string line, Parcel& parcel) {
        std::string fields[8];
        std::istringstream input(line);
        for (int i = 0; i < 7; i++) {
            if (!std::getline(input, fields[i], ',')) return false;
        }
        if (!std::getline(input, fields[7])) return false;
        std::string extra;
        if (std::getline(input, extra, ',')) return false;
        for (int i = 0; i < 8; i++) fields[i] = trimText(fields[i]);
        for (int i = 0; i < 8; i++) if (fields[i].empty()) return false;

        parcel.parcelID = fields[0];
        parcel.recipientName = fields[1];
        parcel.recipientID = fields[2];
        parcel.deliveryInfo = fields[3];
        parcel.parcelType = fields[4];
        parcel.courier = fields[5];
        parcel.arrivalDateTime = fields[6];
        parcel.status = fields[7];
        return true;
    }

    // Checks that a user-entered field can be stored in the project's CSV.
    bool validCsvField(std::string value, std::size_t maxLength) {
        if (value.empty() || value.size() > maxLength) return false;
        return value.find(',') == std::string::npos && value.find('\n') == std::string::npos
            && value.find('\r') == std::string::npos;
    }

    // Checks the timestamp shape and basic ranges used by parcels.csv.
    bool validTimestamp(std::string value) {
        if (value.size() != 16 || value[4] != '-' || value[7] != '-'
            || value[10] != ' ' || value[13] != ':') return false;
        const int digitPositions[] = { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15 };
        for (int i = 0; i < 12; i++)
            if (!std::isdigit(static_cast<unsigned char>(value[digitPositions[i]]))) return false;
        int month = (value[5] - '0') * 10 + value[6] - '0';
        int day = (value[8] - '0') * 10 + value[9] - '0';
        int hour = (value[11] - '0') * 10 + value[12] - '0';
        int minute = (value[14] - '0') * 10 + value[15] - '0';
        return month >= 1 && month <= 12 && day >= 1 && day <= 31
            && hour <= 23 && minute <= 59;
    }

    // Converts parcel type input to the canonical labels used by parcels.csv.
    bool canonicalParcelType(std::string input, std::string& result) {
        std::string value = trimText(input);
        for (std::size_t i = 0; i < value.size(); i++) value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
        if (value == "standard") result = "Standard";
        else if (value == "fragile") result = "Fragile";
        else if (value == "urgent") result = "Urgent";
        else if (value == "oversized") result = "Oversized";
        else return false;
        return true;
    }

    // Returns true only for a status already understood by the other modules.
    bool knownParcelStatus(std::string status) {
        const char* values[] = { "Registered", "ReadyForCollection", "AssignedForDelivery",
            "SpecialHandling", "ReturnToSender", "OnHold", "Missing", "Collected",
            "Delivered", "Returned", "Cancelled" };
        for (int i = 0; i < 11; i++) if (status == values[i]) return true;
        return false;
    }

    // Names the selected tree key for menu labels and search prompts.
    const char* treeKeyName(ParcelTreeKey key) {
        switch (key) {
        case ParcelTreeKey::ParcelID: return "Parcel ID";
        case ParcelTreeKey::RecipientID: return "Recipient ID";
        case ParcelTreeKey::ParcelType: return "Parcel Type";
        case ParcelTreeKey::Status: return "Status";
        }
        return "Unknown";
    }
}

// Creates an empty tree and keeps the shared CSV file paths.
ParcelCategoryModule::ParcelCategoryModule(const std::string& parcelsFile,
    const std::string& requestsFile)
    : root(nullptr), organisationKey(ParcelTreeKey::ParcelID),
    parcelsPath(parcelsFile), requestsPath(requestsFile), parcels(nullptr) {
}

// Frees all tree storage when the Task 5 module is destroyed.
ParcelCategoryModule::~ParcelCategoryModule() { destroy(root); }

// Recursively deletes every tree node and its parcel chain.
void ParcelCategoryModule::destroy(TreeNode* node) {
    if (node == nullptr) return;
    destroy(node->left);
    destroy(node->right);
    ParcelNode* current = node->first;
    while (current != nullptr) {
        ParcelNode* next = current->next;
        delete current;
        current = next;
    }
    delete node;
}

// Trims whitespace and folds ASCII letters for case-insensitive keys.
std::string ParcelCategoryModule::normaliseKey(const std::string& value) {
    std::string result = trimText(value);
    for (std::size_t i = 0; i < result.size(); i++)
        result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    return result;
}

// Returns the selected parcel field in a comparison-friendly form.
std::string ParcelCategoryModule::keyFor(const Parcel& parcel) const {
    switch (organisationKey) {
    case ParcelTreeKey::ParcelID: return normaliseKey(parcel.parcelID);
    case ParcelTreeKey::RecipientID: return normaliseKey(parcel.recipientID);
    case ParcelTreeKey::ParcelType: return normaliseKey(parcel.parcelType);
    case ParcelTreeKey::Status: return normaliseKey(parcel.status);
    }
    return normaliseKey(parcel.parcelID);
}

// Inserts a parcel into its key node, adding a node when the key is new.
bool ParcelCategoryModule::insertIntoTree(const Parcel& parcel) {
    const std::string key = keyFor(parcel);
    if (key.empty()) return false;
    TreeNode** link = &root;
    while (*link != nullptr) {
        if (key == (*link)->key) {
            ParcelNode* record = new ParcelNode{ parcel, nullptr };
            (*link)->last->next = record;
            (*link)->last = record;
            (*link)->records++;
            return true;
        }
        link = (key < (*link)->key) ? &(*link)->left : &(*link)->right;
    }
    ParcelNode* record = new ParcelNode{ parcel, nullptr };
    *link = new TreeNode{ key, record, record, 1, nullptr, nullptr };
    return true;
}

// Finds the node whose key matches the supplied normalised key.
ParcelCategoryModule::TreeNode* ParcelCategoryModule::findNode(std::string key) const {
    TreeNode* current = root;
    while (current != nullptr) {
        if (key == current->key) return current;
        current = (key < current->key) ? current->left : current->right;
    }
    return nullptr;
}

// Searches all buckets for a globally unique Parcel ID.
bool ParcelCategoryModule::findParcelByID(std::string parcelID, Parcel& result) const {
    return findParcelInTree(root, parcelID, result);
}

// Recursive helper that searches one subtree for a parcel ID.
bool ParcelCategoryModule::findParcelInTree(const TreeNode* node, const std::string& parcelID,
    Parcel& result) const {
    if (node == nullptr) return false;
    ParcelNode* record = node->first;
    while (record != nullptr) {
        if (normaliseKey(record->parcel.parcelID) == normaliseKey(parcelID)) {
            result = record->parcel;
            return true;
        }
        record = record->next;
    }
    return findParcelInTree(node->left, parcelID, result)
        || findParcelInTree(node->right, parcelID, result);
}

// Displays one complete parcel record in a consistent table format.
void ParcelCategoryModule::printParcel(const Parcel& parcel) {
    std::cout << "  " << parcel.parcelID << " | " << parcel.recipientName << " | "
        << parcel.recipientID << " | " << parcel.deliveryInfo << " | "
        << parcel.parcelType << " | " << parcel.courier << " | "
        << parcel.arrivalDateTime << " | " << parcel.status << "\n";
}

// Displays a key node and every parcel in its duplicate-key chain.
void ParcelCategoryModule::displayNodeRecords(const TreeNode* node) const {
    if (node == nullptr) return;
    ParcelNode* record = node->first;
    while (record != nullptr) {
        printParcel(record->parcel);
        record = record->next;
    }
}

// Writes the BST in-order, which saves records in selected-key order.
bool ParcelCategoryModule::writeInOrder(std::ofstream& out, const TreeNode* node) const {
    if (node == nullptr) return true;
    if (!writeInOrder(out, node->left)) return false;
    ParcelNode* record = node->first;
    while (record != nullptr) {
        const Parcel& p = record->parcel;
        out << p.parcelID << ',' << p.recipientName << ',' << p.recipientID << ','
            << p.deliveryInfo << ',' << p.parcelType << ',' << p.courier << ','
            << p.arrivalDateTime << ',' << p.status << '\n';
        if (!out.good()) return false;
        record = record->next;
    }
    return writeInOrder(out, node->right);
}

// Reads the shared parcel CSV and rebuilds the selected-key BST.
bool ParcelCategoryModule::reload(bool verbose) {
    std::ifstream in(parcelsPath.c_str());
    if (!in) {
        if (verbose) std::cout << "[Error] Cannot open " << parcelsPath << ".\n";
        return false;
    }
    std::string header;
    if (!std::getline(in, header)) {
        if (verbose) std::cout << "[Error] " << parcelsPath << " is empty.\n";
        return false;
    }
    header = trimText(header);
    if (!header.empty() && static_cast<unsigned char>(header[0]) == 0xEF)
        header = header.substr(3); // tolerate a UTF-8 BOM
    if (header != PARCEL_HEADER) {
        if (verbose) std::cout << "[Error] Unexpected parcels.csv header.\n";
        return false;
    }

    TreeNode* oldRoot = root;
    root = nullptr;
    std::string line;
    int loaded = 0, lineNo = 1;
    bool failed = false;
    while (std::getline(in, line)) {
        lineNo++;
        line = trimText(line);
        if (line.empty()) continue;
        Parcel parcel;
        if (!splitParcelRow(line, parcel)) {
            std::cout << "[Error] Invalid parcel row at line " << lineNo << ". No changes were made.\n";
            failed = true;
            break;
        }
        Parcel existing;
        // Duplicate parcel IDs would make update/remove ambiguous, so refuse the file.
        if (findParcelByID(parcel.parcelID, existing)) {
            std::cout << "[Error] Duplicate Parcel ID " << parcel.parcelID << " at line " << lineNo << ".\n";
            failed = true;
            break;
        }
        if (!insertIntoTree(parcel)) { failed = true; break; }
        loaded++;
    }
    if (failed || !in.eof()) {
        destroy(root);
        root = oldRoot;
        return false;
    }
    destroy(oldRoot);
    if (verbose) std::cout << "Loaded " << loaded << " parcel record(s).\n";
    return true;
}

// Saves every parcel to the shared CSV file.
bool ParcelCategoryModule::save() const {
    std::ofstream out(parcelsPath.c_str());
    if (!out) return false;
    out << PARCEL_HEADER << '\n';
    return writeInOrder(out, root) && out.good();
}

// Checks requests.csv for Pending or Assigned requests for one parcel.
bool ParcelCategoryModule::hasActiveRequest(std::string parcelID, std::string& requestID) const {
    std::ifstream in(requestsPath.c_str());
    if (!in) return false;
    std::string line;
    if (!std::getline(in, line)) return false;
    while (std::getline(in, line)) {
        std::string fields[6];
        std::istringstream row(line);
        bool complete = true;
        for (int i = 0; i < 5; i++) if (!std::getline(row, fields[i], ',')) { complete = false; break; }
        if (!complete || !std::getline(row, fields[5])) continue;
        if (normaliseKey(fields[1]) == normaliseKey(parcelID)
            && (trimText(fields[5]) == "Pending" || trimText(fields[5]) == "Assigned")) {
            requestID = trimText(fields[0]);
            return true;
        }
    }
    return false;
}

// Locates a parcel's bucket and removes that parcel from its chain.
bool ParcelCategoryModule::removeParcelFromTree(std::string parcelID) {
    return removeByID(root, parcelID);
}

// Recursively removes a parcel and deletes an empty BST node safely.
bool ParcelCategoryModule::removeByID(TreeNode*& node, const std::string& parcelID) {
    if (node == nullptr) return false;
    ParcelNode* previous = nullptr;
    ParcelNode* current = node->first;
    while (current != nullptr) {
        if (normaliseKey(current->parcel.parcelID) == normaliseKey(parcelID)) {
            if (previous != nullptr) previous->next = current->next;
            else node->first = current->next;
            if (node->last == current) node->last = previous;
            delete current;
            node->records--;
            if (node->records > 0) return true;
            if (node->left == nullptr) {
                TreeNode* old = node; node = node->right; delete old;
            }
            else if (node->right == nullptr) {
                TreeNode* old = node; node = node->left; delete old;
            }
            else if (node->right->left == nullptr) {
                TreeNode* old = node;
                TreeNode* successor = node->right;
                successor->left = node->left;
                node = successor;
                delete old;
            }
            else {
                TreeNode* parent = node->right;
                while (parent->left->left != nullptr) parent = parent->left;
                TreeNode* successor = parent->left;
                parent->left = successor->right;
                successor->left = node->left;
                successor->right = node->right;
                TreeNode* old = node; node = successor; delete old;
            }
            return true;
        }
        previous = current;
        current = current->next;
    }
    return removeByID(node->left, parcelID) || removeByID(node->right, parcelID);
}

// Reads one console line and removes surrounding whitespace.
std::string ParcelCategoryModule::readLine(std::string prompt) {
    std::string line;
    std::cout << prompt;
    std::getline(std::cin, line);
    return trimText(line);
}

// Reads a numeric menu choice and rejects malformed input.
int ParcelCategoryModule::readChoice(std::string prompt) {
    std::string line = readLine(prompt);
    if (!std::cin) return 0;
    if (line.empty()) return -1;
    for (std::size_t i = 0; i < line.size(); i++)
        if (!std::isdigit(static_cast<unsigned char>(line[i]))) return -1;
    return std::atoi(line.c_str());
}

// Loads the parcel CSV into the BST and reports the load result.
bool ParcelCategoryModule::load() { return reload(true); }

// Shows the organisation key and available Task 5 operations.
void ParcelCategoryModule::showMenu() const {
    std::cout << "\n===== Task 5: Parcel Category Organisation (BST) =====\n"
        << "Organisation key: " << treeKeyName(organisationKey) << "\n"
        << "1. Select organisation key / rebuild BST\n"
        << "2. Insert a new parcel record\n"
        << "3. Search by Parcel ID or current key\n"
        << "4. Display all parcel records\n"
        << "5. Update parcel information\n"
        << "6. Remove an eligible parcel record\n"
        << "7. Reload from parcels.csv\n"
        << "0. Back to main menu\n";
}

// Selects a key and rebuilds the tree from the current parcel file.
void ParcelCategoryModule::menuSelectKey() {
    std::cout << "\nOrganise by:\n  1. Parcel ID (unique)\n  2. Recipient ID\n  3. Parcel Type\n  4. Status\n";
    int choice = readChoice("Choose (1-4, 0 = cancel): ");
    if (choice == 0) return;
    if (choice < 1 || choice > 4) { std::cout << "[Error] Invalid key choice.\n"; return; }
    ParcelTreeKey previous = organisationKey;
    organisationKey = static_cast<ParcelTreeKey>(choice);
    if (!reload(true)) {
        organisationKey = previous;
        reload(false);
        std::cout << "[Error] Could not rebuild the BST; previous key remains active.\n";
    }
}

// ---------------------------------------------------------------------------
// CHANGED (Fix 2): insert now delegates to Task 1's registerParcel(), so every
// registration rule is enforced in ONE place:
//   - recipient name / ID format
//   - same recipient ID must map to the same person
//   - duplicate parcel rejection (courier + type + delivery + arrival)
//   - no future arrival date/time
//   - Parcel ID = highest existing + 1
//   - publishes ACT_REGISTERED through the Task 4 hook
// ---------------------------------------------------------------------------
void ParcelCategoryModule::menuInsert() {
    if (!reload(false)) { std::cout << "[Error] Cannot insert until parcels.csv is readable.\n"; return; }

    if (parcels == nullptr) {
        std::cout << "[Error] Task 5 is not connected to the parcel module; "
            "insertion is unavailable.\n";
        return;
    }

    Parcel input;
    input.recipientName = readLine("Recipient name: ");
    input.recipientID = readLine("Recipient ID (Example: TP012345 / STAFF021): ");
    input.deliveryInfo = readLine(
        "Delivery information (Example: Counter / Block A L3 / Admin Office): ");
    input.parcelType = readLine(
        "Parcel type (Standard / Fragile / Urgent / Oversized): ");
    input.courier = readLine(
        "Courier (Example: Pos Laju / J&T / DHL / Ninja Van / Flash Express / Shopee Xpress): ");
    input.arrivalDateTime = readLine(
        "Arrival date/time (YYYY-MM-DD HH:MM, Enter = now): ");
    // parcelID and status are assigned by Task 1 (nextID / "Registered").

    std::string newID, msg;
    if (!parcels->registerParcel(input, newID, msg)) {
        std::cout << "[Error] " << msg << "\n";
        return;
    }

    // Task 1 has already written parcels.csv; reload so the new row is in the tree.
    reload(false);
    std::cout << "[OK] " << msg << "\n";
}

// Searches by Parcel ID or by the current organisation key.
void ParcelCategoryModule::menuSearch() const {
    std::cout << "Search by:\n  1. Parcel ID\n  2. Current organisation key (" << treeKeyName(organisationKey) << ")\n";
    int mode = readChoice("Choose (1-2, 0 = cancel): ");
    if (mode == 0) return;
    if (mode == 1) {
        std::string id = readLine("Parcel ID: ");
        Parcel parcel;
        if (!findParcelByID(id, parcel)) { std::cout << "[Info] Parcel " << id << " does not exist.\n"; return; }
        std::cout << "ParcelID | Recipient | RecipientID | DeliveryInfo | Type | Courier | ArrivalDateTime | Status\n";
        printParcel(parcel);
        return;
    }
    if (mode != 2) { std::cout << "[Error] Invalid search option.\n"; return; }
    std::string value = readLine(std::string(treeKeyName(organisationKey)) + ": ");
    TreeNode* node = findNode(normaliseKey(value));
    if (node == nullptr) { std::cout << "[Info] No parcel category/key '" << value << "' was found.\n"; return; }
    std::cout << "Found key '" << value << "' with " << node->records << " parcel(s).\n"
        << "ParcelID | Recipient | RecipientID | DeliveryInfo | Type | Courier | ArrivalDateTime | Status\n";
    displayNodeRecords(node);
}

// Displays all parcels in BST in-order traversal.
void ParcelCategoryModule::menuDisplay() const {
    if (root == nullptr) { std::cout << "[Info] No parcel records are loaded.\n"; return; }
    std::cout << "\n=== Parcel records ordered by " << treeKeyName(organisationKey) << " ===\n"
        << "ParcelID | Recipient | RecipientID | DeliveryInfo | Type | Courier | ArrivalDateTime | Status\n";
    displayInOrder(root);
}

// Performs an in-order display of all nodes in one subtree.
void ParcelCategoryModule::displayInOrder(const TreeNode* node) const {
    if (node == nullptr) return;
    displayInOrder(node->left);
    displayNodeRecords(node);
    displayInOrder(node->right);
}

// Updates a parcel's editable information while preserving its ID/status.
void ParcelCategoryModule::menuUpdate() {
    if (!reload(false)) { std::cout << "[Error] Cannot update until parcels.csv is readable.\n"; return; }
    std::string id = readLine("Parcel ID to update: ");
    Parcel original;
    if (!findParcelByID(id, original)) { std::cout << "[Info] Parcel " << id << " does not exist.\n"; return; }
    std::string activeRequest;
    if (hasActiveRequest(original.parcelID, activeRequest)) {
        std::cout << "[Error] Parcel has active request " << activeRequest
            << "; update it after the request is completed or cancelled.\n";
        return;
    }
    std::cout << "Update field:\n  1. Recipient name\n  2. Recipient ID\n  3. Delivery information\n"
        << "  4. Parcel type\n  5. Courier\n  6. Arrival date/time\n";
    int field = readChoice("Choose (1-6, 0 = cancel): ");
    if (field == 0) return;
    if (field < 1 || field > 6) { std::cout << "[Error] Invalid field.\n"; return; }
    std::string prompt = "New value: ";
    if (field == 2) prompt = "New Recipient ID (Example: TP012345 / STAFF021): ";
    else if (field == 3) prompt = "New delivery information (Example: Counter / Block A L3 / Admin Office): ";
    else if (field == 5) prompt = "New courier (Example: Pos Laju / J&T / DHL / Ninja Van / Flash Express / Shopee Xpress): ";
    std::string value = readLine(prompt);
    Parcel updated = original;
    if (field == 1 && validCsvField(value, 60)) updated.recipientName = value;
    else if (field == 2 && validCsvField(value, 20)) updated.recipientID = value;
    else if (field == 3 && validCsvField(value, 40)) updated.deliveryInfo = value;
    else if (field == 4 && canonicalParcelType(value, updated.parcelType)) {}
    else if (field == 5 && validCsvField(value, 40)) updated.courier = value;
    else if (field == 6 && validTimestamp(value)) updated.arrivalDateTime = value;
    else { std::cout << "[Error] Invalid value for the selected field.\n"; return; }

    if (!removeParcelFromTree(original.parcelID) || !insertIntoTree(updated)) {
        reload(false);
        std::cout << "[Error] Could not maintain the BST after the update.\n";
        return;
    }
    if (!save()) {
        reload(false);
        std::cout << "[Error] Could not save parcels.csv; update was not kept.\n";
        return;
    }
    std::cout << "[OK] Parcel " << original.parcelID << " updated.\n";
}

// Removes an eligible parcel and its matching tree record.
void ParcelCategoryModule::menuRemove() {
    if (!reload(false)) { std::cout << "[Error] Cannot remove until parcels.csv is readable.\n"; return; }
    std::string id = readLine("Parcel ID to remove: ");
    Parcel parcel;
    if (!findParcelByID(id, parcel)) { std::cout << "[Info] Parcel " << id << " does not exist.\n"; return; }
    if (parcel.status != PARCEL_REGISTERED && parcel.status != PARCEL_CANCELLED) {
        std::cout << "[Error] Only Registered or Cancelled parcels can be removed; "
            "other records are retained as history.\n";
        return;
    }
    std::string requestID;
    if (hasActiveRequest(parcel.parcelID, requestID)) {
        std::cout << "[Error] Parcel still has active request " << requestID << ". Cancel it in Task 2 first.\n";
        return;
    }
    if (!removeParcelFromTree(parcel.parcelID)) { std::cout << "[Error] Could not remove parcel from BST.\n"; return; }
    if (!save()) {
        reload(false);
        std::cout << "[Error] Could not save parcels.csv; removal was not kept.\n";
        return;
    }
    std::cout << "[OK] Parcel " << parcel.parcelID << " removed.\n";
}

// Runs the interactive Task 5 menu until staff choose to return.
void ParcelCategoryModule::run() {
    if (!reload(false)) {
        std::cout << "[Error] Task 5 cannot start because parcels.csv could not be loaded.\n";
        return;
    }
    int choice = -1;
    while (choice != 0) {
        showMenu();
        choice = readChoice("Choice: ");
        if (choice == 1) menuSelectKey();
        else if (choice == 2) menuInsert();
        else if (choice == 3) { reload(false); menuSearch(); }
        else if (choice == 4) { reload(false); menuDisplay(); }
        else if (choice == 5) menuUpdate();
        else if (choice == 6) menuRemove();
        else if (choice == 7) reload(true);
        else if (choice != 0) std::cout << "[Error] Invalid menu choice.\n";
    }
}