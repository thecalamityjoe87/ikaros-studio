#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace ikaros
{

//==============================================================================
/**
    How the window looked for each saved session (panels, tabs, zoom,
    playhead...), remembered on this computer. The UI describes its layout
    as a VIEW tree; this class only stores it, keyed by the session's file.

    A session's view is also saved inside its bundle (View.xml, see
    SessionBundle), so it travels to other computers. This copy is the one
    written when a session closes, saved or not, so hiding the sidebar
    never needs a save. Each VIEW carries the time it was written
    (savedProperty), and whichever copy is newer wins (see newerOf).

    Like SessionBundle: files only, no Tracktion.
*/
class SessionViews
{
public:
    /** storeFile: the XML file the views are kept in (in the app's settings folder). */
    explicit SessionViews (const juce::File& storeFile);

    static constexpr const char* viewType = "VIEW";

    /** On a VIEW: when it was written, in milliseconds since 1970. */
    static constexpr const char* savedProperty = "saved";

    /** Only the sessions used most lately are kept. */
    static constexpr int maxSessions = 50;

    /** The view remembered for a session file, or an invalid tree. */
    juce::ValueTree get (const juce::File& session) const;

    /** Remembers a session's view (a VIEW tree) and stamps it with the time. */
    void set (const juce::File& session, juce::ValueTree view);

    /** Reads a view saved in a bundle (View.xml), or an invalid tree. */
    static juce::ValueTree readFile (const juce::File&);

    /** Writes a view as View.xml. Returns false if it couldn't be written. */
    static bool writeFile (const juce::ValueTree& view, const juce::File&);

    /** Stamps a VIEW with the current time (set does this itself). */
    static void stamp (juce::ValueTree& view);

    /** The one written later; either may be invalid. */
    static juce::ValueTree newerOf (const juce::ValueTree& a, const juce::ValueTree& b);

private:
    juce::File file;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionViews)
};

} // namespace ikaros
