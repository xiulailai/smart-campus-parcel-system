/*
Task 5 - Parcel Category Organisation Module
CT077-3-2-DSTR | Smart Campus Parcel Collection and Delivery System

Data Structure:
ParcelCategoryTree is a Binary Search Tree.
Staff can select Parcel ID, Recipient ID, Parcel Type, or Status as the
organisation key. A key can have multiple parcels, so each tree node owns
a small singly linked chain of parcel records with the same key.

Complexity:
Search/insert: O(h). (h is the tree height and (O(h) similar to O(log n))
Display: O(n).
Deleting a parcel from a key's record chain is O(k) (k is number of parcels in same key)
Deleting an empty tree node is O(h).

Error prevention:
parcels.csv is reloaded before operations and saved after changes, so Task 5 sees statuses written by Tasks 1 and 2.
requests.csv is read to protect active requests and to avoid removing parcels still in use.

Insertion goes through Task 1 (ParcelModule::registerParcel) so every
registration rule – recipient ID format, duplicate names, duplicate
parcels, future arrivals – is enforced by ONE place. If Main does not
call setParcelModule(), Task 5 refuses to insert and says why.
*/

#ifndef PARCEL_CATEGORY_TREE_HPP
#define PARCEL_CATEGORY_TREE_HPP

#include "../Task1/ParcelIntake.hpp"
#include <fstream>
#include <string>

enum class ParcelTreeKey {
    ParcelID = 1,
    RecipientID,
    ParcelType,
    Status
};

class ParcelCategoryModule {
private:
    struct ParcelNode {
        Parcel parcel;
        ParcelNode* next;
    };
    struct TreeNode {
        std::string key;
        ParcelNode* first;
        ParcelNode* last;
        int records;
        TreeNode* left;
        TreeNode* right;
    };

    TreeNode* root;
    ParcelTreeKey organisationKey;
    std::string parcelsPath;
    std::string requestsPath;
    ParcelModule* parcels = nullptr;   // NEW: Task 1, used by menuInsert only

    // Recursively deletes every tree node and its parcel chain.
    void destroy(TreeNode* node);
    // Returns the selected parcel field in a comparison-friendly form.
    std::string keyFor(const Parcel& parcel) const;
    // Trims whitespace and folds ASCII letters for case-insensitive keys.
    static std::string normaliseKey(const std::string& value);
    // Inserts a parcel into its key node, adding a node when the key is new.
    bool insertIntoTree(const Parcel& parcel);
    // Finds the node whose key matches the supplied normalised key.
    TreeNode* findNode(std::string key) const;
    // Searches all buckets for a globally unique Parcel ID.
    bool findParcelByID(std::string parcelID, Parcel& result) const;
    // Recursive helper that searches one subtree for a parcel ID.
    bool findParcelInTree(const TreeNode* node, const std::string& parcelID, Parcel& result) const;
    // Displays one complete parcel record in a consistent table format.
    static void printParcel(const Parcel& parcel);
    // Displays a key node and every parcel in its duplicate-key chain.
    void displayNodeRecords(const TreeNode* node) const;
    // Performs an in-order display of all nodes in one subtree.
    void displayInOrder(const TreeNode* node) const;
    // Writes the BST in-order, which saves records in selected-key order.
    bool writeInOrder(std::ofstream& out, const TreeNode* node) const;
    // Reads the shared parcel CSV and rebuilds the selected-key BST.
    bool reload(bool verbose);
    // Saves every parcel to the shared CSV file.
    bool save() const;
    // Checks requests.csv for Pending or Assigned requests for one parcel.
    bool hasActiveRequest(std::string parcelID, std::string& requestID) const;
    // Locates a parcel's bucket and removes that parcel from its chain.
    bool removeParcelFromTree(std::string parcelID);
    // Recursively removes a parcel and deletes an empty BST node safely.
    bool removeByID(TreeNode*& node, const std::string& parcelID);
    // Reads one console line and removes surrounding whitespace.
    static std::string readLine(std::string prompt);
    // Reads a numeric menu choice and rejects malformed input.
    static int readChoice(std::string prompt);
    // Shows the organisation key and available Task 5 operations.
    void showMenu() const;
    // Selects a key and rebuilds the tree from the current parcel file.
    void menuSelectKey();
    // Validates and inserts a new Registered parcel into parcels.csv.
    void menuInsert();
    // Searches by Parcel ID or by the current organisation key.
    void menuSearch() const;
    // Displays all parcels in BST in-order traversal.
    void menuDisplay() const;
    // Updates a parcel's editable information while preserving its ID/status.
    void menuUpdate();
    // Removes an eligible parcel and its matching tree record.
    void menuRemove();

public:
    // Stores the shared CSV paths and starts with Parcel ID as the tree key.
    explicit ParcelCategoryModule(const std::string& parcelsFile = "parcels.csv",
        const std::string& requestsFile = "requests.csv");
    // Releases all dynamically allocated BST nodes and parcel chains.
    ~ParcelCategoryModule();
    ParcelCategoryModule(const ParcelCategoryModule&) = delete;
    ParcelCategoryModule& operator=(const ParcelCategoryModule&) = delete;

    // NEW: Main.cpp wires Task 1 in here so menuInsert can reuse registerParcel().
    void setParcelModule(ParcelModule* module) { parcels = module; }

    // Loads the parcel CSV into the BST and reports the load result.
    bool load();
    // Runs the interactive Task 5 menu until staff choose to return.
    void run();
};

#endif