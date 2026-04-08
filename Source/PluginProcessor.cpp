

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

// ==============================================================================
// REAPER & VST3 SDK BOILERPLATE
// ==============================================================================
#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/vst/ivsthostapplication.h>

namespace reaper
{
    using namespace Steinberg;
    using uint32 = Steinberg::uint32;

    // Catch-all: Some REAPER header versions use CStringA instead of const char*
    using CStringA = const char*;

    // Include the REAPER header INSIDE the namespace
#include "extern/reaper_vst3_interfaces.h"

// This macro instantiates the IID (Interface ID) so we can query for it.
// It MUST only appear in one .cpp file to avoid LNK2005 errors.
    DEF_CLASS_IID(IReaperHostApplication)
}
// ==============================================================================

//==============================================================================
void ProKeysPreviewAudioProcessor::VST3Extensions::setIHostApplication(Steinberg::FUnknown* ptr)
{
    if (ptr == nullptr) return;

    void* objPtr = nullptr;
    // Query REAPER's custom host interface
    if (ptr->queryInterface(reaper::IReaperHostApplication::iid, &objPtr) == Steinberg::kResultOk)
    {
        auto* reaperHostApp = static_cast<reaper::IReaperHostApplication*> (objPtr);

        // Fetch all the API pointers we need
        processor.GetTrack = (MediaTrack * (*)(ReaProject*, int)) reaperHostApp->getReaperApi("GetTrack");
        processor.GetTrackMediaItem = (MediaItem * (*)(MediaTrack*, int)) reaperHostApp->getReaperApi("GetTrackMediaItem");
        processor.GetActiveTake = (MediaItem_Take * (*)(MediaItem*)) reaperHostApp->getReaperApi("GetActiveTake");
        processor.MIDI_CountEvts = (int (*)(MediaItem_Take*, int*, int*, int*)) reaperHostApp->getReaperApi("MIDI_CountEvts");
        processor.MIDI_GetNote = (bool (*)(MediaItem_Take*, int, bool*, bool*, double*, double*, int*, int*, int*)) reaperHostApp->getReaperApi("MIDI_GetNote");
        processor.MIDI_GetProjTimeFromPPQPos = (double (*)(MediaItem_Take*, double)) reaperHostApp->getReaperApi("MIDI_GetProjTimeFromPPQPos");
        processor.GetTrackNumMediaItems = (int (*)(MediaTrack*)) reaperHostApp->getReaperApi("GetTrackNumMediaItems");
        processor.CountTracks = (int (*)(ReaProject*)) reaperHostApp->getReaperApi("CountTracks");
        processor.GetSetMediaTrackInfo = (void* (*)(MediaTrack*, const char*, void*)) reaperHostApp->getReaperApi("GetSetMediaTrackInfo");
        processor.TimeMap2_timeToBeats = (double (*)(ReaProject*, double, int*, int*, double*, int*)) reaperHostApp->getReaperApi("TimeMap2_timeToBeats");
        processor.TimeMap2_beatsToTime = (double (*)(ReaProject*, double, const int*)) reaperHostApp->getReaperApi("TimeMap2_beatsToTime");
        processor.Master_GetPlayRate = (double (*)(ReaProject*)) reaperHostApp->getReaperApi("Master_GetPlayRate");
        processor.MIDI_GetHash = (bool (*)(MediaItem_Take*, bool, char*, int)) reaperHostApp->getReaperApi("MIDI_GetHash");

        processor.reaperHost = reaperHostApp;
    }
}

juce::VST3ClientExtensions* ProKeysPreviewAudioProcessor::getVST3ClientExtensions()
{
    return &vst3Extensions;
}

static bool getRangeForMarker(int markerPitch, int& pitchMin, int& pitchMax)
{
    switch (markerPitch) {
        case 0: pitchMin = 48; pitchMax = 64; return true; // C2-E3
        case 2: pitchMin = 50; pitchMax = 65; return true; // D2-F3
        case 4: pitchMin = 52; pitchMax = 67; return true; // E2-G3
        case 5: pitchMin = 53; pitchMax = 69; return true; // F2-A3
        case 7: pitchMin = 55; pitchMax = 71; return true; // G2-B3
        case 9: pitchMin = 57; pitchMax = 72; return true; // A2-C4
        default: return false;
    }
}

