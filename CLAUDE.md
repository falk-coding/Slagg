# Slagg (falk elektro) – arbetsanteckningar för Claude

Analog-inspirerad trummaskin för mörk techno som VST3 (Mac + Windows) och AU (Mac), byggd med JUCE 8.0.10.
Produkt: **Slagg**, varumärke: **falk elektro** – alltid gemener (hette Falk Instruments till nov 2026; Hans tar fram ett varumärkeskit) (Hans enskilda firma ijoli). Syskon till synten **Bakelit**
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

## Gemensam låtform (Slagg + Bakelit)
`Arrangement.h` (`falk::arr`) är **identisk** i Slagg (`Source/Arrangement.h`) och Bakelit (`Source/seq/Arrangement.h`).
Ändra alltid båda samtidigt – annars hamnar breaks/drops på olika takter. Parametern heter `arrForm` i båda
(Off/Classic/Long break/Hypnotic/Short). Live räknas formen från DAW:ns markörer, vid dragning från exportområdet.
Break 32 takter (Short: 16), sista 8 = buildup; första takten efter break = drop. Hans val: 32 takter räcker.
Spåret "Sektioner" (`sectionNote`): en ton per del, C3 intro … B3 outro – för Reason, som troligen inte läser textmarkörer.

## Var saker finns
| Del | Fil |
|---|---|
| Ljudmodellerna (kick, snare, clap, rim, 3 metall, 3 toms), filter, choke, sample-uppspelning (Hermite, S.Decay) | `Source/Voices.h` |
| Sample-lager: läsa WAV/AIFF/FLAC (mono, max 10 s), inbäddning (FLAC 24 bit base64 + sökväg) | `Source/SampleLayer.*` |
| Muller (rumble): FDN-efterklang → lågpass → mättning → ducking från kicken | `Source/Rumble.h` |
| Fabrikskit (ljud + mönster, textformat för steg) – nya läggs sist | `Source/Kits.*` |
| Parametrar, ljudlista (namn, GM-not, etiketter, standardvärden) | `Source/Parameters.*` |
| Mönster (velocity, probability, ratchet, nudge, frö), generering i PPQ, MIDI-fil (typ 1, ett spår per ljud) | `Source/Pattern.*` |
| Ljudtråd, utgångar (Main + 10), transport, MIDI in, export, sample-byte (atomär pekare + retired-lista) | `Source/PluginProcessor.*` |
| GUI: ljudremsor (flikar Analog/Sample, dropp av ljudfil, vågform), rutnät, dragrutor | `Source/PluginEditor.*` |
| Skin (från Bakelit: plåt, punktljus, rattar, LookAndFeel) | `Source/Skin.*` |
| Installation | `packaging/mac`, `packaging/windows` |

## Status (2026-11-04, v0.5.0 – sample-lager)
v0.5: sample-lager per ljud. Dra in WAV/AIFF/FLAC på en remsa (eller klicka i rutan / högerklick -> ladda).
Parametrar per ljud: `Blend` (0 = exakt analogt, standard), `STune` (±24 st), `SStart`, `SDecay` (1 = hela).
Samplet blandas in före velocity/choke/Drive/Cutoff/Reso/Level/Pan/Out – samma kedja som det analoga.
Startar bara om Blend > 0 vid slaget (annars påverkas inte brusgeneratorn -> Blend 0 = bitexakt som förut).
Trådar: laddning i meddelandetråden, ljudtråden läser en atomär råpekare per ljud; gamla sample frigörs
först när två block till är klara (`collectGarbage`, från GUI-timern och vid byte). Resampling vid uppspelning
(Hermite, kvot filens frekvens / värdens) -> låter lika i 44.1/48/96 kHz.
Sparat läge: noden `Samples` (namn, sökväg, frekvens, FLAC 24 bit base64); reserv = sökvägen. Saknas noden = inga samples.
Gräns 10 s per sample, mono-mix. Släpp av fil med Blend 0 sätter Blend 50 %. Kit: Blend -> 0, samples behålls
(Hans kan ändra: alternativ är att kit tömmer samples). GUI: flikar Analog/Sample per remsa, ljudenheten 452 px hög.
Test: frekvens/S.Tune/S.Start/S.Decay/velocity i 44.1+96 kHz med 44.1+96 kHz-filer, Blend 0 bitexakt, Cutoff/Out/choke/
sekvenserare, byte mitt i slag, spara -> radera filen -> ladda, kit, stresstest med 10 samples.

### v0.4.0 – låtform
v0.4: Form (låtform) i Låt-sektionen. Trummor: intro = kick + stängd hihat, uppbyggnad = ljuden kommer in ett efter
ett, break = inga trummor, buildup = virvelrullning 1/4 -> 1/32 med paus sista åttondelen, drop = cymbal, outro = kick + hattar
som tonar ut. "Dra alla" får extraspåren Riser (en ton genom varje buildup) och Impact (ett slag per drop) + markörer.
Test: delarna, trumfritt break, accelererande rullning, cymbal på drop, Riser/Impact, markörer, live = export 104 takter.

### v0.3.0 – Låt/Song + 10 Song-kit + koppar-Cutoff
v0.3: LÅT (Song)-sektion: Evolve (täthetskurva per ljud och fras, kicken känslighet 0), Fills (fill i frasens sista
taktslag på toms/virvel + cymbal på nästa fras, hattar viker undan), Phrase (4/8/16/32 takter). Allt deterministiskt
per takt + frö -> 5 min export = uppspelning, och olika i varje takt. Test simulerar 5 min/130 BPM per Song-kit
(hihat-takter ska variera, kicken i varje takt, fills/cymbaler finns). 10 kit "Song - …" (sist i listan).
Rate/Swing/Humanize flyttade ned till sekvenseraren (rattraden ovanför rutnätet), Muller + Volume i huvudet.
Hans krav: får INTE låta statiskt; ska vara enkelt att dra in en hel låt och bara muta.
**Signatur för falk elektro:** alla Cutoff-rattar har topp av sliten koppar (`Skin.cpp`, slider-namn slutar på "cutoff").
Hans har godkänt den – finns även i Bakelit (v1.9).
Huvudet: loggan "falk elektro" längst upp till vänster, sedan produktnamnet (`drawBrandHeader` i Skin.cpp, samma som Bakelit);
versionen bara i loggans tooltip och svagt nere till höger. Loggan är provisorisk tills varumärkeskitet finns.

### v0.2.0 – etapp 2 + muller + kit
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
4. ~~Sample-lager per ljud~~ (v0.5), egna kit (spara/ladda), manual sv/en.
Viktigt att Hans testar i Reason: hur dragning med flera spår beter sig (Reason: ett spår styr en enhet → notbanor),
och hur utgångarna dyker upp på baksidan.
