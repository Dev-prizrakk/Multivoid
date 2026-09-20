// ue_wrap/world/game_rules_pane.h -- the layout of the game's own rules pane (engine substrate).
//
// The game shows a slot's world rules in its save menu: collapsible categories, and in each a row
// per rule with a name, a description, a control (checkbox, slider, combo box) and sometimes a lock
// (a day to reach, achievements to hold). None of that is in the rules struct; it lives on the
// widgets of ui_gameRulesList, whose template tree hangs off the widget class and is in memory
// whenever the class is. This reads that tree, so our display carries the game's categories, order
// and texts without a copy of them that a game update would leave behind.
//
// A row does not name its rule. It carries the control kind and an index among the rules of that
// kind in the struct's declaration order (ue_wrap::game_rules::NthOfKind); the game's row widget
// resolves it with a hand-wired switch that follows the same order, which is re-checked with the
// struct at every game target. Principle 7: the engine read only. Game thread; the layout is fixed
// for a process, so read it once and keep it.
#pragma once

#include <string>
#include <vector>

namespace ue_wrap::game_rules_pane {

enum class Control { Check, Slider, Combo };

struct Row {
    std::string label;
    std::string description;
    Control     control = Control::Check;
    int         index = 0;            // among the rules of this control's kind, declaration order
    int         sliderDecimals = 2;   // Control::Slider: how many decimals the game shows
    int         unlockDay = 0;        // > 0: the menu lets the row be edited only past this day
    std::vector<std::string> unlockAchievements;  // non-empty: only with all of these held
};

struct Category {
    std::string      name;
    bool             hidden = false;  // the game collapses the whole category (never shown)
    std::vector<Row> rows;
};

struct Pane {
    bool                  valid = false;
    std::vector<Category> categories;  // the game's order
};

// What the game's menu weighs a row's lock against: the furthest day this player has reached in any
// world and the achievements they hold, both on the player's own profile save (mainGamemode.save_main).
// The menu also counts the days of the listed slots; in a world that is the running save's day.
struct LockInputs {
    bool                     valid = false;
    int                      maxDay = 0;
    std::vector<std::string> achievements;
};

// True when the game's menu would let `row` be edited: with achievements named, all of them held;
// otherwise the day reached (the rule of uicomp_gameRuleSlot.updEnabled). True when `in` is not valid.
bool IsUnlocked(const Row& row, const LockInputs& in);

// Read the lock inputs of the local player. False when the gamemode or its profile save is not up.
// Game thread.
bool ReadLockInputs(LockInputs& out);

// Read the pane's layout off the widget class. False (out.valid false) when the class or its tree
// is not loaded. Game thread.
bool Read(Pane& out);

}  // namespace ue_wrap::game_rules_pane
