#include "Task3/ResourceQueue.hpp"
#include <fstream>
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

int main() {
    ResourceManager manager;
    manager.loadFromCSV(findDataFile("resources.csv"));
    runTask3Menu(manager);
    return 0;
}