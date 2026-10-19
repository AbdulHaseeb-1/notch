#!/bin/sh
# Assembles the QML components under test next to the real Theme.qml and a
# stub Glyph, then runs every tst_*.qml with Qt's QML test runner.
#   run_qml_tests.sh <source-dir> <work-dir> [qmltestrunner]
set -e
SRC="$1"; WORK="$2"; RUNNER="${3:-qmltestrunner}"
MOD="$WORK/imports/NochQml"
rm -rf "$WORK/imports"; mkdir -p "$MOD"
cp "$SRC/qml/Theme.qml" "$MOD/"
cp "$SRC/tests/qml/Glyph.qml" "$MOD/"
{
  echo "module NochQml"
  echo "singleton Theme 1.0 Theme.qml"
  echo "Glyph 1.0 Glyph.qml"
} > "$MOD/qmldir"
for f in $(cat "$SRC/tests/qml/components.txt"); do
  cp "$SRC/qml/$f.qml" "$MOD/"
  echo "$f 1.0 $f.qml" >> "$MOD/qmldir"
done
QT_QPA_PLATFORM=offscreen exec "$RUNNER" -input "$SRC/tests/qml" -import "$WORK/imports"
