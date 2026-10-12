#include "engine/SoundShaper.h"

namespace te = tracktion;

namespace ikaros
{

const char* SoundShaperPlugin::xmlTypeName = "ikarosSoundShaper";
const juce::Identifier SoundShaperPlugin::toneID ("tone");
const juce::Identifier SoundShaperPlugin::driveID ("drive");
const juce::Identifier SoundShaperPlugin::widthID ("width");
const juce::Identifier SoundShaperPlugin::roomID ("room");

namespace
{
    // Below this the knob counts as being at its neutral position (so a knob
    // dragged back to the middle really switches the effect off).
    constexpr float neutralTolerance = 0.005f;

    juce::String percent (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
}

//==============================================================================
SoundShaperPlugin::SoundShaperPlugin (te::PluginCreationInfo info) : te::Plugin (info)
{
    auto* um = getUndoManager();

    // referTo links each CachedValue to a property on our state, with the
    // value to use while the property isn't there.
    toneValue.referTo (state, toneID, um, neutralValueOf ("tone"));
    driveValue.referTo (state, driveID, um, neutralValueOf ("drive"));
    widthValue.referTo (state, widthID, um, neutralValueOf ("width"));
    roomValue.referTo (state, roomID, um, neutralValueOf ("room"));

    // All four run 0..1. The lambdas turn a value into text for Tracktion's
    // own displays and back again.
    auto toText = [] (float v) { return percent (v); };
    auto fromText = [] (const juce::String& s) { return s.getFloatValue() / 100.0f; };

    tone  = addParam (toneID.toString(),  "Tone",  { 0.0f, 1.0f }, toText, fromText);
    drive = addParam (driveID.toString(), "Drive", { 0.0f, 1.0f }, toText, fromText);
    width = addParam (widthID.toString(), "Width", { 0.0f, 1.0f }, toText, fromText);
    room  = addParam (roomID.toString(),  "Room",  { 0.0f, 1.0f }, toText, fromText);

    tone->attachToCurrentValue (toneValue);
    drive->attachToCurrentValue (driveValue);
    width->attachToCurrentValue (widthValue);
    room->attachToCurrentValue (roomValue);
}

SoundShaperPlugin::~SoundShaperPlugin()
{
    notifyListenersOfDeletion();

    for (auto* p : { tone.get(), drive.get(), width.get(), room.get() })
        p->detachFromCurrentValue();
}

te::AutomatableParameter* SoundShaperPlugin::getParameterFor (const juce::String& control) const
{
    if (control == toneID.toString())   return tone.get();
    if (control == driveID.toString())  return drive.get();
    if (control == widthID.toString())  return width.get();
    if (control == roomID.toString())   return room.get();
    return nullptr;
}

float SoundShaperPlugin::neutralValueOf (const juce::String& control)
{
    return control == "drive" || control == "room" ? 0.0f : 0.5f;
}

void SoundShaperPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    te::copyPropertiesToCachedValues (v, toneValue, driveValue, widthValue, roomValue);

    for (auto* p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

//==============================================================================
void SoundShaperPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    rate = info.sampleRate;
    reverb.setSampleRate (info.sampleRate);    // allocates, so it's done here and not while playing
    driveGain.reset (info.sampleRate, 0.05);   // drive changes glide over 50 ms
    reset();
}

void SoundShaperPlugin::reset()
{
    for (auto& f : lowPass)     f.reset();
    for (auto& f : highShelf)   f.reset();
    reverb.reset();
    filterTone = reverbRoom = -1.0f;   // set everything up again on the next block
}

void SoundShaperPlugin::updateFilters (float t)
{
    if (t == filterTone)
        return;

    filterTone = t;

    // Darker: the low-pass comes down from 20 kHz to 400 Hz, evenly in
    // octaves. Brighter: up to 10 dB more above 2.5 kHz.
    const double cutoff = 20000.0 * std::pow (400.0 / 20000.0, (double) juce::jmax (0.0f, (0.5f - t) * 2.0f));
    const auto boost = juce::Decibels::decibelsToGain (juce::jmax (0.0f, (t - 0.5f) * 2.0f) * 10.0f);

    for (auto& f : lowPass)
        f.setCoefficients (juce::IIRCoefficients::makeLowPass (rate, juce::jmin (cutoff, rate * 0.45)));

    for (auto& f : highShelf)
        f.setCoefficients (juce::IIRCoefficients::makeHighShelf (rate, 2500.0, 0.7, boost));
}

void SoundShaperPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    SCOPED_REALTIME_CHECK

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample, num = fc.bufferNumSamples;
    const int channels = juce::jmin (2, buffer.getNumChannels());

