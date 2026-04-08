#pragma once

#include <JuceHeader.h>
#include <vector>

namespace reaper { class IReaperHostApplication; }

// Forward declare REAPER types to avoid compilation errors if headers aren't fully linked
struct ReaProject;
struct MediaTrack;
struct MediaItem;
struct MediaItem_Take;

// 1. Define the internal data model
struct GameNote {
    double startTime; // In seconds
    double endTime;   // In seconds
    int pitch;        // MIDI note number
};

struct RangeShift {
    double time;
    int pitchMin;
    int pitchMax;
};

struct GlissandoMarker {
    double startTime;
    double endTime;
};

struct OverdriveMarker {
    double startTime;
    double endTime;
};

struct SoloMarker {
    double startTime;
    double endTime;
};

struct TrillMarker {
    double startTime;
    double endTime;
};

class ProKeysPreviewAudioProcessor : public juce::AudioProcessor
{
public:
    ProKeysPreviewAudioProcessor();
    ~ProKeysPreviewAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // 2. Add the method to fetch the MIDI data
    void fetchMidiTake();
    void invalidateMidiCache();

    // 3. Store the parsed notes
    std::vector<GameNote> parsedNotes;
    std::vector<RangeShift> rangeShifts;
    std::vector<GlissandoMarker> glissandoMarkers;
    std::vector<OverdriveMarker> overdriveMarkers;
    std::vector<SoloMarker> soloMarkers;
    std::vector<TrillMarker> trillMarkers;

    // Store current playback position
    std::atomic<double> currentPlaybackPosition{ 0.0 };
    std::atomic<bool> isPlaying{ false };
    std::atomic<double> playbackRate{ 1.0 };
    std::atomic<double> positionUpdateTime{ 0.0 }; // high-res clock when position was last set
    std::atomic<unsigned> playbackSeq{ 0 }; // seqlock so the UI reads a consistent snapshot

    // User-adjustable time offset in seconds
    std::atomic<double> timeOffsetSeconds{ 0.0 };
    std::atomic<int> refreshRateHz{ 240 };
    std::atomic<int> difficultySelection{ 0 }; // 0=This track, 1=Expert, 2=Hard, 3=Medium, 4=Easy
    std::atomic<int> trackSpeedPercent{ 100 }; // 50-200%; scales visible time window inversely

    // Error message for display
    juce::String errorMessage;

    // Load and save settings
    void loadTimeOffset();
    void saveTimeOffset(double offsetSeconds);
    void loadRefreshRate();
    void saveRefreshRate(int rateHz);
    void loadTrackSpeed();
    void saveTrackSpeed(int speedPercent);

    // 4. Expose JUCE's VST3 extension to talk to REAPER
    juce::VST3ClientExtensions* getVST3ClientExtensions() override;

    // REAPER API Function Pointers
    MediaTrack* (*GetTrack)(ReaProject*, int) = nullptr;
    MediaItem* (*GetTrackMediaItem)(MediaTrack*, int) = nullptr;
    MediaItem_Take* (*GetActiveTake)(MediaItem*) = nullptr;
    int (*MIDI_CountEvts)(MediaItem_Take*, int*, int*, int*) = nullptr;
    bool (*MIDI_GetNote)(MediaItem_Take*, int, bool*, bool*, double*, double*, int*, int*, int*) = nullptr;
    double (*MIDI_GetProjTimeFromPPQPos)(MediaItem_Take*, double) = nullptr;
    int (*GetTrackNumMediaItems)(MediaTrack*) = nullptr;
    int (*CountTracks)(ReaProject*) = nullptr;
    void* (*GetSetMediaTrackInfo)(MediaTrack*, const char*, void*) = nullptr;
    double (*TimeMap2_timeToBeats)(ReaProject*, double, int*, int*, double*, int*) = nullptr;
    double (*TimeMap2_beatsToTime)(ReaProject*, double, const int*) = nullptr;
    double (*Master_GetPlayRate)(ReaProject*) = nullptr;
    bool (*MIDI_GetHash)(MediaItem_Take*, bool, char*, int) = nullptr;

    // Pointer to the host interface so we can query the track this plug-in instance is on
    reaper::IReaperHostApplication* reaperHost = nullptr;

private:
    // Cached MIDI hash to skip re-parsing when data hasn't changed
    juce::String cachedMidiHash;
    int cachedDifficulty = -1;

    // Inner class to handle VST3 specific host communication
    class VST3Extensions final : public juce::VST3ClientExtensions
    {
    public:
        explicit VST3Extensions(ProKeysPreviewAudioProcessor& p) : processor(p) {}
        void setIHostApplication(Steinberg::FUnknown* ptr) override;
    private:
        ProKeysPreviewAudioProcessor& processor;
    };

    VST3Extensions vst3Extensions{ *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProKeysPreviewAudioProcessor)
};