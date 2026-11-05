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

namespace juce
{

String String::fromCFString (CFStringRef cfString)
{
    if (cfString == nullptr)
        return {};

    CFRange range = { 0, CFStringGetLength (cfString) };
    CFIndex bytesNeeded = 0;
    CFStringGetBytes (cfString, range, kCFStringEncodingUTF8, 0, false, nullptr, 0, &bytesNeeded);

    HeapBlock<UInt8> utf8 (bytesNeeded + 1);
    CFStringGetBytes (cfString, range, kCFStringEncodingUTF8, 0, false, utf8, bytesNeeded + 1, nullptr);

    return String (CharPointer_UTF8 ((const CharPointer_UTF8::CharType*) utf8.get()),
                   CharPointer_UTF8 ((const CharPointer_UTF8::CharType*) utf8.get() + bytesNeeded));
}

CFStringRef String::toCFString() const
{
    const char* const utf8 = toRawUTF8();

    if (CFStringRef result = CFStringCreateWithBytes (kCFAllocatorDefault, (const UInt8*) utf8,
                                                      (CFIndex) strlen (utf8), kCFStringEncodingUTF8, false))
        return result;

    // If CFStringCreateWithBytes fails, it probably means there was a UTF8 format
    // error, so we'll return an empty string rather than a null pointer.
    return String().toCFString();
}

bool String::tryConvertToPrecomposedUnicode (String& result) const
{
   #if JUCE_IOS
    JUCE_AUTORELEASEPOOL
    {
        if (isEmpty())
        {
            result = {};
            return true;
        }

        NSString* source = juceStringToNS (*this);
        NSString* composed = source != nil ? [source precomposedStringWithCanonicalMapping] : nil;
        if (composed == nil)
        {
            result = *this;   // conversion failed: keep the input and report it
            return false;
        }
        result = nsStringToJuce (composed);
        return true;
    }
   #else
    UnicodeMapping map;

    map.unicodeEncoding = CreateTextEncoding (kTextEncodingUnicodeDefault,
                                              kUnicodeNoSubset,
                                              kTextEncodingDefaultFormat);

    map.otherEncoding = CreateTextEncoding (kTextEncodingUnicodeDefault,
                                            kUnicodeCanonicalCompVariant,
                                            kTextEncodingDefaultFormat);

    map.mappingVersion = kUnicodeUseLatestMapping;

    UnicodeToTextInfo conversionInfo = {};
    if (isEmpty())
    {
        result = {};
        return true;
    }

    // Never lose a non-empty name on failure; composition may need a larger buffer.
    result = *this;
    bool converted = false;

    if (CreateUnicodeToTextInfo (&map, &conversionInfo) == noErr)
    {
        const size_t bytesNeeded = CharPointer_UTF16::getBytesRequiredFor (getCharPointer());
        const auto utf16 = toUTF16();

        ByteCount outputCapacity = 3 * bytesNeeded + 4;

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            HeapBlock<char> tempOut (outputCapacity);
            ByteCount bytesRead = 0;
            ByteCount outputBufferSize = 0;

            const auto status = ConvertFromUnicodeToText (conversionInfo,
                                                         bytesNeeded, (ConstUniCharArrayPtr) utf16.getAddress(),
                                                         kUnicodeDefaultDirectionMask,
                                                         0, {}, {}, {},
                                                         outputCapacity, &bytesRead,
                                                         &outputBufferSize, tempOut);

            // Accept only a complete conversion of the whole input into whole UTF-16 units.
            if (status == noErr && bytesRead == bytesNeeded && (outputBufferSize % 2) == 0
                && outputBufferSize > 0 && outputBufferSize <= outputCapacity)
            {
                const auto* start = reinterpret_cast<const CharPointer_UTF16::CharType*> (tempOut.get());
                const auto* end = reinterpret_cast<const CharPointer_UTF16::CharType*> (tempOut.get() + outputBufferSize);
                result = String (CharPointer_UTF16 (start), CharPointer_UTF16 (end));
                converted = true;
                break;
            }

            if (status != kTECOutputBufferFullStatus && status != kTECBufferBelowMinimumSizeErr)
                break;

            // The converter keeps state from the failed attempt: reset before retrying,
            // and give up (input kept, false returned) if the reset itself fails.
            if (ResetUnicodeToTextInfo (conversionInfo) != noErr)
                break;
            outputCapacity *= 2;
        }

        DisposeUnicodeToTextInfo (&conversionInfo);
    }

    return converted;
   #endif
}

String String::convertToPrecomposedUnicode() const
{
    String result;
    tryConvertToPrecomposedUnicode (result);
    return result;
}

} // namespace juce
