#include "ui/dialogs/RecoveryPrompt.h"

namespace ikaros
{

namespace
{
    // "today at 3:42 PM", "yesterday at 9:30 AM", or "on Oct 9 at 3:42 PM".
    juce::String describeTime (juce::Time t)
    {
        const auto now = juce::Time::getCurrentTime();
        auto sameDay = [] (juce::Time a, juce::Time b)
        {
            return a.getYear() == b.getYear() && a.getDayOfYear() == b.getDayOfYear();
        };

        const int hour = t.getHours() % 12 == 0 ? 12 : t.getHours() % 12;
        const auto clock = juce::String (hour) + ":" + juce::String (t.getMinutes()).paddedLeft ('0', 2)
                         + (t.getHours() < 12 ? " AM" : " PM");

        if (sameDay (t, now))
            return "today at " + clock;

        if (sameDay (t, now - juce::RelativeTime::days (1)))
            return "yesterday at " + clock;

        return "on " + t.getMonthName (true) + " " + juce::String (t.getDayOfMonth()) + " at " + clock;
    }

    // SafePointer becomes null if the component is deleted, so a reply
    // arriving after the window has closed does nothing.
    using SafeParent = juce::Component::SafePointer<juce::Component>;
    using Done = std::function<void (bool)>;

    void ask (SafeParent parent, AudioEngine& engine, std::vector<RecoverableSession> remaining, Done done);

    void confirmDiscard (SafeParent parent, AudioEngine& engine, std::vector<RecoverableSession> remaining, Done done)
    {
        const auto session = remaining.front();
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::WarningIcon)
                           .withTitle ("Discard unsaved work?")
                           .withMessage ("The unsaved work in \"" + session.name + "\" will be deleted. This can't be undone.")
                           .withButton ("Discard")
                           .withButton ("Cancel")
                           .withAssociatedComponent (parent);

        // 1 = Discard, 0 = Cancel (or Escape): back to the first question.
        juce::AlertWindow::showAsync (options, [parent, &engine, remaining, done] (int result) mutable
        {
            if (parent == nullptr)
                return;

            if (result == 1)
            {
                engine.discardRecoverableSession (remaining.front());
                remaining.erase (remaining.begin());
            }

            ask (parent, engine, std::move (remaining), std::move (done));
        });
    }

    void ask (SafeParent parent, AudioEngine& engine, std::vector<RecoverableSession> remaining, Done done)
    {
        if (parent == nullptr)
            return;

        if (remaining.empty())
        {
            done (false);
            return;
        }

        const auto session = remaining.front();
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Recover unsaved work?")
                           .withMessage ("Ikaros Studio quit unexpectedly while \"" + session.name + "\" was open. "
                                         "Its work was saved automatically " + describeTime (session.savedAt) + ".\n\n"
                                         "Recover it, or discard the unsaved work?")
                           .withButton ("Recover")
                           .withButton ("Discard")
                           .withAssociatedComponent (parent);

        // 1 = Recover, 0 = Discard (or Escape, so Discard asks again first).
        juce::AlertWindow::showAsync (options, [parent, &engine, remaining, done] (int result) mutable
        {
            if (parent == nullptr)
                return;

            if (result != 1)
            {
                confirmDiscard (parent, engine, std::move (remaining), std::move (done));
                return;
            }

            if (engine.recoverSession (remaining.front()))
            {
                done (true);   // any others wait until the next launch
                return;
            }

            // Couldn't be read: say so, and keep it (maybe a later version can).
            auto failed = juce::MessageBoxOptions()
                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                              .withTitle ("Couldn't recover \"" + remaining.front().name + "\"")
                              .withMessage ("Its autosaved copy couldn't be read.")
                              .withButton ("OK")
                              .withAssociatedComponent (parent);

            remaining.erase (remaining.begin());
            juce::AlertWindow::showAsync (failed, [parent, &engine, remaining, done] (int)
            {
                ask (parent, engine, remaining, done);
            });
        });
    }
}

void RecoveryPrompt::show (juce::Component& parent, AudioEngine& engine, std::function<void (bool recovered)> done)
{
    ask (SafeParent (&parent), engine, engine.findRecoverableSessions(), std::move (done));
}

} // namespace ikaros
