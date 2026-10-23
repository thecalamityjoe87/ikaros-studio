#include "engine/SessionViews.h"

namespace ikaros
{

// The store file looks like this, the session used last first:
//
//   <SESSIONVIEWS>
//     <SESSION path="/home/me/Music/My Song.iksession">
//       <VIEW saved="1792000000000" libraryVisible="1" ... />
//     </SESSION>
//     ...
namespace
{
    const juce::Identifier storeType { "SESSIONVIEWS" }, sessionType { "SESSION" }, pathProperty { "path" };

    juce::String keyOf (const juce::File& session)
    {
        return session.getFullPathName();
    }
}

SessionViews::SessionViews (const juce::File& storeFile)  : file (storeFile)
{
}

juce::ValueTree SessionViews::readFile (const juce::File& f)
{
    // parseXML returns a std::unique_ptr<XmlElement> (nullptr if the file
    // is missing or isn't XML); fromXml turns it into a ValueTree.
    if (auto xml = juce::parseXML (f))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.hasType (viewType) || tree.hasType (storeType))
            return tree;
    }

    return {};
}

bool SessionViews::writeFile (const juce::ValueTree& view, const juce::File& f)
{
    auto xml = view.createXml();
    return xml != nullptr && xml->writeTo (f);
}

void SessionViews::stamp (juce::ValueTree& view)
{
    view.setProperty (savedProperty, juce::Time::currentTimeMillis(), nullptr);
}

juce::ValueTree SessionViews::newerOf (const juce::ValueTree& a, const juce::ValueTree& b)
{
    if (! a.isValid()) return b;
    if (! b.isValid()) return a;

    // var -> int64: the time was stored as a number (it may read back as text).
    const auto timeOf = [] (const juce::ValueTree& v) { return (juce::int64) v[savedProperty]; };
    return timeOf (b) > timeOf (a) ? b : a;
}

juce::ValueTree SessionViews::get (const juce::File& session) const
{
    const auto store = readFile (file);
    const auto entry = store.getChildWithProperty (pathProperty, keyOf (session));

    // A copy, so changing it doesn't change the tree read from the file.
    return entry.getChildWithName (viewType).createCopy();
}

void SessionViews::set (const juce::File& session, juce::ValueTree view)
{
    if (! view.hasType (viewType) || session == juce::File())
        return;

    stamp (view);

    auto store = readFile (file);

    if (! store.hasType (storeType))
        store = juce::ValueTree (storeType);

    // Replace the session's entry, and move it to the top.
    store.removeChild (store.getChildWithProperty (pathProperty, keyOf (session)), nullptr);

    juce::ValueTree entry (sessionType);
    entry.setProperty (pathProperty, keyOf (session), nullptr);
    entry.appendChild (view.createCopy(), nullptr);
    store.addChild (entry, 0, nullptr);

    while (store.getNumChildren() > maxSessions)
        store.removeChild (store.getNumChildren() - 1, nullptr);

    file.getParentDirectory().createDirectory();
    writeFile (store, file);
}

} // namespace ikaros
