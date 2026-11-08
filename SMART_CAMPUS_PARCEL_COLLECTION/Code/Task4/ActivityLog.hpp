// ============================================================================
// ActivityLog.hpp
// Task 4 - Parcel Activity and Record Management Module
// CT077-3-2-DSTR  |  Smart Campus Parcel Collection and Delivery System
//
// DATA STRUCTURE (written by us, no STL containers)
//
//   ActivityStack - a STACK built on a singly linked list. Every important
//   thing that happens to a parcel is recorded as an Activity and PUSHED on
//   top. The top is always the most recent activity.
//        push          O(1)   link a new node in front of 'top'
//        peek          O(1)   look at the most recent activity
//        pop           O(1)   unlink the top node
//        scan (one parcel's history, recent list)   O(n)   walk top to bottom
//
//   Why a stack?
//     - Undo is LAST IN, FIRST OUT: the activity staff want to reverse is
//       always the most recent one, which is exactly the top of a stack.
//       A queue would put it at the far end and need a full walk to find it.
//     - The history has no fixed size. Linked nodes grow one at a time with
//       no maximum and no resizing (an array stack would have to double).
//
//   Undo does NOT pop. The activity stays in the history with its 'reversed'
//   flag set, so "already reversed" can still be reported and the history
//   stays truthful.
//
// UNDO RULES (applied by the undo step)
//   Only the newest activity still IN EFFECT can be undone. Activities that were
//   already reversed, and the Undone records that reversed them, are passed over
//   (so several steps can be undone one after another). A live activity that
//   cannot be undone is never passed over: undo names it, says why, and stops.
//   Undoable types : Sorted, StatusUpdated, Missing, Assigned
//   Not undoable   : Registered, InfoUpdated, Removed, RequestCreated,
//                    Completed, Cancelled, Undone
//   Also refused   : a StatusUpdated that finished the parcel (Collected,
//                    Delivered, Returned); the parcel is gone or no longer has
//                    the status the activity left it in; for Assigned, the
//                    request is no longer Assigned or the resource no longer
//                    holds it. Everything is checked BEFORE anything is changed.
//   Undoing an Assigned frees the resource, sets the request back to Pending
//   and restores the parcel's earlier status. The original activity stays in
//   the history with reversed = true and an Undone activity is logged.
//
// WHAT OLD/NEW STATUS MEAN
//   oldStatus / newStatus are always the PARCEL's status before and after the
//   activity ("-" before the first activity, i.e. Registered). Request and
//   resource information is kept in requestID / resourceID ("-" = none).
//   Request status is implied by the type (Assigned = Pending -> Assigned).
//   For Assigned activities the parcel status often does not change, so
//   oldStatus == newStatus.
//
// FILE
//   activities.csv - owned by this module. Saved after EVERY change, stored
//   OLDEST FIRST, so loading it in order leaves the newest activity on top.
//   Other modules never write it; they report through logActivity().
// ============================================================================
#ifndef ACTIVITY_LOG_HPP
#define ACTIVITY_LOG_HPP

#include <string>
#include "../ActivityHook.hpp"   // ACT_* constants + ActivityEvent + ActivityHook

// ---------------------------------------------------------- activity types
// The ACT_* constants and the ActivityEvent / ActivityHook declarations
// live in ActivityHook.hpp so that Tasks 1/2/3 can include that tiny file
// instead of pulling in the whole ActivityLog module.

bool isKnownActivityType(const std::string& type);
bool isUndoableActivityType(const std::string& type);

// ----------------------------------------------------------------- record
struct Activity {
    std::string activityID;    // A001, A002, ...
    std::string parcelID;      // P001, P002, ...
    std::string type;          // one of the ACT_ constants
    std::string oldStatus;     // parcel status before ("-" for the first activity)
    std::string newStatus;     // parcel status after
    std::string requestID;     // R001, ... or "-"
    std::string resourceID;    // C01 / D01, ... or "-"
    std::string timestamp;     // "YYYY-MM-DD HH:MM"
    bool reversed = false;     // true once this activity has been undone
};

// ------------------------------------------- ActivityStack (linked-list stack)
class ActivityStack {
private:
    struct Node {
        Activity data;
        Node* next;
    };
    Node* topNode;                                 // most recent activity
    int count;

public:
    ActivityStack();
    ~ActivityStack();
    ActivityStack(const ActivityStack&) = delete;
    ActivityStack& operator=(const ActivityStack&) = delete;