void ProKeysPreviewAudioProcessor::invalidateMidiCache()
{
    cachedMidiHash.clear();
    cachedDifficulty = -1;
}

//==============================================================================
void ProKeysPreviewAudioProcessor::fetchMidiTake()
{
    // Ensure API pointers were successfully loaded
    if (!GetTrack || !GetTrackMediaItem || !GetActiveTake || !MIDI_CountEvts || !MIDI_GetNote || !MIDI_GetProjTimeFromPPQPos)
        return;

    // --- MIDI hash cache: skip full re-parse if data hasn't changed ---
    int difficulty = difficultySelection.load();
    if (MIDI_GetHash)
    {
        juce::String combinedHash;

        auto appendTakeHash = [&](MediaItem_Take* take)
        {
            if (take == nullptr) return;
            char buf[256] = {};
            MIDI_GetHash(take, false, buf, sizeof(buf));
            combinedHash += buf;
        };

        auto appendTrackHash = [&](MediaTrack* track)
        {
            if (track == nullptr) return;
            const int numItems = GetTrackNumMediaItems ? GetTrackNumMediaItems(track) : 1;
            for (int i = 0; i < numItems; ++i)
            {
                MediaItem* item = GetTrackMediaItem(track, i);
                if (item) appendTakeHash(GetActiveTake(item));
            }
        };

        if (difficulty == 0)
        {
            if (reaperHost != nullptr)
            {
                if (auto* take = static_cast<MediaItem_Take*>(reaperHost->getReaperParent(2)))
                    appendTakeHash(take);
                else
                {
                    MediaTrack* track = static_cast<MediaTrack*>(reaperHost->getReaperParent(1));
                    if (track == nullptr) track = GetTrack(nullptr, 0);
                    appendTrackHash(track);
                }
            }
        }
        else if (CountTracks != nullptr && GetSetMediaTrackInfo != nullptr)
        {
            const char* targetName = nullptr;
            switch (difficulty)
            {
                case 1: targetName = "PART REAL_KEYS_X"; break;
                case 2: targetName = "PART REAL_KEYS_H"; break;
                case 3: targetName = "PART REAL_KEYS_M"; break;
                case 4: targetName = "PART REAL_KEYS_E"; break;
            }
            if (targetName)
            {
                int numTracks = CountTracks(nullptr);
                for (int i = 0; i < numTracks; ++i)
                {
                    MediaTrack* track = GetTrack(nullptr, i);
                    if (track == nullptr) continue;
                    char* trackName = static_cast<char*>(GetSetMediaTrackInfo(track, "P_NAME", nullptr));
                    if (trackName && juce::String(trackName).contains(targetName))
                    { appendTrackHash(track); break; }
                }
                if (difficulty >= 2)
                {
                    for (int i = 0; i < numTracks; ++i)
                    {
                        MediaTrack* track = GetTrack(nullptr, i);
                        if (track == nullptr) continue;
                        char* n = static_cast<char*>(GetSetMediaTrackInfo(track, "P_NAME", nullptr));
                        if (n && juce::String(n).contains("PART REAL_KEYS_X"))
                        { appendTrackHash(track); break; }
                    }
                }
            }
        }

        if (combinedHash.isNotEmpty() && combinedHash == cachedMidiHash && difficulty == cachedDifficulty)
            return; // MIDI data unchanged — keep existing parsed data

        cachedMidiHash = combinedHash;
        cachedDifficulty = difficulty;
    }

    parsedNotes.clear();
    rangeShifts.clear();
    glissandoMarkers.clear();
    overdriveMarkers.clear();
    soloMarkers.clear();
    trillMarkers.clear();
    errorMessage = ""; // Clear any previous error

    auto parseTake = [&](MediaItem_Take* take)
    {
        if (take == nullptr)
            return;

        int noteCount = 0;
        MIDI_CountEvts(take, &noteCount, nullptr, nullptr);

        for (int i = 0; i < noteCount; ++i)
        {
            double startPPQ, endPPQ;
            int pitch;

            if (MIDI_GetNote(take, i, nullptr, nullptr, &startPPQ, &endPPQ, nullptr, &pitch, nullptr))
            {
                double startSec = MIDI_GetProjTimeFromPPQPos(take, startPPQ);
                double endSec = MIDI_GetProjTimeFromPPQPos(take, endPPQ);

                int rangeMin, rangeMax;
                if (getRangeForMarker(pitch, rangeMin, rangeMax))
                    rangeShifts.push_back({ startSec, rangeMin, rangeMax });
                else if (pitch == 126)
                    glissandoMarkers.push_back({ startSec, endSec });
                else if (pitch == 116 || pitch == 166)
                    overdriveMarkers.push_back({ startSec, endSec });
                else if (pitch == 115)
                    soloMarkers.push_back({ startSec, endSec });
                else if (pitch == 127)
                    trillMarkers.push_back({ startSec, endSec });
                else
                    parsedNotes.push_back({ startSec, endSec, pitch });
            }
        }
    };

    auto parseTrack = [&](MediaTrack* track)
    {
        if (track == nullptr) return;

        const int numItems = GetTrackNumMediaItems ? GetTrackNumMediaItems(track) : 1;
        for (int itemIndex = 0; itemIndex < numItems; ++itemIndex)
        {
            MediaItem* item = GetTrackMediaItem(track, itemIndex);
            if (item == nullptr)
                continue;

            MediaItem_Take* take = GetActiveTake(item);
            parseTake(take);
        }
    };

    // Parse only overdrive and solo markers from a take (used to pull these from Expert)
    auto parseOdSoloFromTake = [&](MediaItem_Take* take)
    {
        if (take == nullptr) return;
        int noteCount = 0;
        MIDI_CountEvts(take, &noteCount, nullptr, nullptr);
        for (int i = 0; i < noteCount; ++i)
        {
            double startPPQ, endPPQ;
            int pitch;
            if (MIDI_GetNote(take, i, nullptr, nullptr, &startPPQ, &endPPQ, nullptr, &pitch, nullptr))
            {
                double startSec = MIDI_GetProjTimeFromPPQPos(take, startPPQ);
                double endSec = MIDI_GetProjTimeFromPPQPos(take, endPPQ);
                if (pitch == 116 || pitch == 166)
                    overdriveMarkers.push_back({ startSec, endSec });
                else if (pitch == 115)
                    soloMarkers.push_back({ startSec, endSec });
            }
        }
    };

    auto parseOdSoloFromTrack = [&](MediaTrack* track)
    {
        if (track == nullptr) return;
        const int numItems = GetTrackNumMediaItems ? GetTrackNumMediaItems(track) : 1;
        for (int itemIndex = 0; itemIndex < numItems; ++itemIndex)
        {
            MediaItem* item = GetTrackMediaItem(track, itemIndex);
            if (item == nullptr) continue;
            MediaItem_Take* take = GetActiveTake(item);
            parseOdSoloFromTake(take);
        }
    };

    // Difficulty 0 = "This track" - use current track
    if (difficulty == 0)
    {
        // Prefer the take this plug-in instance belongs to
        if (reaperHost != nullptr)
        {
            if (auto* take = static_cast<MediaItem_Take*>(reaperHost->getReaperParent(2)))
            {
                parseTake(take);
                std::sort(rangeShifts.begin(), rangeShifts.end(), [](const RangeShift& a, const RangeShift& b) { return a.time < b.time; });
                std::sort(glissandoMarkers.begin(), glissandoMarkers.end(), [](const GlissandoMarker& a, const GlissandoMarker& b) { return a.startTime < b.startTime; });
                std::sort(overdriveMarkers.begin(), overdriveMarkers.end(), [](const OverdriveMarker& a, const OverdriveMarker& b) { return a.startTime < b.startTime; });
                std::sort(soloMarkers.begin(), soloMarkers.end(), [](const SoloMarker& a, const SoloMarker& b) { return a.startTime < b.startTime; });
                std::sort(trillMarkers.begin(), trillMarkers.end(), [](const TrillMarker& a, const TrillMarker& b) { return a.startTime < b.startTime; });
                std::sort(parsedNotes.begin(), parsedNotes.end(), [](const GameNote& a, const GameNote& b) { return a.startTime < b.startTime; });
                return;
            }
        }

        // Fallback: use the track this plug-in is on
        MediaTrack* track = nullptr;
        if (reaperHost != nullptr)
            track = static_cast<MediaTrack*>(reaperHost->getReaperParent(1));

        if (track == nullptr)
            track = GetTrack(nullptr, 0);

        parseTrack(track);
    }
    else
    {
        // Search for track by name based on difficulty
        const char* targetName = nullptr;
        switch (difficulty)
        {
            case 1: targetName = "PART REAL_KEYS_X"; break; // Expert
            case 2: targetName = "PART REAL_KEYS_H"; break; // Hard
            case 3: targetName = "PART REAL_KEYS_M"; break; // Medium
            case 4: targetName = "PART REAL_KEYS_E"; break; // Easy
        }

        if (targetName != nullptr && CountTracks != nullptr && GetSetMediaTrackInfo != nullptr)
        {
            int numTracks = CountTracks(nullptr);
            bool foundTrack = false;

            for (int i = 0; i < numTracks; ++i)
            {
                MediaTrack* track = GetTrack(nullptr, i);
                if (track == nullptr) continue;

                // Get track name
                char* trackName = static_cast<char*>(GetSetMediaTrackInfo(track, "P_NAME", nullptr));
                if (trackName != nullptr)
                {
                    juce::String name(trackName);
                    if (name.contains(targetName))
                    {
                        parseTrack(track);
                        foundTrack = true;

                        // For lower difficulties, fetch overdrive and solo markers from expert track
                        if (difficulty >= 2)
                        {
                            overdriveMarkers.clear();
                            soloMarkers.clear();
                            for (int ei = 0; ei < numTracks; ++ei)
                            {
                                MediaTrack* expertTrack = GetTrack(nullptr, ei);
                                if (expertTrack == nullptr) continue;
                                char* eName = static_cast<char*>(GetSetMediaTrackInfo(expertTrack, "P_NAME", nullptr));
                                if (eName != nullptr && juce::String(eName).contains("PART REAL_KEYS_X"))
                                {
                                    parseOdSoloFromTrack(expertTrack);
                                    break;
                                }
                            }
                        }

                        std::sort(rangeShifts.begin(), rangeShifts.end(), [](const RangeShift& a, const RangeShift& b) { return a.time < b.time; });
                        std::sort(glissandoMarkers.begin(), glissandoMarkers.end(), [](const GlissandoMarker& a, const GlissandoMarker& b) { return a.startTime < b.startTime; });
                        std::sort(overdriveMarkers.begin(), overdriveMarkers.end(), [](const OverdriveMarker& a, const OverdriveMarker& b) { return a.startTime < b.startTime; });
                        std::sort(soloMarkers.begin(), soloMarkers.end(), [](const SoloMarker& a, const SoloMarker& b) { return a.startTime < b.startTime; });
                        std::sort(trillMarkers.begin(), trillMarkers.end(), [](const TrillMarker& a, const TrillMarker& b) { return a.startTime < b.startTime; });
                        std::sort(parsedNotes.begin(), parsedNotes.end(), [](const GameNote& a, const GameNote& b) { return a.startTime < b.startTime; });
                        return;
                    }
                }
            }

            // If we didn't find the track, set error message
            if (!foundTrack)
            {
                            errorMessage = juce::String("Cannot find track: ") + targetName;
                            }
                        }
                    }

                    std::sort(rangeShifts.begin(), rangeShifts.end(), [](const RangeShift& a, const RangeShift& b) { return a.time < b.time; });
                    std::sort(glissandoMarkers.begin(), glissandoMarkers.end(), [](const GlissandoMarker& a, const GlissandoMarker& b) { return a.startTime < b.startTime; });
                    std::sort(overdriveMarkers.begin(), overdriveMarkers.end(), [](const OverdriveMarker& a, const OverdriveMarker& b) { return a.startTime < b.startTime; });
                    std::sort(soloMarkers.begin(), soloMarkers.end(), [](const SoloMarker& a, const SoloMarker& b) { return a.startTime < b.startTime; });
                    std::sort(trillMarkers.begin(), trillMarkers.end(), [](const TrillMarker& a, const TrillMarker& b) { return a.startTime < b.startTime; });
                    std::sort(parsedNotes.begin(), parsedNotes.end(), [](const GameNote& a, const GameNote& b) { return a.startTime < b.startTime; });
                }

