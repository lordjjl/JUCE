/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

// Projucer expects JUCE 8.0.10 to provide a juce_audio_processors_lv2_libs.cpp
// translation unit which pulls in LV2 support libraries when LV2 hosting is
// enabled. The trimmed Tracktion vendor tree that Batchy uses deliberately
// omits those sources, so regeneration created include_*.cpp wrappers that
// could not find the backing TU. Supplying this placeholder keeps builds
// working and documents the limitation explicitly.

#if JUCE_PLUGINHOST_LV2 && ! (JUCE_ANDROID || JUCE_IOS)
 #error "LV2 support libraries are not included in Batchy's JUCE fork. Disable JUCE_PLUGINHOST_LV2 or add the missing LV2 support sources before enabling it."
#endif

