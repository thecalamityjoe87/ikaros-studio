#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ikaros::fonts
{

// The bundled typefaces (compiled into the binary via juce_add_binary_data).
// Each function returns a juce::Font at the given pixel height.

juce::Font ui (float height);            // Figtree Regular
juce::Font uiSemiBold (float height);    // Figtree SemiBold
juce::Font uiBold (float height);        // Figtree Bold
juce::Font uiExtraBold (float height);   // Figtree ExtraBold (pedal names)
juce::Font mono (float height);          // JetBrains Mono Medium
juce::Font monoBold (float height);      // JetBrains Mono Bold

/** The default UI typeface, handed to the LookAndFeel so plain JUCE widgets
    (menus, dialogs, text editors) also use Figtree. */
juce::Typeface::Ptr defaultTypeface();

} // namespace ikaros::fonts