//==============================================================================
ProKeysPreviewAudioProcessor::ProKeysPreviewAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    loadTimeOffset();
    loadRefreshRate();
    loadTrackSpeed();
}

ProKeysPreviewAudioProcessor::~ProKeysPreviewAudioProcessor()
{
}

//==============================================================================
const juce::String ProKeysPreviewAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ProKeysPreviewAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ProKeysPreviewAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ProKeysPreviewAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ProKeysPreviewAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ProKeysPreviewAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int ProKeysPreviewAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ProKeysPreviewAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String ProKeysPreviewAudioProcessor::getProgramName (int index)
{
    return {};
}

void ProKeysPreviewAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void ProKeysPreviewAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fetchMidiTake();
}

void ProKeysPreviewAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ProKeysPreviewAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void ProKeysPreviewAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Update playback position on audio thread
    if (auto* playHead = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo posInfo;
        if (playHead->getCurrentPosition(posInfo))
        {
            // Use the timeline position directly - the host already handles latency compensation.
            // Seqlock: odd sequence = write in progress, even = write complete.
            // The UI retries reads that land on an odd sequence or see a change.
            const auto seq = playbackSeq.load(std::memory_order_relaxed);
            playbackSeq.store(seq + 1, std::memory_order_release);

            currentPlaybackPosition.store(posInfo.timeInSeconds, std::memory_order_relaxed);
            isPlaying.store(posInfo.isPlaying, std::memory_order_relaxed);
            positionUpdateTime.store(juce::Time::getMillisecondCounterHiRes() * 0.001, std::memory_order_relaxed);

            // Store the current project playback rate so the editor can scale the time offset
            if (Master_GetPlayRate)
                playbackRate.store(Master_GetPlayRate(nullptr), std::memory_order_relaxed);
            else
                playbackRate.store(1.0, std::memory_order_relaxed);

            playbackSeq.store(seq + 2, std::memory_order_release);
        }
    }

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);

        // ..do something to the data...
    }
}

