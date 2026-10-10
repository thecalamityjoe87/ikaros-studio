/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#if TRACKTION_UNIT_TESTS && ENGINE_UNIT_TESTS_CLICKNODE

#include <tracktion_engine/../3rd_party/doctest/tracktion_doctest.hpp>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

namespace tracktion::inline engine
{
    TEST_SUITE ("tracktion_engine")
    {
        TEST_CASE("ClickNode")
        {
            auto& engine = *Engine::getEngines()[0];

            for (auto srbs : std::array<SampleRateAndBlockSize, 4>{{ { 44100.0, 2048 },
                                                                     { 44100.0, 512 },
                                                                     { 44100.0, 64 },
                                                                     { 96000.0, 2048 } }})
            {
                HostedAudioDeviceInterface::Parameters params { .sampleRate = srbs.sampleRate, .blockSize = srbs.blockSize,
                                                                .inputChannels = 0, .outputChannels = 1,
                                                                .inputNames = {}, .outputNames = {} };
                test_utilities::EnginePlayer player (engine, params);

                auto edit = engine::test_utilities::createTestEdit (engine, 1, Edit::EditRole::forEditing);
                auto& tc = edit->getTransport();

                edit->clickTrackEnabled = true;
                edit->setClickTrackVolume (1.0f);
                CHECK_EQ (edit->clickTrackGain.get(), 1.0f);
                tc.play (false);

                const auto totalNumSamples = toSamples (60_td, params.sampleRate);
                auto resultBuffer = player.process (totalNumSamples);

                auto f = graph::test_utilities::writeToTemporaryFile<juce::WavAudioFormat> (toBufferView (resultBuffer), params.sampleRate);

                CHECK_GT (resultBuffer.getRMSLevel (0, 0, resultBuffer.getNumSamples()), 0.01f);
                CHECK_GT (resultBuffer.getMagnitude (0, 0, resultBuffer.getNumSamples()), 0.5f);
            }
        }
    }
} // namespace tracktion::inline engine

#endif