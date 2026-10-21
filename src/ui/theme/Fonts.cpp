#include "ui/theme/Fonts.h"
#include "BinaryData.h"

namespace ikaros::fonts
{

namespace
{
    // Loads each typeface once, the first time it's asked for.
    // A function-local `static` is initialised exactly once (thread-safe since C++11).
    struct Typefaces
    {
        juce::Typeface::Ptr regular  = load (BinaryData::FigtreeRegular_ttf,       BinaryData::FigtreeRegular_ttfSize);
        juce::Typeface::Ptr semiBold = load (BinaryData::FigtreeSemiBold_ttf,      BinaryData::FigtreeSemiBold_ttfSize);
        juce::Typeface::Ptr bold     = load (BinaryData::FigtreeBold_ttf,          BinaryData::FigtreeBold_ttfSize);
        juce::Typeface::Ptr extraBold = load (BinaryData::FigtreeExtraBold_ttf,    BinaryData::FigtreeExtraBold_ttfSize);
        juce::Typeface::Ptr mono     = load (BinaryData::JetBrainsMonoMedium_ttf,  BinaryData::JetBrainsMonoMedium_ttfSize);
        juce::Typeface::Ptr monoBold = load (BinaryData::JetBrainsMonoBold_ttf,    BinaryData::JetBrainsMonoBold_ttfSize);

        static juce::Typeface::Ptr load (const void* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }
    };

    Typefaces& get()
    {
        static Typefaces t;
        return t;
    }

    juce::Font make (const juce::Typeface::Ptr& tf, float height)
    {
        // FontOptions is JUCE 8's way to build a Font from a specific typeface.
        return juce::Font (juce::FontOptions (tf).withHeight (height));
    }
}

juce::Font ui (float h)          { return make (get().regular, h); }
juce::Font uiSemiBold (float h)  { return make (get().semiBold, h); }
juce::Font uiBold (float h)      { return make (get().bold, h); }
juce::Font uiExtraBold (float h) { return make (get().extraBold, h); }
juce::Font mono (float h)        { return make (get().mono, h); }
juce::Font monoBold (float h)    { return make (get().monoBold, h); }

juce::Typeface::Ptr defaultTypeface()   { return get().regular; }

} // namespace ikaros::fonts