    bool isEmpty() const { return count == 0; }
    int  size() const { return count; }
    void push(const Activity& a);                  // O(1)
    Activity* peek();                              // O(1), nullptr if empty
    const Activity* peek() const;
    bool pop(Activity& out);                       // O(1), false if empty
    int  toArray(const Activity** out, int max) const; // pointers, newest first
    // O(n): first activity from the top for which match() is true (nullptr if
    // none). 'position' (optional) receives how many activities were passed.
    Activity* findFirst(bool (*match)(const Activity&), int* position = nullptr);
    void clear();
};

// The undo step talks to the other modules through their public functions only
class ParcelModule;     // Task 1
class ServiceModule;    // Task 2
class ResourceManager;  // Task 3

// ----------------------------------------------------------- ActivityModule
class ActivityModule {
private:
    ActivityStack history;
    std::string activitiesPath;                    // where activities.csv lives
    int nextNumber;                                // number of the next ActivityID
    std::string lastMessage;                       // why the last logActivity failed / succeeded

    // set by connect(); only the undo step needs them
    ParcelModule* parcels;
    ServiceModule* requests;
    ResourceManager* resources;
    std::string resourcesPath;                     // resources.csv, saved after an undo

    bool readFile(bool verbose, int& loaded, int& skipped);

    // Walks the stack and copies pointers to one parcel's activities (newest
    // first) into out[], which must hold history.size() entries. Returns how
    // many matched. O(n).
    int collectForParcel(const std::string& parcelID, const Activity** out) const;

    // menu handlers (console I/O only)
    void showMenu() const;
    void menuRecent() const;
    void menuParcelHistory() const;
    void menuReview() const;
    void menuUndo();
    void menuRecord();

    Activity* liveTarget(std::string& why, int* position);   // undoTarget, but writable

public:
    // The path lets Main.cpp point the module at the shared Data folder.
    explicit ActivityModule(const std::string& activitiesFile = "activities.csv");

    bool load();                                   // read activities.csv, prints a summary
    bool save() const;                             // write activities.csv (oldest first)
    void run();                                    // interactive submenu

    // ---- integration API (no console input) ----
    // Req 1: record an activity. Pushes it on the stack and saves the file.
    // requestID / resourceID "-" (or empty) = none. Empty timestamp = now.
    // Returns false (nothing recorded) if a field is invalid or the file
    // cannot be written; lastError() says why.
    bool logActivity(const std::string& parcelID, const std::string& type,
        const std::string& oldStatus, const std::string& newStatus,
        const std::string& requestID = "-", const std::string& resourceID = "-",
        const std::string& timestamp = "");
    const std::string& lastError() const { return lastMessage; }
    int  count() const { return history.size(); }
    const Activity* latest() const { return history.peek(); }

    // Req 2: show the most recent activities, newest first
    void displayRecent(int n) const;

    // Req 3: every activity of ONE parcel, newest first (walks the whole stack,
    // O(n)). Returns how many were shown (0 = parcel has no history), or -1 if
    // the Parcel ID is not valid (an error message is printed).
    int displayParcelHistory(const std::string& parcelID) const;

    // Req 4: review one parcel = a summary built from the log (last LOGGED
    // status, counts, latest request/resource) followed by its history.
    // The status shown is the last one recorded in the log, not read live from
    // the parcel module. Return value as for displayParcelHistory().
    int reviewParcel(const std::string& parcelID) const;

    // ---- Req 5: undo ----
    // Gives the undo step access to the other modules. Without it undo refuses.
    // resourcesFile is where resources.csv is saved after an undo.
    void connect(ParcelModule* parcelModule, ServiceModule* serviceModule,
        ResourceManager* resourceManager, const std::string& resourcesFile);

    // The activity undo would reverse: the newest one that is still in effect
    // (activities already reversed, and the Undone records that reversed them,
    // are passed over). nullptr + why if there is none. 'position' = how many
    // activities were passed over to reach it (0 = the very top).
    const Activity* undoTarget(std::string& why, int* position = nullptr);

    // Checks every rule WITHOUT changing anything. false + why if undo is refused.
    bool canUndo(const Activity& a, std::string& why) const;

    // Reverses the undo target across Task 1/2/3, marks it reversed and logs an
    // Undone activity. msg says what happened or why it was refused.
    bool undoLatest(std::string& msg);
};

#endif