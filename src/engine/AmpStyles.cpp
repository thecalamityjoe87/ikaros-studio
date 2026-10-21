#include "engine/AmpStyles.h"

namespace ikaros
{

namespace
{
    // Indexes into getAmpCabs(), for the voices' rows below.
    enum Cab
    {
        cab1x8SmallCombo, cab1x10SmallCombo, cab1x12OpenBack, cab1x12Studio, cab1x12Warm, cab1x12Crisp,
        cab1x15, cab2x10OpenBack, cab2x12ClosedBack, cab2x12OpenBack, cab2x12Blue, cab2x12Bright,
        cab4x10ClosedBack, cab4x10OpenBack, cab4x12ClosedBack, cab4x12Vintage, cab4x12Heavy, cab4x12Modern
    };

    juce::Colour rgb (juce::uint32 hex)    { return juce::Colour (0xff000000 | hex); }
}

const std::vector<AmpVoiceStyle>& getAmpVoices()
{
    using Cap = AmpKnobCap;
    using Grille = AmpGrille;

    // Knob defaults: Gain, Bass, Mid, Treble, Presence, Master.
    static const std::vector<AmpVoiceStyle> voices {
        { "Clean",     { .30f, .55f, .45f, .62f, .50f, .60f }, true,  cab2x12OpenBack,
          rgb (0x1b1c1f), rgb (0x222328), rgb (0xd9dde3), rgb (0x2a2c31), rgb (0xe9ecef), Cap::chrome, rgb (0x565a62), Grille::plain },
        { "Crunch",    { .62f, .50f, .58f, .60f, .45f, .55f }, false, cab4x10OpenBack,
          rgb (0xb99a64), rgb (0xa8895a), rgb (0xefe4c8), rgb (0x4a3426), rgb (0xefe4c8), Cap::black,  rgb (0x5e2a22), Grille::plain },
        { "Chime",     { .50f, .45f, .50f, .65f, .55f, .55f }, true,  cab2x12Blue,
          rgb (0x1a1b1e), rgb (0x24252a), rgb (0xc99a6b), rgb (0xb8754a), rgb (0x1c120a), Cap::cream,  rgb (0x4d3527), Grille::diamond },
        { "British",   { .70f, .50f, .60f, .62f, .55f, .50f }, false, cab4x12ClosedBack,
          rgb (0x141518), rgb (0x1c1d21), rgb (0xd8b25a), rgb (0xc9a54e), rgb (0x1a160d), Cap::black,  rgb (0x33312d), Grille::basketweave },
        { "High Gain", { .82f, .60f, .40f, .60f, .60f, .45f }, false, cab4x12Modern,
          rgb (0x101114), rgb (0x17181c), rgb (0x9aa3b0), rgb (0x3a3f48), rgb (0xeef1f5), Cap::black,  rgb (0x121316), Grille::plain },
        { "Bass",      { .40f, .62f, .50f, .48f, .40f, .60f }, false, cab1x15,
          rgb (0x39414d), rgb (0x323944), rgb (0xc9ced6), rgb (0xc9ced6), rgb (0x15171b), Cap::black,  rgb (0x3a3f48), Grille::perforated },
    };

    return voices;
}

const std::vector<AmpCab>& getAmpCabs()
{
    // Grouped by size. Which Guitarix IR each one plays is in AmpPlugin.cpp
    // (in the same order). juce::String::fromUTF8: the names have a real
    // multiplication sign.
    static const std::vector<AmpCab> cabs = []
    {
        auto cab = [] (const char* utf8, int speakers)    { return AmpCab { juce::String::fromUTF8 (utf8), speakers }; };

        return std::vector<AmpCab> {
            cab ("1\xc3\x97" "8 Small Combo", 1),
            cab ("1\xc3\x97" "10 Small Combo", 1),
            cab ("1\xc3\x97" "12 Open Back", 1),
            cab ("1\xc3\x97" "12 Studio", 1),
            cab ("1\xc3\x97" "12 Warm", 1),
            cab ("1\xc3\x97" "12 Crisp", 1),
            cab ("1\xc3\x97" "15", 1),
            cab ("2\xc3\x97" "10 Open Back", 2),
            cab ("2\xc3\x97" "12 Closed Back", 2),
            cab ("2\xc3\x97" "12 Open Back", 2),
            cab ("2\xc3\x97" "12 Blue", 2),
            cab ("2\xc3\x97" "12 Bright", 2),
            cab ("4\xc3\x97" "10 Closed Back", 4),
            cab ("4\xc3\x97" "10 Open Back", 4),
            cab ("4\xc3\x97" "12 Closed Back", 4),
            cab ("4\xc3\x97" "12 Vintage", 4),
            cab ("4\xc3\x97" "12 Heavy", 4),
            cab ("4\xc3\x97" "12 Modern", 4),
        };
    }();

    return cabs;
}

juce::String getAmpCabName (int cab)
{
    auto& cabs = getAmpCabs();
    return juce::isPositiveAndBelow (cab, (int) cabs.size()) ? cabs[(size_t) cab].name : juce::String ("Off");
}

int getDefaultAmpVoiceFor (const juce::String& trackIcon)
{
    auto& voices = getAmpVoices();

    if (trackIcon == "bass")
        for (int i = 0; i < (int) voices.size(); ++i)
            if (voices[(size_t) i].name == "Bass")
                return i;

    return 0;   // Clean
}

} // namespace ikaros
