#!/bin/sh
# Assembles the QML components under test next to the real Theme.qml and a
# stub Glyph, then runs every tst_*.qml with Qt's QML test runner.
#   run_qml_tests.sh <source-dir> <work-dir> [qmltestrunner]
set -e
SRC="$1"; WORK="$2"; RUNNER="${3:-qmltestrunner}"
MOD="$WORK/imports/NochQml"
rm -rf "$WORK/imports"; mkdir -p "$MOD"
cp "$SRC/qml/theme/Theme.qml" "$MOD/"
cp "$SRC/tests/qml/Glyph.qml" "$MOD/"
{
  echo "module NochQml"
  echo "singleton Theme 1.0 Theme.qml"
  echo "Glyph 1.0 Glyph.qml"
} > "$MOD/qmldir"
# Entries may be either a flat name or a path relative to qml/ (for example
# buddy/BuddyQuestionCard). Components are copied to the flat import folder
# because the real QML module exposes every type at its root.
for f in $(cat "$SRC/tests/qml/components.txt"); do
  name=$(basename "$f")
  cp "$SRC/qml/$f.qml" "$MOD/$name.qml"
  echo "$name 1.0 $name.qml" >> "$MOD/qmldir"
done
QT_QPA_PLATFORM=offscreen exec "$RUNNER" -input "$SRC/tests/qml" -import "$WORK/imports"
