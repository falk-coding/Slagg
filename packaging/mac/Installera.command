#!/bin/bash
# Installerar Slagg (Falk Instruments) – VST3 + AU – för den inloggade användaren och tar bort
# macOS-karantänen så att DAW:en kan ladda pluginet. Ingen admin-behörighet behövs.
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

echo "Installerar Slagg …"
rm -rf "$VST3_DIR/Slagg.vst3" "$AU_DIR/Slagg.component"
cp -R Slagg.vst3 "$VST3_DIR/"
cp -R Slagg.component "$AU_DIR/"
xattr -dr com.apple.quarantine "$VST3_DIR/Slagg.vst3" "$AU_DIR/Slagg.component" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Klart!"
echo "  VST3: $VST3_DIR/Slagg.vst3"
echo "  AU:   $AU_DIR/Slagg.component"
echo ""
echo "Starta om din DAW. Slagg finns under Instruments → Falk Instruments → Slagg."
echo ""
read -n 1 -s -r -p "Tryck på valfri tangent för att stänga."
