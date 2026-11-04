// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Kept apart from the JUCE code: Apple's headers clash with "using namespace juce".

#if defined (__APPLE__)
 #include <AudioToolbox/AudioToolbox.h>
#endif

namespace concert7::test
{
bool setAudioUnitOfflineRender (void* audioUnit, bool offline);

/** Sets kAudioUnitProperty_OfflineRender like Logic does for bounces (JUCE's AU host does not). */
bool setAudioUnitOfflineRender (void* audioUnit, bool offline)
{
   #if defined (__APPLE__)
    UInt32 value = offline ? 1 : 0;
    return AudioUnitSetProperty ((AudioUnit) audioUnit, kAudioUnitProperty_OfflineRender, kAudioUnitScope_Global, 0,
                                 &value, sizeof (value)) == noErr;
   #else
    (void) audioUnit; (void) offline;
    return false;
   #endif
}
} // namespace concert7::test
