#include "SavedRectanglesManager.h"

const char* SavedRectanglesManager::RECTS_FILE = "saved_rects.txt";

SavedRectanglesManager::SavedRectanglesManager() {
    // Initialize all entries as invalid
}

// Parse a single line from the file
bool SavedRectanglesManager::ParseLine(const std::string& line, int& slot, SavedRectEntry& entry) {
    if (line.empty() || line[0] == '#' || line[0] == ';')
        return false;

    size_t equalPos = line.find('=');
    if (equalPos == std::string::npos)
        return false;

    std::string slotStr = line.substr(0, equalPos);
    std::string dataStr = line.substr(equalPos + 1);

    // Trim whitespace
    slotStr.erase(0, slotStr.find_first_not_of(" \t"));
    slotStr.erase(slotStr.find_last_not_of(" \t") + 1);
    dataStr.erase(0, dataStr.find_first_not_of(" \t"));
    dataStr.erase(dataStr.find_last_not_of(" \t") + 1);

    // Parse slot number
    char* endPtr;
    slot = static_cast<int>(strtol(slotStr.c_str(), &endPtr, 10));
    if (*endPtr != '\0' || slot < 0 || slot >= NUM_SAVED_RECTS)
        return false;

    // Parse comma-separated values
    std::vector<std::string> items;
    std::stringstream ss(dataStr);
    std::string item;
    while (std::getline(ss, item, ',')) {
        items.push_back(item);
    }

    if (items.size() < 4)
        return false;

    // Parse rectangle coordinates
    entry.rect.left = static_cast<LONG>(strtol(items[0].c_str(), &endPtr, 10));
    if (*endPtr != '\0') return false;
    entry.rect.top = static_cast<LONG>(strtol(items[1].c_str(), &endPtr, 10));
    if (*endPtr != '\0') return false;
    entry.rect.right = static_cast<LONG>(strtol(items[2].c_str(), &endPtr, 10));
    if (*endPtr != '\0') return false;
    entry.rect.bottom = static_cast<LONG>(strtol(items[3].c_str(), &endPtr, 10));
    if (*endPtr != '\0') return false;

    // Parse color settings (with backward compatibility)
    if (items.size() >= 7) {
        entry.inversionEnabled = (strtol(items[4].c_str(), &endPtr, 10) != 0);
        if (*endPtr != '\0') return false;
        entry.grayscaleEnabled = (strtol(items[5].c_str(), &endPtr, 10) != 0);
        if (*endPtr != '\0') return false;
        entry.grayLevel = static_cast<int>(strtol(items[6].c_str(), &endPtr, 10));
        if (*endPtr != '\0' || entry.grayLevel < 0 || entry.grayLevel > 3) return false;
    } else {
        // Default values for old format
        entry.inversionEnabled = true;
        entry.grayscaleEnabled = false;
        entry.grayLevel = 0;
    }

    entry.isValid = true;
    return true;
}

// Load all rectangles from file
bool SavedRectanglesManager::Load() {
    std::ifstream file(RECTS_FILE);
    if (!file.is_open())
        return false;

    std::string line;
    while (std::getline(file, line)) {
        int slot;
        SavedRectEntry entry;
        if (ParseLine(line, slot, entry)) {
            entries[slot] = entry;
        }
    }

    file.close();
    return true;
}

// Save all rectangles to file
bool SavedRectanglesManager::Save() {
    std::ofstream file(RECTS_FILE);
    if (!file.is_open())
        return false;

    file << "# Saved Rectangle Configurations with Color Settings\n";
    file << "# Format: SlotNumber=Left,Top,Right,Bottom,Invert,Grayscale,GrayLevel\n";
    file << "# Slots 1-9 available. Use 0 to cycle, 1-9 to load, Ctrl+1-9 to save.\n";
    file << "# Invert: 1=enabled, 0=disabled\n";
    file << "# Grayscale: 1=enabled, 0=disabled\n";
    file << "# GrayLevel: 0=100%, 1=80%, 2=60%, 3=40%\n\n";

    for (int i = 0; i < NUM_SAVED_RECTS; i++) {
        if (entries[i].isValid) {
            file << i << "="
                 << entries[i].rect.left << ","
                 << entries[i].rect.top << ","
                 << entries[i].rect.right << ","
                 << entries[i].rect.bottom << ","
                 << (entries[i].inversionEnabled ? 1 : 0) << ","
                 << (entries[i].grayscaleEnabled ? 1 : 0) << ","
                 << entries[i].grayLevel << "\n";
        }
    }

    file.close();
    return true;
}

// Get a specific entry
const SavedRectEntry& SavedRectanglesManager::GetEntry(int slot) const {
    static SavedRectEntry invalid;
    if (slot < 0 || slot >= NUM_SAVED_RECTS)
        return invalid;
    return entries[slot];
}

// Set a specific entry
void SavedRectanglesManager::SetEntry(int slot, const SavedRectEntry& entry) {
    if (slot >= 0 && slot < NUM_SAVED_RECTS) {
        entries[slot] = entry;
    }
}

// Check if a slot is valid
bool SavedRectanglesManager::IsValid(int slot) const {
    if (slot < 0 || slot >= NUM_SAVED_RECTS)
        return false;
    return entries[slot].isValid;
}

// Save current state to file, preserving entries from other instances
bool SavedRectanglesManager::SavePreservingExisting() {
    // Load current file state
    SavedRectanglesManager fileState;
    fileState.Load();

    // Merge our valid entries into the file state
    for (int i = 0; i < NUM_SAVED_RECTS; i++) {
        if (entries[i].isValid) {
            fileState.SetEntry(i, entries[i]);
        }
    }

    // Save the merged state
    return fileState.Save();
}