//==============================================================================
bool ProKeysPreviewAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* ProKeysPreviewAudioProcessor::createEditor()
{
    return new ProKeysPreviewAudioProcessorEditor (*this);
}

//==============================================================================
void ProKeysPreviewAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void ProKeysPreviewAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
void ProKeysPreviewAudioProcessor::loadTimeOffset()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "ProKeysPreview";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("ProKeysPreview").getFullPathName();

    juce::PropertiesFile props(options);
    if (props.containsKey("timeOffsetSeconds"))
    {
        double offset = props.getDoubleValue("timeOffsetSeconds", 0.0);
        timeOffsetSeconds.store(offset);
    }
}

void ProKeysPreviewAudioProcessor::saveTimeOffset(double offsetSeconds)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "ProKeysPreview";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("ProKeysPreview").getFullPathName();

    juce::PropertiesFile props(options);
    props.setValue("timeOffsetSeconds", offsetSeconds);
    props.save();
}

void ProKeysPreviewAudioProcessor::loadRefreshRate()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "ProKeysPreview";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("ProKeysPreview").getFullPathName();

    juce::PropertiesFile props(options);
    if (props.containsKey("refreshRateHz"))
    {
        int rate = props.getIntValue("refreshRateHz", 240);
        refreshRateHz.store(rate);
    }
}

void ProKeysPreviewAudioProcessor::saveRefreshRate(int rateHz)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "ProKeysPreview";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("ProKeysPreview").getFullPathName();

    juce::PropertiesFile props(options);
    props.setValue("refreshRateHz", rateHz);
    props.save();
}

void ProKeysPreviewAudioProcessor::loadTrackSpeed()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "ProKeysPreview";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("ProKeysPreview").getFullPathName();

    juce::PropertiesFile props(options);
    if (props.containsKey("trackSpeedPercent"))
    {
        int speed = props.getIntValue("trackSpeedPercent", 100);
        trackSpeedPercent.store(speed);
    }
}

void ProKeysPreviewAudioProcessor::saveTrackSpeed(int speedPercent)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "ProKeysPreview";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("ProKeysPreview").getFullPathName();

    juce::PropertiesFile props(options);
    props.setValue("trackSpeedPercent", speedPercent);
    props.save();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ProKeysPreviewAudioProcessor();
}
