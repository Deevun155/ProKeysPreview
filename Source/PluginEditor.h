/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class ProKeysPreviewAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    ProKeysPreviewAudioProcessorEditor (ProKeysPreviewAudioProcessor&);
    ~ProKeysPreviewAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void timerCallback() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    ProKeysPreviewAudioProcessor& audioProcessor;

    juce::Label difficultyLabel;
    juce::ComboBox difficultyComboBox;
    juce::Label timeOffsetLabel;
    juce::TextEditor timeOffsetEditor;
    juce::Label refreshRateLabel;
    juce::TextEditor refreshRateEditor;
    juce::Label trackSpeedLabel;
    juce::ComboBox trackSpeedComboBox;

    juce::Image whiteGemImg;
    juce::Image blackGemImg;
    juce::Image whiteGlissGemImg;
    juce::Image blackErrorGemImg;
    juce::Image whiteErrorGemImg;
    juce::Image whiteOdGemImg;
    juce::Image blackOdGemImg;
    juce::Image smashImgs[10];
    juce::Image smashFlareImg;

    // Horizontal swipe animation state for range shift transitions
    float        m_transitionInitialOffset = 0.0f;
    juce::uint32 m_transitionStartMs       = 0;
    int          m_prevDisplayPitchMin     = -1;
    int          m_prevDisplayPitchMax     = -1;

    // Playback position smoothing to eliminate micro-jitter at audio buffer boundaries
    double m_smoothedTime  = 0.0;
    double m_lastPaintTime = 0.0;
    bool   m_wasPlaying    = false;

    // Track playing state to trigger MIDI re-fetch on playback start
    bool m_wasPlayingForFetch = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProKeysPreviewAudioProcessorEditor)
};