    // Tone
    const float t = tone->getCurrentValue();

    if (std::abs (t - 0.5f) > neutralTolerance)
    {
        updateFilters (t);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& filter = t < 0.5f ? lowPass[(size_t) ch] : highShelf[(size_t) ch];
            filter.processSamples (buffer.getWritePointer (ch, start), num);
        }
    }

    // Drive: tanh rounds off the peaks. The gain into it goes up to +24 dB,
    // and the output comes down by half that, so the overall level stays
    // about the same.
    const float d = drive->getCurrentValue();
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (d * 24.0f));

    if (d > neutralTolerance || driveGain.isSmoothing())
    {
        for (int i = 0; i < num; ++i)
        {
            const float g = driveGain.getNextValue();
            const float makeUp = 1.0f / std::sqrt (g);

            for (int ch = 0; ch < channels; ++ch)
            {
                auto* sample = buffer.getWritePointer (ch, start + i);
                *sample = std::tanh (*sample * g) * makeUp;
            }
        }
    }

    // Width: scale the difference between left and right (the "side"),
    // keeping what they have in common (the "mid").
    const float w = width->getCurrentValue();

    if (channels == 2 && std::abs (w - 0.5f) > neutralTolerance)
    {
        auto* left = buffer.getWritePointer (0, start);
        auto* right = buffer.getWritePointer (1, start);
        const float sideGain = w * 2.0f;

        for (int i = 0; i < num; ++i)
        {
            const float mid = (left[i] + right[i]) * 0.5f;
            const float side = (left[i] - right[i]) * 0.5f * sideGain;
            left[i] = mid + side;
            right[i] = mid - side;
        }
    }

    // Room: JUCE's reverb (the same one Tracktion's Reverb effect uses).
    // Its dry level 0.5 means "unchanged"; wet and size grow with the knob.
    const float r = room->getCurrentValue();

    if (r > neutralTolerance)
    {
        if (r != reverbRoom)
        {
            reverbRoom = r;
            juce::Reverb::Parameters p;
            p.roomSize = 0.3f + 0.6f * r;
            p.damping = 0.45f;
            p.wetLevel = 0.25f * r;
            p.dryLevel = 0.5f;
            p.width = 1.0f;
            reverb.setParameters (p);
        }

        if (channels == 2)
            reverb.processStereo (buffer.getWritePointer (0, start), buffer.getWritePointer (1, start), num);
        else if (channels == 1)
            reverb.processMono (buffer.getWritePointer (0, start), num);
    }
    else if (reverbRoom > 0.0f)
    {
        // Turned all the way down: forget the old tail, so turning it up
        // again doesn't bring back a burst of old sound.
        reverb.reset();
        reverbRoom = -1.0f;
    }
}

//==============================================================================
const char* TransposePlugin::xmlTypeName = "ikarosTranspose";
const juce::Identifier TransposePlugin::semitonesID ("semitones");

TransposePlugin::TransposePlugin (te::PluginCreationInfo info) : te::Plugin (info)
{
    semitonesValue.referTo (state, semitonesID, getUndoManager(), 0.0f);

    semitones = addParam (semitonesID.toString(), "Semitones", { -maxSemitones, maxSemitones },
                          [] (float v)                 { return juce::String (juce::roundToInt (v)); },
                          [] (const juce::String& s)   { return s.getFloatValue(); });
    semitones->attachToCurrentValue (semitonesValue);
}

TransposePlugin::~TransposePlugin()
{
    notifyListenersOfDeletion();
    semitones->detachFromCurrentValue();
}

void TransposePlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    te::copyPropertiesToCachedValues (v, semitonesValue);
    semitones->updateFromAttachedValue();
}

void TransposePlugin::initialise (const te::PluginInitialisationInfo&)
{
    for (auto& channel : heldShift)
        channel.fill (0);
}

void TransposePlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    auto* midi = fc.bufferForMidiMessages;

    if (midi == nullptr)
        return;

    const int shift = juce::roundToInt (semitones->getCurrentValue());

    for (auto& m : *midi)
    {
        if (! (m.isNoteOn() || m.isNoteOff() || m.isAftertouch()))
            continue;

        // getChannel() is 1..16.
        auto& held = heldShift[(size_t) juce::jlimit (0, 15, m.getChannel() - 1)][(size_t) m.getNoteNumber()];

        if (m.isNoteOn())
            held = (juce::int8) shift;

        // Note-offs (and aftertouch) move as far as their note-on did.
        m.setNoteNumber (juce::jlimit (0, 127, m.getNoteNumber() + held));
    }
}

} // namespace ikaros
