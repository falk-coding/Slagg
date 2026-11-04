# Slagg (Falk Instruments) – arbetsanteckningar för Claude

Analog-inspirerad trummaskin för mörk techno som VST3 (Mac + Windows) och AU (Mac), byggd med JUCE 8.0.10.
Produkt: **Slagg**, varumärke: **Falk Instruments** (Hans enskilda firma ijoli). Syskon till synten **Bakelit**
(repo falk-coding/Bakelit) – skin, byggflöde och sekvenserarprinciper kommer därifrån.
Repo: github.com/falk-coding/Slagg. GitHub-verktygen: `owner: falk-coding`, `repo: slagg`.

## Användaren
Hans: tekniskt kunnig men inte utvecklare, svarar på svenska, vill ha korta skumbara svar (ADHD), komplett lösning direkt.
Kör Reason på Mac. Kostnadsmedveten. Säg emot om något är en dålig idé.

## Hårda regler
1. **Ändra aldrig** `PLUGIN_MANUFACTURER_CODE Falk` / `PLUGIN_CODE Slgg` eller ValueTree-typen `"Slagg"` (apvts)
   efter första release – då hittar DAW:arna inte gamla projekt.
2. **Ändra aldrig ett parameter-ID** (`Source/Parameters.h`, prefix per ljud + suffix). Nya parametrar: standardvärde
   som inte ändrar ljudet. Ändra aldrig ordningen på ljuden (index används i mönster, utgångar och bitmasker).
3. Fabrikspresets/kits (när de kommer) läggs **alltid sist**.
4. Säg aldrig att något är klart förrän bygget + `SlaggTests` är grönt (lokalt) och CI (pluginval/auval) är grön.
5. Av/på-parametrar: `AudioParameterChoice {"Off","On"}`, inte `AudioParameterBool`.
6. Synliga texter: **svenska med engelska inom parentes**; ratt-/knappnamn på engelska; `juce::String::fromUTF8` för åäö.
   Inga andras varumärken (ingen 808/909/TR i namn, presets eller texter).
7. Hans krav: MIDI-dragning ska ge **separata spår per ljud** (ett spår per ljud i filen, en dragruta per ljud),
   och varje ljud ska kunna gå till **egen utgång**.

## Bygga och testa (molnmiljön)
```bash
git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE.git /tmp/juce
sudo apt-get update && sudo apt-get install -y ninja-build libasound2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev xvfb
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/tmp/juce && cmake --build build
mkdir -p /tmp/slagg-out && cd /tmp/slagg-out && xvfb-run -a /home/user/slagg/build/SlaggTests_artefacts/Release/SlaggTests --wav-dir wav --screenshot slagg.png
```
Testerna: varje ljud (44.1/96 kHz: NaN, klipp, för tyst, klingar ut), stresstest med slumpade värden, Main/egen
utgång, choke (stängd hihat stryper öppen), sekvenserare med DAW-transport (uppspelning = export per ljud, spårnamn,
polymeter, velocity, avstängt ljud efter dragning), spara/ladda (även pluginval-liknande slumpvärden), skärmdump.
pluginval/auval körs bara i CI.

## Release
Push till main → `.github/workflows/build.yml`: Linux (tester + pluginval), macOS (`macos-15-intel` – arm64-Macarna hade kapacitetsbrist nov 2026; bygger ändå universal, ad hoc-signerat,
pluginval + auval) och Windows (pluginval). Release: `actions_run_trigger` → `run_workflow`, `build.yml`, ref `main`,
inputs `{"release_tag": "vX.Y.Z"}`; kolla `get_release_by_tag` att `Slagg-mac.zip` och `Slagg-windows.zip` finns.
Höj `project(... VERSION x.y.z)`. Byter man macOS-runner: byt även cache-nyckeln.
Commits: författare `Hans Falk <hans.falk@gmail.com>`. Pusha direkt till main.

## Var saker finns
| Del | Fil |
|---|---|
| Ljudmodellerna (kick, snare, clap, rim, 3 metall, 3 toms), filter, choke | `Source/Voices.h` |
| Muller (rumble): FDN-efterklang → lågpass → mättning → ducking från kicken | `Source/Rumble.h` |
| Fabrikskit (ljud + mönster, textformat för steg) – nya läggs sist | `Source/Kits.*` |
| Parametrar, ljudlista (namn, GM-not, etiketter, standardvärden) | `Source/Parameters.*` |
| Mönster (velocity, probability, ratchet, nudge, frö), generering i PPQ, MIDI-fil (typ 1, ett spår per ljud) | `Source/Pattern.*` |
| Ljudtråd, utgångar (Main + 10), transport, MIDI in, export | `Source/PluginProcessor.*` |
| GUI: ljudremsor, rutnät, dragrutor | `Source/PluginEditor.*` |
| Skin (från Bakelit: plåt, punktljus, rattar, LookAndFeel) | `Source/Skin.*` |
| Installation | `packaging/mac`, `packaging/windows` |

## Status (2026-11-04, v0.2.0 – etapp 2 + muller + kit)
v0.2: per steg probability/ratchet/nudge (lägesknappar i GUI), humanize, Reroll (slumpen är deterministisk per
position + frö, så export = uppspelning), Muller (rumble) med Amount/Decay/Tone/Drive/Duck, 13 kit (Init, 3 raka,
5 groove, 4 rumble). Hans önskemål: probability i procent (inte "var 4:e gång"), levande velocity, några raka och
några förskjutna kit, tydligt "rumble"-driv.

### v0.1.0
Klart: 10 analoga ljud med Tune/Decay/Tone/Snap/Drive/Cutoff/Reso/Level/Pan/Out, sekvenserare (32 steg, velocity,
egen längd per ljud, swing, rate), MIDI-dragning per ljud + "alla" mellan markörer, egna utgångar, MIDI in (GM-noter),
choke, skin. **Inget är provlyssnat av Hans än.**

**Etapper kvar:**
3. Parameter-lås per steg, villkor (1:2, fill …), mönster A/B + Fill, kedjning.
4. Sample-lager per ljud (dra in WAV, Blend/Tune/Start/Decay), egna kit (spara/ladda), manual sv/en.
Viktigt att Hans testar i Reason: hur dragning med flera spår beter sig (Reason: ett spår styr en enhet → notbanor),
och hur utgångarna dyker upp på baksidan.
