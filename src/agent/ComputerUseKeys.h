#pragma once

// Lightweight pure-logic key-combo helpers from Buddy's computer use.
// Split out of ComputerUse.h so the permission policy (AgentPolicy, which
// must not depend on QtGui) can share the canonical risky-key and
// shell-text definitions instead of duplicating them.

#include <QString>
#include <QStringList>

namespace cu {

namespace detail {
// Shared with ComputerUseActions.cpp (parseModifierList).
QString canonicalModifier(const QString &t);
QString canonicalKey(const QString &t);
} // namespace detail

struct KeyCombo {
    QStringList modifiers;   // canonical: ctrl, alt, shift, super
    QString key;             // canonical key name: a, f4, return, escape, ...
    QString error;
    bool valid() const { return error.isEmpty(); }
    QString toString() const;
};

KeyCombo parseKeyCombo(const QString &combo);
// Combos that open a run dialog, a terminal, task manager, lock or sign out:
// fine for the user, but asked for even in Trusted mode once the agent has
// read the screen (which may be showing it instructions).
bool isRiskyKeyCombo(const KeyCombo &combo);

// True when pressing this combo would type text (a bare or shift-modified
// printable character) rather than a navigation key or shortcut.
bool comboTypesText(const KeyCombo &combo);
// Typed text that reads like a shell command or URL fetch.
bool looksLikeShellText(const QString &text);

} // namespace cu
