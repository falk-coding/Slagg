#pragma once

// Låtform (arrangemang) – GEMENSAM för alla falk elektro-instrument (samma fil i Slagg och Bakelit).
// Ändra den bara i båda repona samtidigt: Slagg och Bakelit måste lägga breaks och drops på exakt samma takter
// när de dras in mellan samma markörer.
//
// Formen räknas från arrangemangets start (markörernas början) och sträcks ut till slutet:
//   intro -> uppbyggnad -> [huvuddel, break] upprepas -> outro (sista takterna före slutet).
// Sista takterna i varje break är "buildup" (spänningen byggs mot dropet). Första takten efter ett break är "drop".
#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>
#include <vector>

namespace falk::arr
{
enum Form { off = 0, classic, longBreak, hypnotic, shortForm };
inline constexpr int numForms = 5;

enum class Part { none = 0, intro, build, main, breakdown, buildup, outro };

struct Lengths { int intro, build, main, brk, outro; };

inline Lengths lengthsFor (int form) noexcept
{
    switch (form)
    {
        case classic:   return { 16, 16, 32, 32, 16 };
        case longBreak: return { 16, 16, 48, 32, 16 };
        case hypnotic:  return { 32, 16, 64, 32, 16 };
        case shortForm: return {  8,  8, 16, 16,  8 };
        default:        return {  0,  0,  0,  0,  0 };
    }
}

struct Section
{
    Part part = Part::none;
    int bar = 0;          // takt i hela arrangemanget (0 = första)
    int barInPart = 0;    // takt i delen
    int partLength = 1;   // delens längd i takter
    bool drop = false;    // första takten i en huvuddel efter ett break
    double progress = 0;  // 0..1 genom delen (inkl. positionen i takten)
};

// Var i låtformen ligger ppq? startPpq/endPpq = arrangemangets gränser (endPpq <= startPpq = inget slut, ingen outro).
inline Section sectionAt (int form, double ppq, double startPpq, double endPpq, double barPpq) noexcept
{
    Section s;
    if (form <= off || form >= numForms || ppq < startPpq - 1.0e-9 || (endPpq > startPpq && ppq >= endPpq))
        return s;

    const auto L = lengthsFor (form);
    barPpq = std::max (0.25, barPpq);
    const double rel = (ppq - startPpq) / barPpq;
    const int bar = (int) std::floor (rel + 1.0e-9);
    const double inBar = rel - bar;
    const int total = endPpq > startPpq ? (int) std::lround ((endPpq - startPpq) / barPpq) : INT_MAX;
    s.bar = bar;

    auto set = [&] (Part p, int first, int len)
    {
        s.part = p;
        s.barInPart = bar - first;
        s.partLength = std::max (1, len);
        s.progress = std::clamp ((s.barInPart + inBar) / (double) s.partLength, 0.0, 1.0);
    };

    const int outroStart = (total != INT_MAX && total >= L.intro + L.build + L.outro) ? total - L.outro : INT_MAX;
    if (bar >= outroStart)        { set (Part::outro, outroStart, L.outro); return s; }
    if (bar < L.intro)            { set (Part::intro, 0, L.intro); return s; }
    if (bar < L.intro + L.build)  { set (Part::build, L.intro, L.build); return s; }

    const int cycle = L.main + L.brk;
    const int c = (bar - L.intro - L.build) / cycle;
    const int first = L.intro + L.build + c * cycle;
    const int inCycle = bar - first;
    if (inCycle < L.main)
    {
        set (Part::main, first, std::min (L.main, outroStart - first));
        s.drop = c > 0 && inCycle == 0;
        return s;
    }
    const int brkFirst = first + L.main;
    const int brkLen = std::min (L.brk, outroStart - brkFirst);
    const int buildupLen = std::min (8, std::max (1, brkLen / 2));
    if (bar - brkFirst >= brkLen - buildupLen)
        set (Part::buildup, brkFirst + brkLen - buildupLen, buildupLen);
    else
        set (Part::breakdown, brkFirst, brkLen - buildupLen);
    return s;
}

inline const char* partName (Part p) noexcept
{
    switch (p)
    {
        case Part::intro:     return "Intro";
        case Part::build:     return "Build";
        case Part::main:      return "Main";
        case Part::breakdown: return "Break";
        case Part::buildup:   return "Buildup";
        case Part::outro:     return "Outro";
        default:              return "";
    }
}

// "Sektioner"-spåret: en kort ton i början av varje del, en tonhöjd per del (C3 = intro … B3 = outro).
// Syns som noter i alla DAW:ar (även Reason, som inte läser textmarkörer) – en karta över låten, inget ljud.
inline int sectionNote (const char* name) noexcept
{
    struct Map { const char* n; int note; };
    static constexpr Map map[] { { "Intro", 48 }, { "Build", 50 }, { "Main", 52 }, { "Break", 53 },
                                 { "Buildup", 55 }, { "Drop", 57 }, { "Outro", 59 } };
    for (const auto& m : map)
    {
        const char* a = m.n;
        const char* b = name;
        while (*a != 0 && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0)
            return m.note;
    }
    return 60;
}

// Markörer (början av varje del + "Drop") för MIDI-filen: {ppq, namn}.
inline std::vector<std::pair<double, const char*>> markers (int form, double startPpq, double endPpq, double barPpq)
{
    std::vector<std::pair<double, const char*>> out;
    if (form <= off || endPpq <= startPpq)
        return out;
    Part last = Part::none;
    for (double t = startPpq; t < endPpq - 1.0e-9; t += barPpq)
    {
        const auto s = sectionAt (form, t, startPpq, endPpq, barPpq);
        if (s.drop)
            out.push_back ({ t, "Drop" });
        else if (s.part != last)
            out.push_back ({ t, partName (s.part) });
        last = s.part;
    }
    return out;
}
} // namespace falk::arr
