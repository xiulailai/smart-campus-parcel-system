# Smart Campus Parcel System

A console application in C++ that manages parcels arriving at a university campus, from the moment they are registered until they are collected, delivered or returned. It is built around five modules, and **every data structure is hand-written, with no STL containers** (only `std::string` is used).

Built as a group project (Group P) for CT077-3-2-DSTR Data Structures at Asia Pacific University (APU).

## How it works

A parcel moves through the system in a fixed flow, and each module owns one step:

1. **Intake (Task 1).** A parcel is registered and waits in a FIFO queue until staff sort it. Sorting routes it to collection, campus delivery, special handling, return to sender or hold.
2. **Service request (Task 2).** A sorted parcel gets a service request with an urgency from 1 to 3. Requests are handled by priority.
3. **Resource assignment (Task 3).** The next request is given a free collection counter or delivery person.
4. **Activity history (Task 4).** Every change made by the modules above is recorded, and the most recent activity can be undone.
5. **Category organisation (Task 5).** Staff can organise, search and maintain parcel records by Parcel ID, Recipient ID, Parcel Type or Status.

The main menu also has combined options that run steps across modules, for example "sort the next parcel and open its request" and "assign the next request to a resource I choose".

## Data structures

| Task | Module | Data structure | Why it fits |
|---|---|---|---|
| 1 | Parcel intake, sorting and processing | **FIFO queue** on a singly linked list, plus a **dynamic array** of all parcels kept in Parcel ID order | Parcels are sorted in the order they arrive, while records still need ordered lookup |
| 2 | Service requests | **Priority queue** on a plain array, which stores indices into a request store | Highest urgency first, then earliest request time, then smallest request ID. Enqueue is O(1); peek, dequeue and cancel are O(n) |
| 3 | Counters and delivery resources | **Fixed-capacity array queues** (front, rear and count), one for counters and one for delivery staff | Resources are used in turn, and the number of counters and delivery staff is small and fixed (up to 10 in each queue) |
| 4 | Activity history and undo | **Stack** on a singly linked list | Undo is last in, first out. Push, peek and pop are O(1), and the history can grow without a size limit |
| 5 | Parcel category organisation | **Binary search tree**, where each node holds a linked chain of parcels sharing the same key | Search and insert are O(h). The key can be switched between Parcel ID, Recipient ID, Parcel Type and Status |

### Parcel status flow

`Registered` leads to `ReadyForCollection`, `AssignedForDelivery`, `SpecialHandling`, `ReturnToSender`, `OnHold` or `Missing`, and finally ends as `Collected`, `Delivered`, `Returned` or `Cancelled`.

## Features

**Task 1: Parcel intake.** Register parcels, display and search records (by Parcel ID, Recipient ID or part of a name), view details, show the intake queue, sort the next parcel, update status or delivery information, report a parcel missing and remove a record. Input is validated (recipient ID format, duplicate parcels, future arrival times).

**Task 2: Service requests.** Create, view, cancel, assign and complete requests. Requests are shown in priority order, and a collection can only be completed after the collector's Recipient ID is checked.

**Task 3: Counters and delivery resources.** Display and register resources, assign a request to the next free resource or to one you choose, release resources and update their status. Resources are saved to file.

**Task 4: Activity history and undo.** Display recent activities, view the history of one parcel, review a parcel, record an activity by hand and undo the most recent one. Undo is checked before anything changes, and an undone activity stays in the history marked as reversed, so the record stays truthful.

**Task 5: Parcel categories.** Rebuild the tree under a chosen key, insert, search, update and remove parcel records, and reload from file. Records that are still in use by an active request cannot be removed.

### How the modules talk to each other

Tasks 1, 2 and 3 report events to Task 4 through a small shared header (`ActivityHook.hpp`) and a function pointer, so no module depends on Task 4 directly and there are no circular dependencies. Task 3 uses two hooks into Task 2 in the same way. `Main.cpp` connects everything.

## Project structure

```
.
├── Data/                              Sample data (all fictional)
│   ├── parcels.csv
│   ├── requests.csv
│   ├── resources.csv
│   └── activities.csv
└── SMART_CAMPUS_PARCEL_COLLECTION/
    ├── Code/
    │   ├── Main.cpp                   Main menu and cross-module flows
    │   ├── ActivityHook.hpp           Shared bridge to the activity log
    │   ├── Task1/                     ParcelIntake
    │   ├── Task2/                     ServiceRequest
    │   ├── Task3/                     ResourceQueue
    │   ├── Task4/                     ActivityLog
    │   └── Task5/                     ParcelCategoryTree
    ├── Tests/
    │   ├── SystemTests.cpp
    │   ├── run_tests.sh               Linux and macOS
    │   └── run_tests.bat              Windows
    └── SMART_CAMPUS_PARCEL_COLLECTION.vcxproj
```

## Getting started

### Visual Studio (Windows)
1. Open `SMART_CAMPUS_PARCEL_COLLECTION/SMART_CAMPUS_PARCEL_COLLECTION.slnx`. The project uses the v145 toolset and C++20.
2. Build and run (F5). The program finds the `Data` folder automatically.

### Command line (g++ or clang++)
```bash
g++ -std=c++20 SMART_CAMPUS_PARCEL_COLLECTION/Code/Main.cpp SMART_CAMPUS_PARCEL_COLLECTION/Code/Task*/*.cpp -o smartcampus
```
On Linux and macOS the program looks for the CSV files in the current folder, so run it from a folder that holds a copy of them:
```bash
mkdir run && cp Data/*.csv run/ && cd run && ../smartcampus
```

The program **saves changes back to the CSV files**. To keep the sample data as it is, work on a copy, as above.

## Tests

```bash
bash SMART_CAMPUS_PARCEL_COLLECTION/Tests/run_tests.sh      # Linux / macOS
SMART_CAMPUS_PARCEL_COLLECTION\Tests\run_tests.bat          # Windows
```

The script builds the project with AddressSanitizer and UndefinedBehaviorSanitizer, so memory errors count as failures. It runs:
1. **241 unit and integration tests** across Tasks 1 to 5 and the way they work together
2. **A scripted run of the real program** on a copy of the data (sort, create a request, assign it, check that everything is logged and saved)
3. **A check that every menu returns** when the input ends, so no menu can loop forever

The real `Data` folder is never modified by the tests.

## Sample data

`Data/` holds a small made-up data set: 40 parcels, 19 service requests, 9 resources (counters and delivery staff) and 42 logged activities. All names and IDs are fictional.

## Team

Group P, CT077-3-2-DSTR, Asia Pacific University (APU).

| Task | Module | Member |
|---|---|---|
| 1 | Parcel intake, sorting and processing | [@2FoR1](https://github.com/2FoR1) |
| 2 | Service requests | [@Xylore9](https://github.com/Xylore9) |
| 3 | Counters and delivery resources | [@xiulailai](https://github.com/xiulailai) |
| 4 | Activity history and undo | [@WilliamChuah](https://github.com/WilliamChuah) |
| 5 | Parcel category organisation | [@7786z](https://github.com/7786z) |

## License

Released under the [MIT License](LICENSE). Copyright (c) 2026 automaticicemaker Contributors and APU.
