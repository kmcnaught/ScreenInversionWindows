#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#define NUM_SAVED_RECTS 10

// Single saved rectangle entry
struct SavedRectEntry {
    RECT rect;
    bool inversionEnabled;
    bool grayscaleEnabled;
    int grayLevel;
    bool isValid;

    SavedRectEntry() : inversionEnabled(false), grayscaleEnabled(false), grayLevel(0), isValid(false) {
        memset(&rect, 0, sizeof(RECT));
    }
};

// Robust saved rectangles manager
class SavedRectanglesManager {
private:
    static const char* RECTS_FILE;
    SavedRectEntry entries[NUM_SAVED_RECTS];

    // Parse a single line from the file
    bool ParseLine(const std::string& line, int& slot, SavedRectEntry& entry);

public:
    SavedRectanglesManager();

    // Load all rectangles from file
    bool Load();

    // Save all rectangles to file
    bool Save();

    // Get a specific entry
    const SavedRectEntry& GetEntry(int slot) const;

    // Set a specific entry
    void SetEntry(int slot, const SavedRectEntry& entry);

    // Check if a slot is valid
    bool IsValid(int slot) const;

    // Save current state to file, preserving entries from other instances
    bool SavePreservingExisting();
};