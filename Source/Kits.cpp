#include "Kits.h"

namespace slagg
{
namespace
{
    using namespace dsp;

    // Liten byggare så att kiten går att läsa: K ("Namn", "Kategori").v (kick, "Tune", -3).p (kick, "X . . . ")
    struct K
    {
        Kit kit;
        K (const char* name, const char* category) { kit.name = name; kit.category = category; }
        K& g (const char* id, float value) { kit.values.push_back ({ id, value }); return *this; }
        K& v (int voice, const char* suffix, float value) { kit.values.push_back ({ pid::id (voice, suffix), value }); return *this; }
        K& p (int voice, const char* steps) { kit.steps[(size_t) voice] = steps; return *this; }
        K& len (int voice, int steps) { return v (voice, pid::sLen, (float) steps); }
        K& seed (uint32_t s) { kit.seed = s; return *this; }
        K& rumble (float amt, float decay, float tone, float drive, float duck)
        {
            return g (pid::rumbleAmt, amt).g (pid::rumbleDecay, decay).g (pid::rumbleTone, tone)
                  .g (pid::rumbleDrive, drive).g (pid::rumbleDuck, duck);
        }
        operator Kit() const { return kit; }
    };

    // Mörk, rak standardkick för techno.
    K& darkKick (K& k, float tune = -3, float decay = 0.5f)
    {
        return k.v (kick, pid::sTune, tune).v (kick, pid::sDecay, decay).v (kick, pid::sTone, 0.4f)
                .v (kick, pid::sSnap, 0.3f).v (kick, pid::sDrive, 0.25f).v (kick, pid::sCutoff, 6000);
    }
}

const std::vector<Kit>& getKits()
{
    static const std::vector<Kit> kits = []
    {
        std::vector<Kit> list;

        // ---------------------------------------------------------------- Bas
        list.push_back (K ("Init", "Basic")
            .p (kick,      "X . . . X . . . X . . . X . . .")
            .p (clap,      ". . . . X . . . . . . . X . . .")
            .p (closedHat, "o . x . o . x . o . x . o . x o?50")
            .p (openHat,   ". . . . . . . . . . . . . . x ."));

        // ---------------------------------------------------------------- Raka
        {
            K k ("Bunker Straight", "Straight");
            darkKick (k);
            k.v (clap, pid::sLevel, 0.55f).v (clap, pid::sTone, 0.35f)
             .v (openHat, pid::sDecay, 0.3f).v (closedHat, pid::sDecay, 0.2f).v (rim, pid::sLevel, 0.45f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (clap,      ". . . . X . . . . . . . X . . .")
             .p (closedHat, "x - o - x - o - x - o - x - o -?70")
             .p (openHat,   ". . X . . . x . . . X . . . x .")
             .p (rim,       ". . . . . . . -?35 . . . . . . -?45 .")
             .g (pid::humanize, 0.12f);
            list.push_back (k);
        }
        {
            K k ("Steel Pulse", "Straight");
            darkKick (k, -2, 0.4f);
            k.v (kick, pid::sSnap, 0.55f)
             .v (snare, pid::sTone, 0.3f).v (snare, pid::sSnap, 0.6f).v (snare, pid::sLevel, 0.55f)
             .v (closedHat, pid::sDecay, 0.15f).v (closedHat, pid::sTone, 0.7f)
             .v (cymbal, pid::sTone, 0.7f).v (cymbal, pid::sDecay, 0.55f).v (cymbal, pid::sLevel, 0.35f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (snare,     ". . . . X . . . . . . . X . . .")
             .p (closedHat, "X o x o X o x o X o x o X o x o")
             .p (cymbal,    "X?50 . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .")
             .len (cymbal, 32)
             .rumble (0.25f, 0.4f, 150, 0.3f, 0.6f);
            list.push_back (k);
        }
        {
            K k ("Night Shift", "Straight");
            darkKick (k, -4, 0.55f);
            // Probability i stället för fasta mönster: hattarna spelar "ofta men inte alltid".
            k.v (closedHat, pid::sDecay, 0.22f).v (openHat, pid::sDecay, 0.35f).v (clap, pid::sLevel, 0.5f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (clap,      ". . . . X . . . . . . . X . . x?20")
             .p (closedHat, "x?66 -?40 x?66 o?66 x?66 -?40 x?66 o?66 x?66 -?40 x?66 o?66 x?66 -?40 x?66 o?66")
             .p (openHat,   ". . x?80 . . . x?80 . . . x?80 . . . X?90 .")
             .g (pid::humanize, 0.15f)
             .rumble (0.3f, 0.45f, 140, 0.35f, 0.7f);
            list.push_back (k);
        }

        // ---------------------------------------------------------------- Groove (förskjutet)
        {
            K k ("Late Hats", "Groove");
            darkKick (k);
            // Hattarna ligger lite efter takten (nudge) – lägger sig "bakom" kicken.
            k.v (closedHat, pid::sDecay, 0.2f).v (openHat, pid::sDecay, 0.3f).v (clap, pid::sLevel, 0.55f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (clap,      ". . . . X>4 . . . . . . . X>4 . . .")
             .p (closedHat, "x>10 -?55 o>14 -?55 x>10 -?55 o>14 x?40>8 x>10 -?55 o>14 -?55 x>10 -?55 o>14 x?40>8")
             .p (openHat,   ". . X>12 . . . x>12 . . . X>12 . . . x>12 .")
             .g (pid::swing, 0.2f).g (pid::humanize, 0.2f);
            list.push_back (k);
        }
        {
            K k ("Broken Shuffle", "Groove");
            darkKick (k, -2, 0.45f);
            k.v (closedHat, pid::sDecay, 0.18f).v (snare, pid::sLevel, 0.5f).v (rim, pid::sLevel, 0.5f)
             .p (kick,      "X . . . X . . x?66 . . X . X . . .")
             .p (snare,     ". . . . X . . -?40 . -?30 . . X . . -?50")
             .p (closedHat, "x - o x?70 x - o -?60 x - o x?70 x - o x*2?35")
             .p (openHat,   ". . . . . . x . . . . . . . x?60 .")
             .p (rim,       ". . o?50 . . . . . . . o?50 . . o?30 . .")
             .g (pid::swing, 0.45f).g (pid::humanize, 0.25f);
            list.push_back (k);
        }
        {
            K k ("Ghost Train", "Groove");
            darkKick (k, -3, 0.5f);
            // Spökslag på virveln, ratchets på hattarna – rullar framåt.
            k.v (snare, pid::sTone, 0.4f).v (snare, pid::sSnap, 0.45f).v (closedHat, pid::sDecay, 0.16f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (snare,     ". -?40 . -?30 X . -?50 . . -?40 . . X . -?40 -?60")
             .p (closedHat, "x . x*2?40 . x . x . x . x*3?30 . x . x*2?50 .")
             .p (openHat,   ". . . . . . . . . . . . . . o .")
             .g (pid::humanize, 0.3f)
             .rumble (0.2f, 0.4f, 160, 0.3f, 0.6f);
            list.push_back (k);
        }
        {
            K k ("Polymeter Drift", "Groove");
            darkKick (k);
            // Olika längder: hattarna 12, öppna 7, kant 5 steg – mönstret vänder sig hela tiden.
            k.v (closedHat, pid::sDecay, 0.2f).v (openHat, pid::sDecay, 0.3f).v (rim, pid::sTone, 0.7f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (closedHat, "x o - x o - x o?60 - x o -?50")
             .p (openHat,   ". . x . . o?60 .")
             .p (rim,       "o . . x?66 .")
             .len (closedHat, 12).len (openHat, 7).len (rim, 5)
             .g (pid::humanize, 0.15f);
            list.push_back (k);
        }
        {
            K k ("Off-grid Toms", "Groove");
            darkKick (k, -4, 0.45f);
            k.v (tomLow, pid::sTune, -5).v (tomLow, pid::sDecay, 0.4f).v (tomMid, pid::sTune, -4)
             .v (tomHigh, pid::sTune, -3).v (tomMid, pid::sSnap, 0.35f).v (closedHat, pid::sDecay, 0.15f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (tomLow,    ". . . x?66 . . o<6 . . . . x>10 . . o?50 .")
             .p (tomMid,    ". . . . . o?60 . . . x>8 . . . -?40 . .")
             .p (tomHigh,   ". x?40 . . . . . . . . . . -?50 . . x?30")
             .p (closedHat, ". . x . . . x . . . x . . . x .")
             .g (pid::swing, 0.15f).g (pid::humanize, 0.25f);
            list.push_back (k);
        }

        // ---------------------------------------------------------------- Muller (rumble)
        {
            K k ("Concrete Rumble", "Rumble");
            darkKick (k, -4, 0.55f);
            k.v (closedHat, pid::sDecay, 0.18f).v (openHat, pid::sDecay, 0.28f).v (clap, pid::sLevel, 0.5f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (clap,      ". . . . x?66 . . . . . . . X . . .")
             .p (closedHat, ". . x . . . x . . . x . . . x -?50")
             .p (openHat,   ". . . . . . x?50 . . . . . . . x .")
             .rumble (0.7f, 0.55f, 140, 0.5f, 0.75f);
            list.push_back (k);
        }
        {
            K k ("Deep Hall Rumble", "Rumble");
            darkKick (k, -5, 0.4f);
            k.v (closedHat, pid::sDecay, 0.12f).v (closedHat, pid::sTone, 0.75f).v (rim, pid::sLevel, 0.4f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (closedHat, "- . x - - . x -?60 - . x - - . x x?40")
             .p (rim,       ". . . . . . . o?40 . . . . . . . .")
             .rumble (0.85f, 0.8f, 110, 0.35f, 0.85f)
             .g (pid::humanize, 0.1f);
            list.push_back (k);
        }
        {
            K k ("Warehouse 4AM", "Rumble");
            darkKick (k, -3, 0.5f);
            // Hypnotiskt: ride på åttondelar, kant i 7 steg, muller som rullar.
            k.v (cymbal, pid::sTone, 0.75f).v (cymbal, pid::sDecay, 0.3f).v (cymbal, pid::sLevel, 0.3f)
             .v (rim, pid::sTone, 0.65f).v (closedHat, pid::sDecay, 0.18f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (cymbal,    "x . o?80 . x . o?80 . x . o?80 . x . o?80 .")
             .p (rim,       "o . . x?66 . -?50 .")
             .p (closedHat, ". . x . . . x . . . x . . . x .")
             .len (rim, 7)
             .rumble (0.6f, 0.6f, 130, 0.45f, 0.7f)
             .g (pid::humanize, 0.2f);
            list.push_back (k);
        }
        {
            K k ("Sparse Ritual", "Rumble");
            darkKick (k, -5, 0.6f);
            // Brutet och glest: toms och kick med sannolikheter ovanpå tungt muller.
            k.v (tomLow, pid::sTune, -7).v (tomLow, pid::sDecay, 0.55f).v (tomMid, pid::sTune, -5)
             .v (clap, pid::sDecay, 0.6f).v (clap, pid::sLevel, 0.45f)
             .p (kick,      "X . . . . . X?66 . . . X . . . . .")
             .p (tomLow,    ". . . o?50 . . . . . x?66 . . . . o<8 .")
             .p (tomMid,    ". . . . . . . -?40 . . . . . o?50 . .")
             .p (clap,      ". . . . . . . . . . . . X . . .")
             .p (closedHat, ". . x?66 . . . x?66 . . . x?66 . . . x?66 .")
             .rumble (0.8f, 0.7f, 120, 0.55f, 0.8f)
             .g (pid::humanize, 0.3f);
            list.push_back (k);
        }

        // ---------------------------------------------------------------- Song (v0.3, läggs sist)
        // Byggda för att dras in som en hel låt mellan markörerna: Evolve låter varje ljud andas över minuter,
        // Fills ger fills i slutet av varje fras och cymbal på nästa fras. Bara att muta i DAW:n där det passar.
        auto song = [] (K& k, float evolve, float fills, int phraseIndex)
        {
            k.g (pid::evolve, evolve).g (pid::fills, fills).g (pid::phrase, (float) phraseIndex);
        };
        auto songToms = [] (K& k)
        {
            k.v (tomLow, pid::sTune, -5).v (tomMid, pid::sTune, -4).v (tomHigh, pid::sTune, -3)
             .v (tomLow, pid::sLevel, 0.6f).v (tomMid, pid::sLevel, 0.55f).v (tomHigh, pid::sLevel, 0.5f);
        };
        {
            K k ("Song - Bunker Night", "Song");
            darkKick (k, -4, 0.55f); songToms (k);
            k.v (closedHat, pid::sDecay, 0.2f).v (openHat, pid::sDecay, 0.3f).v (clap, pid::sLevel, 0.5f)
             .v (rim, pid::sLevel, 0.4f).v (cymbal, pid::sTone, 0.7f).v (cymbal, pid::sLevel, 0.35f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (clap,      ". . . . X . . . . . . . X?90 . . .")
             .p (closedHat, "x - o -?60 x - o x?40 x - o -?60 x - o - x - o -?60 x?80 - o x?40 x - o -?60 x - o x*2?30")
             .p (openHat,   ". . X . . . X?80 . . . X . . . X?70 .")
             .p (rim,       ". . . . . . . -?40 . . -?25 . . . -?45 .")
             .p (tomLow,    ". . . . . . . . . . . . . . . . . . . . . . . . . . . o?40 . . . .")
             .len (closedHat, 32).len (tomLow, 32)
             .g (pid::humanize, 0.15f).rumble (0.5f, 0.55f, 140, 0.45f, 0.75f).seed (11);
            song (k, 0.6f, 0.5f, 1);
            list.push_back (k);
        }
        {
            K k ("Song - Concrete Drive", "Song");
            darkKick (k, -3, 0.5f); songToms (k);
            k.v (closedHat, pid::sDecay, 0.17f).v (openHat, pid::sDecay, 0.28f).v (snare, pid::sLevel, 0.5f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (snare,     ". . . . X . . -?30 . . . . X . -?40 .")
             .p (closedHat, "x>8 -?50 o>12 - x>8 -?50 o>12 x?40 x>8 -?50 o>12 - x>8 x?30 o>12 -?60")
             .p (openHat,   ". . x>10 . . . x>10 . . . x>10 . . . X>10 .")
             .p (tomMid,    ". . . . . . . . . . . o?35 . . . . . . . . . . . . . . . . . -?30 . .")
             .len (tomMid, 32)
             .g (pid::swing, 0.15f).g (pid::humanize, 0.2f).rumble (0.35f, 0.45f, 150, 0.4f, 0.7f).seed (23);
            song (k, 0.7f, 0.4f, 2);
            list.push_back (k);
        }
        {
            K k ("Song - Deep Tunnel", "Song");
            darkKick (k, -5, 0.45f); songToms (k);
            k.v (closedHat, pid::sDecay, 0.12f).v (closedHat, pid::sTone, 0.75f).v (rim, pid::sLevel, 0.35f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (closedHat, "- . x - - . x?80 - - . x - - . x x?40")
             .p (openHat,   ". . . . . . . . . . . . . . x?50 .")
             .p (rim,       ". . . . . . . o?35 . . . . . . . . . . . . . . . . . . . . . -?40 . .")
             .len (rim, 32)
             .g (pid::humanize, 0.1f).rumble (0.8f, 0.75f, 110, 0.4f, 0.85f).seed (37);
            song (k, 0.8f, 0.35f, 1);
            list.push_back (k);
        }
        {
            K k ("Song - Ritual Toms", "Song");
            darkKick (k, -4, 0.55f); songToms (k);
            k.v (tomLow, pid::sDecay, 0.5f).v (tomMid, pid::sSnap, 0.3f).v (closedHat, pid::sDecay, 0.15f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (tomLow,    ". . . x?66 . . o<6 . . . . x>10 . . o?50 .")
             .p (tomMid,    ". . o?50 . . -?40 . . x?60 . . .")
             .p (tomHigh,   ". x?35 . . . . -?50 . . x?30")
             .p (closedHat, ". . x . . . x?80 . . . x . . . x -?40")
             .p (clap,      ". . . . . . . . . . . . X?75 . . .")
             .len (tomMid, 12).len (tomHigh, 10)
             .g (pid::swing, 0.12f).g (pid::humanize, 0.25f).rumble (0.4f, 0.5f, 130, 0.45f, 0.7f).seed (41);
            song (k, 0.7f, 0.6f, 1);
            list.push_back (k);
        }
        {
            K k ("Song - Ghost Factory", "Song");
            darkKick (k, -3, 0.5f); songToms (k);
            k.v (snare, pid::sTone, 0.4f).v (snare, pid::sSnap, 0.45f).v (closedHat, pid::sDecay, 0.16f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (snare,     ". -?40 . -?30 X . -?50 . . -?40 . . X . -?40 -?60")
             .p (closedHat, "x . x*2?35 . x . x . x . x*3?25 . x . x*2?45 . x . x . x . x?70 . x . x*2?40 . x . x*4?20 .")
             .p (openHat,   ". . . . . . . . . . . . . . o .")
             .len (closedHat, 32)
             .g (pid::humanize, 0.3f).rumble (0.25f, 0.4f, 160, 0.3f, 0.6f).seed (53);
            song (k, 0.6f, 0.5f, 1);
            list.push_back (k);
        }
        {
            K k ("Song - Steel Rails", "Song");
            darkKick (k, -2, 0.42f); songToms (k);
            k.v (kick, pid::sSnap, 0.5f).v (cymbal, pid::sTone, 0.75f).v (cymbal, pid::sDecay, 0.3f)
             .v (cymbal, pid::sLevel, 0.3f).v (closedHat, pid::sDecay, 0.15f).v (snare, pid::sLevel, 0.5f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (snare,     ". . . . X . . . . . . . X . . .")
             .p (cymbal,    "x . o?80 . x . o?80 . x . o?80 . x . o?80 .")
             .p (closedHat, "X o x o X o x o?70 X o x o X o x o")
             .g (pid::humanize, 0.12f).rumble (0.2f, 0.4f, 150, 0.3f, 0.6f).seed (67);
            song (k, 0.55f, 0.5f, 2);
            list.push_back (k);
        }
        {
            K k ("Song - Night Shuffle", "Song");
            darkKick (k, -3, 0.5f); songToms (k);
            k.v (closedHat, pid::sDecay, 0.18f).v (rim, pid::sLevel, 0.45f)
             .p (kick,      "X . . . X . . x?50 . . X . X . . . X . . . X . . . . . X . X . . x?40")
             .p (closedHat, "x - o x?70 x - o -?60 x - o x?70 x - o x*2?35")
             .p (openHat,   ". . . . . . x . . . . . . . x?60 .")
             .p (rim,       ". . o?50 . . . . . . . o?50 . . o?30 . .")
             .len (kick, 32)
             .g (pid::swing, 0.35f).g (pid::humanize, 0.25f).rumble (0.4f, 0.5f, 140, 0.45f, 0.7f).seed (71);
            song (k, 0.65f, 0.45f, 1);
            list.push_back (k);
        }
        {
            K k ("Song - Hypno Loop", "Song");
            darkKick (k, -4, 0.5f); songToms (k);
            // Polymeter 12/7/5/9 mot 4/4 – mönstret upprepar sig nästan aldrig.
            k.v (closedHat, pid::sDecay, 0.2f).v (openHat, pid::sDecay, 0.3f).v (rim, pid::sTone, 0.7f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (closedHat, "x o - x o - x o?60 - x o -?50")
             .p (openHat,   ". . x . . o?60 .")
             .p (rim,       "o . . x?66 .")
             .p (tomMid,    ". . . . -?40 . . o?30 .")
             .len (closedHat, 12).len (openHat, 7).len (rim, 5).len (tomMid, 9)
             .g (pid::humanize, 0.15f).rumble (0.45f, 0.6f, 130, 0.4f, 0.75f).seed (83);
            song (k, 0.85f, 0.3f, 2);
            list.push_back (k);
        }
        {
            K k ("Song - Warehouse Peak", "Song");
            darkKick (k, -2, 0.45f); songToms (k);
            k.v (kick, pid::sSnap, 0.45f).v (closedHat, pid::sDecay, 0.16f).v (openHat, pid::sDecay, 0.3f)
             .v (clap, pid::sLevel, 0.55f).v (cymbal, pid::sLevel, 0.35f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (clap,      ". . . . X . . . . . . . X . . x?25")
             .p (closedHat, "X o x o X o x o X o x o X o x o*2?40")
             .p (openHat,   ". . X . . . X . . . X . . . X .")
             .p (cymbal,    "X?60 . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .")
             .len (cymbal, 32)
             .g (pid::humanize, 0.12f).rumble (0.3f, 0.45f, 150, 0.4f, 0.7f).seed (97);
            song (k, 0.5f, 0.7f, 1);
            list.push_back (k);
        }
        {
            K k ("Song - After Hours", "Song");
            darkKick (k, -5, 0.6f); songToms (k);
            k.v (closedHat, pid::sDecay, 0.14f).v (rim, pid::sTone, 0.65f).v (rim, pid::sLevel, 0.4f)
             .p (kick,      "X . . . X . . . X . . . X . . .")
             .p (closedHat, ". . x?70 . . . x?70 -?40 . . x?70 . . . x?70 .")
             .p (rim,       "o . . x?60 . -?40 .")
             .p (openHat,   ". . . . . . . . . . . . . . x?40 .")
             .len (rim, 7)
             .g (pid::humanize, 0.2f).rumble (0.65f, 0.65f, 120, 0.5f, 0.8f).seed (101);
            song (k, 0.6f, 0.35f, 2);
            list.push_back (k);
        }
        return list;
    }();
    return kits;
}

void applyKit (juce::AudioProcessorValueTreeState& apvts, const Kit& kit, seq::Pattern& pattern)
{
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || rp->getParameterID().endsWith (pid::sOut))
            continue;   // utgångsvalet hör till användarens mixer, inte till kitet
        rp->setValueNotifyingHost (rp->getDefaultValue());
    }
    for (const auto& [id, value] : kit.values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));

    pattern.clear();
    for (int v = 0; v < dsp::numVoices; ++v)
        if (kit.steps[(size_t) v].isNotEmpty())
            pattern.setVoiceFromString (v, kit.steps[(size_t) v]);
    pattern.seed = kit.seed;
}
} // namespace slagg
