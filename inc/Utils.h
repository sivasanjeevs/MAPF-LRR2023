#pragma once
#include <string>
#include "ActionModel.h" // Needed for the Action enum

// Helper function to convert Action enum to string
inline std::string action_to_string_local(Action action) {
    switch (action) {
        case Action::FW:  return "F";
        case Action::CR:  return "R";
        case Action::CCR: return "C";
        case Action::W:   return "W";
        case Action::NA:  return "T";
        default:          return "?";
    }
}

// Helper function to convert orientation int to string
inline std::string orientation_to_string_local(int orientation) {
    switch (orientation) {
        case 0: return "E";
        case 1: return "S";
        case 2: return "W";
        case 3: return "N";
        default: return "";
    }
}