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

XmlDocument::XmlDocument (const String& text)  : originalText (text) {}
XmlDocument::XmlDocument (const File& file)  : inputSource (new FileInputSource (file)) {}

XmlDocument::~XmlDocument() {}

std::unique_ptr<XmlElement> XmlDocument::parse (const File& file)
{
    return XmlDocument (file).getDocumentElement();
}

std::unique_ptr<XmlElement> XmlDocument::parse (const String& textToParse)
{
    return XmlDocument (textToParse).getDocumentElement();
}

std::unique_ptr<XmlElement> parseXML (const String& textToParse)
{
    return XmlDocument (textToParse).getDocumentElement();
}

std::unique_ptr<XmlElement> parseXML (const File& file)
{
    return XmlDocument (file).getDocumentElement();
}

std::unique_ptr<XmlElement> parseXMLIfTagMatches (const String& textToParse, StringRef requiredTag)
{
    return XmlDocument (textToParse).getDocumentElementIfTagMatches (requiredTag);
}

std::unique_ptr<XmlElement> parseXMLIfTagMatches (const File& file, StringRef requiredTag)
{
    return XmlDocument (file).getDocumentElementIfTagMatches (requiredTag);
}

void XmlDocument::setInputSource (InputSource* newSource) noexcept
{
    inputSource.reset (newSource);
}

void XmlDocument::setEmptyTextElementsIgnored (bool shouldBeIgnored) noexcept
{
    ignoreEmptyTextElements = shouldBeIgnored;
}

void XmlDocument::setTrailingContentIgnored (bool shouldBeIgnored) noexcept
{
    ignoreTrailingContent = shouldBeIgnored;
}

namespace XmlIdentifierChars
{
    static bool isIdentifierCharSlow (juce_wchar c) noexcept
    {
        return CharacterFunctions::isLetterOrDigit (c)
                 || c == '_' || c == '-' || c == ':' || c == '.';
    }

    static bool isIdentifierChar (juce_wchar c) noexcept
    {
        static const uint32 legalChars[] = { 0, 0x7ff6000, 0x87fffffe, 0x7fffffe, 0 };

        return ((int) c < (int) numElementsInArray (legalChars) * 32) ? ((legalChars [c >> 5] & (uint32) (1 << (c & 31))) != 0)
                                                                      : isIdentifierCharSlow (c);
    }

    /*static void generateIdentifierCharConstants()
    {
        uint32 n[8] = { 0 };
        for (int i = 0; i < 256; ++i)
            if (isIdentifierCharSlow (i))
                n[i >> 5] |= (1 << (i & 31));

        String s;
        for (int i = 0; i < 8; ++i)
            s << "0x" << String::toHexString ((int) n[i]) << ", ";

        DBG (s);
    }*/

    static String::CharPointerType findEndOfToken (String::CharPointerType p) noexcept
    {
        while (isIdentifierChar (*p))
            ++p;

        return p;
    }
}

// A NUL byte (or, for UTF-16 input, a NUL code unit) followed by anything other than more
// NULs would silently truncate the text the parser sees. Pure trailing NUL padding is fine.
static bool inputHasEmbeddedNull (const MemoryOutputStream& data)
{
    const auto size = data.getDataSize();

    if (size == 0)
        return false;

    const auto* bytes = static_cast<const uint8*> (data.getData());
    const auto isUTF16 = size >= 2 && (CharPointer_UTF16::isByteOrderMarkBigEndian (bytes)
                                        || CharPointer_UTF16::isByteOrderMarkLittleEndian (bytes));
    const size_t unitSize = isUTF16 ? 2 : 1;
    auto i = isUTF16 ? (size_t) 2 : (size_t) 0;

    for (; i + unitSize <= size; i += unitSize)
        if (bytes[i] == 0 && (unitSize == 1 || bytes[i + 1] == 0))
            break;

    if (i + unitSize > size)
        return false;

    for (; i < size; ++i)
        if (bytes[i] != 0)
            return true;

    return false;
}

std::unique_ptr<XmlElement> XmlDocument::getDocumentElement (const bool onlyReadOuterDocumentElement)
{
    probeTruncated = false;

    // Note: the outer-element probe is deliberately not cached, so a tag-matched full parse reads the stream twice.
    if (originalText.isEmpty() && inputSource != nullptr)
    {
        std::unique_ptr<InputStream> in (inputSource->createInputStream());

        if (in != nullptr)
        {
            MemoryOutputStream data;
            data.writeFromInputStream (*in, onlyReadOuterDocumentElement ? 8192 : -1);
            probeTruncated = onlyReadOuterDocumentElement && data.getDataSize() >= 8192;

            if (inputHasEmbeddedNull (data))
            {
                // leave the same fresh state a parse starts from
                outOfData = false;
                limitExceeded = false;
                strictErrorSet = false;
                lastError = "embedded null in input";
                errorOccurred = true;
                return {};
            }

           #if JUCE_STRING_UTF_TYPE == 8
            if (data.getDataSize() > 2)
            {
                data.writeByte (0);
                auto* text = static_cast<const char*> (data.getData());

                if (CharPointer_UTF16::isByteOrderMarkBigEndian (text)
                      || CharPointer_UTF16::isByteOrderMarkLittleEndian (text))
                {
                    auto decoded = data.toString();

                    // A truncated outer-element probe must never be cached as the document text,
                    // or a following full parse would silently parse only the prefix.
                    if (onlyReadOuterDocumentElement)
                        return parseDocumentElement (decoded.getCharPointer(), true);

                    originalText = decoded;
                }
                else
                {
                    if (CharPointer_UTF8::isByteOrderMark (text))
                        text += 3;

                    // parse the input buffer directly to avoid copying it all to a string..
                    return parseDocumentElement (String::CharPointerType (text), onlyReadOuterDocumentElement);
                }
            }
           #else
            auto decoded = data.toString();

            if (onlyReadOuterDocumentElement)
                return parseDocumentElement (decoded.getCharPointer(), true);

            originalText = decoded;
           #endif
        }
    }

    return parseDocumentElement (originalText.getCharPointer(), onlyReadOuterDocumentElement);
}

std::unique_ptr<XmlElement> XmlDocument::getDocumentElementIfTagMatches (StringRef requiredTag)
{
    // The probe result is deliberately not cached (see getDocumentElement): a match costs a second read of the stream.
    auto xml = getDocumentElement (true);

    // When the probe saw only a prefix of a large file and ran out of input inside the root start
    // tag (or could not read it), its answer is unreliable, so decide on the complete document.
    if (probeTruncated && (xml == nullptr || outOfData))
    {
        xml = getDocumentElement (false);

        if (xml != nullptr && xml->hasTagName (requiredTag))
            return xml;

        return {};
    }

    if (xml != nullptr && xml->hasTagName (requiredTag))
        return getDocumentElement (false);

    return {};
}

// Untrusted input must not be able to overflow the stack, so the recursive descent
// through elements is bounded (see XmlDocument::maxElementNestingDepth). Legitimate
// documents nest far below this limit. Entities are plain text and never nest.
// Measured stack use of a parse of a document nested exactly maxElementNestingDepth (128)
// deep, arm64 macOS clang: ~119 KB at -O0, ~52 KB at -O1, ~70 KB at -O3. A 256-deep cap
// was measured at ~222 KB at -O0 and over 400 KB in some optimised builds, which
// overflows a 256 KB thread stack, hence the lower cap.

// Upper bound on the number of characters produced by entity expansion during one
// parse, which stops "billion laughs" style documents from exhausting memory or time.
static constexpr size_t maxTotalEntityExpansion = 8 * 1024 * 1024;

namespace
{
    struct XmlEntityText
    {
        void append (const String& text)
        {
            append (text.getCharPointer(), text.getCharPointer().findTerminatingNull());
        }

        void append (String::CharPointerType start, String::CharPointerType end)
        {
            characters.insert (characters.end(), start.getAddress(), end.getAddress());
        }

        String toString()
        {
            characters.push_back (0);
            return String (String::CharPointerType (characters.data()),
                           String::CharPointerType (characters.data() + characters.size() - 1));
        }

        // Preserve native code units, including non-canonical encodings. Vector growth
        // keeps appends amortized linear, including large attribute values.
        std::vector<String::CharPointerType::CharType> characters;
    };

    struct ScopedDepthIncrement
    {
        explicit ScopedDepthIncrement (int& d) noexcept : depth (d)   { ++depth; }
        ~ScopedDepthIncrement() noexcept                              { --depth; }

        int& depth;

        JUCE_DECLARE_NON_COPYABLE (ScopedDepthIncrement)
    };
}

const String& XmlDocument::getLastParseError() const noexcept
{
    return lastError;
}

void XmlDocument::setLastError (const String& desc, const bool carryOn)
{
    // once a limit has been exceeded, or a malformed-input error has been reported, the
    // parse has failed, so keep that first error
    if (limitExceeded || strictErrorSet)
        return;

    lastError = desc;
    errorOccurred = ! carryOn;
}

void XmlDocument::setFatalError (const String& desc)
{
    setLastError (desc, false);

    if (! limitExceeded)
    {
        strictErrorSet = true;
        outOfData = true;
    }
}

bool XmlDocument::chargeEntityExpansion (size_t numChars)
{
    // checked before the text is allocated, so a hostile document can't grow memory unbounded
    if (numChars > maxTotalEntityExpansion || totalEntityExpansion > maxTotalEntityExpansion - numChars)
    {
        failBecauseLimitExceeded ("entity expansion too large");
        return false;
    }

    totalEntityExpansion += numChars;
    return true;
}

void XmlDocument::failBecauseLimitExceeded (const String& desc)
{
    setLastError (desc, false);
    limitExceeded = true;
    outOfData = true;
}

juce_wchar XmlDocument::readNextChar() noexcept
{
    auto c = input.getAndAdvance();

    if (c == 0)
    {
        outOfData = true;
        --input;
    }

    return c;
}

std::unique_ptr<XmlElement> XmlDocument::parseDocumentElement (String::CharPointerType textToParse,
                                                               bool onlyReadOuterDocumentElement)
{
    input = textToParse;
    lastError.clear();
    internalEntities.clear();
    doctypeHasExternalId = false;
    errorOccurred = false;
    strictErrorSet = false;
    outOfData = false;
    elementNestingDepth = 0;
    totalEntityExpansion = 0;
    limitExceeded = false;

    // a strict error recorded while scanning (e.g. an unterminated comment) must not be replaced
    if (textToParse.isEmpty())
    {
        lastError = "not enough input";
    }
    else if (! parseHeader())
    {
        if (! strictErrorSet)
            lastError = "malformed header";
    }
    else if (! parseDTD())
    {
        // parseDTD has already recorded its (sticky) error
    }
    else
    {
        if (! strictErrorSet)
            lastError.clear();

        std::unique_ptr<XmlElement> result (readNextElement (! onlyReadOuterDocumentElement));

        // after the root element only whitespace, comments and processing instructions may follow
        if (result != nullptr && ! onlyReadOuterDocumentElement && ! ignoreTrailingContent
              && ! errorOccurred && ! limitExceeded && ! onlyMetadataRemains())
            setFatalError ("content after the root element");

        if (! errorOccurred && ! limitExceeded)
            return result;
    }

    return {};
}

// This scans by hand rather than reusing skipNextWhiteSpace(), because that reports an unterminated
// comment or PI with its own error, and anything unterminated after the root must be reported
// as "content after the root element".
bool XmlDocument::onlyMetadataRemains()
{
    for (;;)
    {
        input.incrementToEndOfWhitespace();

        if (input.isEmpty())
            return true;

        if (*input != '<')
            return false;

        if (input[1] == '!' && input[2] == '-' && input[3] == '-')
        {
            input += 4;
            auto closeComment = input.indexOf (CharPointer_ASCII ("-->"));

            if (closeComment < 0)
                return false;

            input += closeComment + 3;
            continue;
        }

        if (input[1] == '?')
        {
            input += 2;
            auto closeBracket = input.indexOf (CharPointer_ASCII ("?>"));

            if (closeBracket < 0)
                return false;

            input += closeBracket + 2;
            continue;
        }

        return false;
    }
}

bool XmlDocument::parseHeader()
{
    skipNextWhiteSpace();

    if (CharacterFunctions::compareUpTo (input, CharPointer_ASCII ("<?xml"), 5) == 0)
    {
        auto headerEnd = CharacterFunctions::find (input, CharPointer_ASCII ("?>"));

        if (headerEnd.isEmpty())
            return false;

       #if JUCE_DEBUG
        auto encoding = String (input, headerEnd)
                          .fromFirstOccurrenceOf ("encoding", false, true)
                          .fromFirstOccurrenceOf ("=", false, false)
                          .fromFirstOccurrenceOf ("\"", false, false)
                          .upToFirstOccurrenceOf ("\"", false, false)
                          .trim();

        /* If you load an XML document with a non-UTF encoding type, it may have been
           loaded wrongly.. Since all the files are read via the normal juce file streams,
           they're treated as UTF-8, so by the time it gets to the parser, the encoding will
           have been lost. Best plan is to stick to utf-8 or if you have specific files to
           read, use your own code to convert them to a unicode String, and pass that to the
           XML parser.
        */
        jassert (encoding.isEmpty() || encoding.startsWithIgnoreCase ("utf-"));
       #endif

        input = headerEnd + 2;
        skipNextWhiteSpace();
    }

    return true;
}

// The DOCTYPE is read with a real character scanner:
//   <!DOCTYPE name [ SYSTEM "literal" | PUBLIC "literal" "literal" ] [ '[' subset ']' ] '>'
// JUCE supports exactly one kind of DTD content, and supports it exactly: internal GENERAL entities whose
// value is plain text (no markup, and only predefined or numeric character references). ELEMENT and NOTATION
// declarations and comments are skipped, and an ATTLIST is accepted only if no attribute has a default value
// (JUCE never applies defaults). Everything else -- parameter entities, external entities, PIs and conditional
// sections in the subset, entities that refer to entities or contain markup -- fails loudly rather than being
// silently dropped or reinterpreted. No external DTD or entity is ever read.
bool XmlDocument::parseDTD()
{
    if (CharacterFunctions::compareUpTo (input, CharPointer_ASCII ("<!DOCTYPE"), 9) != 0)
        return true;

    input += 9;

    // XML whitespace only (not JUCE's wider isWhitespace), as the DOCTYPE grammar requires
    const auto isSpace = [] (juce_wchar c)  { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    const auto skipSpace = [&]              { while (isSpace (*input)) ++input; };
    const auto malformed = [this]           { setFatalError ("malformed DTD"); return false; };
    const auto unsupported = [this]         { setFatalError ("unsupported DTD declaration"); return false; };
    const auto badReference = [this]        { setFatalError ("malformed character reference"); return false; };
    const auto startsWith = [this] (const char* word)
    {
        return CharacterFunctions::compareUpTo (input, CharPointer_ASCII (word), (int) strlen (word)) == 0;
    };

    // a quoted literal; false if it is not quoted or never closes
    const auto readLiteral = [&] (String& text)
    {
        const auto quote = *input;

        if (quote != '"' && quote != '\'')
            return false;

        ++input;
        const auto start = input;

        while (*input != quote)
        {
            if (input.isEmpty())
                return false;

            ++input;
        }

        text = String (start, input);
        ++input;
        return true;
    };

    // an entity value with its references decoded; the value must be text only
    const auto decodeValue = [&] (const String& raw, String& value)
    {
        XmlEntityText text;
        auto p = raw.getCharPointer();

        while (! p.isEmpty())
        {
            const auto runStart = p;

            while (! p.isEmpty() && *p != '<' && *p != '&' && *p != '%')
                ++p;

            text.append (runStart, p);

            if (p.isEmpty())
                break;

            // markup, and parameter-entity references, are not text
            if (*p == '<' || *p == '%')
                return unsupported();

            ++p; // the ampersand
            const auto bodyStart = p;

            while (! p.isEmpty() && *p != ';' && *p != '&' && *p != '<')
                ++p;

            const String body (bodyStart, p);

            if (*p != ';')
                return body.startsWithChar ('#') ? badReference() : unsupported();

            ++p;

            // These names are case-sensitive here (as in XML) although content references are matched
            // case-insensitively; the stricter rule is intentional so that anything odd fails loudly.
            if (body == "amp")        { text.append (String::charToString ('&')); continue; }
            if (body == "lt")         { text.append (String::charToString ('<')); continue; }
            if (body == "gt")         { text.append (String::charToString ('>')); continue; }
            if (body == "quot")       { text.append (String::charToString ('"')); continue; }
            if (body == "apos")       { text.append (String::charToString ('\'')); continue; }

            if (! body.startsWithChar ('#'))
                return unsupported();   // a reference to another entity (or anything else)

            // the same rules as a numeric reference in the document: digits only, at most 8 significant
            // hex / 12 significant decimal digits, no range validation
            const auto isHex = body[1] == 'x' || body[1] == 'X';
            const auto digits = body.substring (isHex ? 2 : 1);
            auto valid = digits.isNotEmpty() && digits.trimCharactersAtStart ("0").length() <= (isHex ? 8 : 12);
            int64_t charCode = 0;

            for (auto d = digits.getCharPointer(); valid && ! d.isEmpty(); ++d)
            {
                const auto digit = isHex ? CharacterFunctions::getHexDigitValue (*d)
                                         : (*d >= '0' && *d <= '9' ? (int) (*d - '0') : -1);

                if (digit < 0)
                    valid = false;
                else
                    charCode = charCode * (isHex ? 16 : 10) + digit;
            }

            if (! valid)
                return badReference();

            // XML re-parses character references in a replacement text, so one that yields '<' or '&' would
            // be markup or a reference in a conforming parser, while here it would be plain text
            // (checked on the character actually inserted: a huge value narrows to the same juce_wchar)
            const auto decoded = static_cast<juce_wchar> (charCode);

            if (decoded == '<' || decoded == '&')
                return unsupported();

            text.append (String::charToString (decoded));
        }

        value = text.toString();
        return true;
    };

    // ELEMENT, NOTATION and ATTLIST: skipped up to the closing '>' (quotes honoured), except that a
    // parameter-entity reference is unsupported and an ATTLIST must not declare a default value
    // ELEMENT has no literals; NOTATION may have external ids; only ATTLIST can declare a default value
    enum class Declaration { element, notation, attlist };

    const auto skipDeclaration = [&] (Declaration kind)
    {
        if (! isSpace (*input))
            return malformed();

        bool declaresDefault = false;

        for (;;)
        {
            if (input.isEmpty())
                return malformed();

            const auto c = *input;

            if (c == '<')
                return malformed();   // a stray '<' means the declaration was never closed

            if (c == '"' || c == '\'')
            {
                String ignored;

                if (kind == Declaration::element || ! readLiteral (ignored))
                    return malformed();

                declaresDefault = declaresDefault || kind == Declaration::attlist;
                continue;
            }

            if (c == '%')
                return unsupported();

            if (c == '>')
            {
                ++input;
                break;
            }

            if (c == '#' && kind == Declaration::attlist && startsWith ("#FIXED"))
                declaresDefault = true;

            ++input;
        }

        return ! declaresDefault || unsupported();
    };

    const auto parseEntity = [&]
    {
        if (! isSpace (*input))
            return malformed();

        skipSpace();

        if (*input == '%')
            return unsupported();

        const auto nameStart = input;
        const auto nameEnd = XmlIdentifierChars::findEndOfToken (nameStart);

        if (nameEnd == nameStart)
            return malformed();

        input = nameEnd;

        if (! isSpace (*input))
            return malformed();

        const String name (nameStart, nameEnd);
        skipSpace();

        if (*input == '%')
            return unsupported();   // "<!ENTITY e %p;>"

        String raw;

        if (! readLiteral (raw))
            return (startsWith ("SYSTEM") || startsWith ("PUBLIC")) ? unsupported() : malformed();

        skipSpace();

        if (*input != '>')
            return startsWith ("NDATA") ? unsupported() : malformed();

        ++input;
        String value;

        if (! decodeValue (raw, value))
            return false;

        // The first declaration of a name wins. Redeclaring a predefined name (amp, lt, gt, quot, apos) is accepted
        // but never used, because readEntity resolves the predefined names before it looks here.
        internalEntities.emplace (name, value);
        return true;
    };

    // --- the DOCTYPE itself
    if (! isSpace (*input))
        return malformed();

    skipSpace();
    const auto nameEnd = XmlIdentifierChars::findEndOfToken (input);

    if (nameEnd == input)
        return malformed();

    input = nameEnd;
    skipSpace();

    if (startsWith ("SYSTEM") || startsWith ("PUBLIC"))
    {
        const auto isPublic = startsWith ("PUBLIC");
        input += 6;

        if (! isSpace (*input))
            return malformed();

        skipSpace();
        String literal;

        if (! readLiteral (literal))
            return malformed();

        if (isPublic)
        {
            if (! isSpace (*input))
                return malformed();

            skipSpace();

            if (! readLiteral (literal))
                return malformed();
        }

        doctypeHasExternalId = true;
        skipSpace();
    }

    if (*input == '[')
    {
        ++input;

        for (;;)
        {
            skipSpace();

            if (input.isEmpty())
                return malformed();

            const auto c = *input;

            if (c == ']')
            {
                ++input;
                break;
            }

            if (c == '%')
                return unsupported();

            if (c != '<')
                return malformed();

            if (startsWith ("<!--"))
            {
                input += 4;
                const auto close = input.indexOf (CharPointer_ASCII ("-->"));

                if (close < 0)
                    return malformed();

                input += close + 3;
            }
            else if (startsWith ("<?") || startsWith ("<!["))
            {
                return unsupported();
            }
            else if (startsWith ("<!ENTITY"))
            {
                input += 8;

                if (! parseEntity())
                    return false;
            }
            else if (startsWith ("<!ELEMENT"))
            {
                input += 9;

                if (! skipDeclaration (Declaration::element))
                    return false;
            }
            else if (startsWith ("<!NOTATION"))
            {
                input += 10;

                if (! skipDeclaration (Declaration::notation))
                    return false;
            }
            else if (startsWith ("<!ATTLIST"))
            {
                input += 9;

                if (! skipDeclaration (Declaration::attlist))
                    return false;
            }
            else
            {
                return unsupported();
            }
        }

        skipSpace();
    }

    if (*input != '>')
        return malformed();

    ++input;
    return true;
}

void XmlDocument::skipNextWhiteSpace()
{
    for (;;)
    {
        input.incrementToEndOfWhitespace();

        if (input.isEmpty())
        {
            outOfData = true;
            break;
        }

        if (*input == '<')
        {
            if (input[1] == '!'
                 && input[2] == '-'
                 && input[3] == '-')
            {
                input += 4;
                auto closeComment = input.indexOf (CharPointer_ASCII ("-->"));

                if (closeComment < 0)
                {
                    setFatalError ("unterminated comment");
                    break;
                }

                input += closeComment + 3;
                continue;
            }

            if (input[1] == '?')
            {
                input += 2;
                auto closeBracket = input.indexOf (CharPointer_ASCII ("?>"));

                if (closeBracket < 0)
                {
                    setFatalError ("unterminated processing instruction");
                    break;
                }

                input += closeBracket + 2;
                continue;
            }
        }

        break;
    }
}

void XmlDocument::readQuotedString (String& result)
{
    auto quote = readNextChar();
    XmlEntityText buffer;
    bool usingBuffer = false;

    while (! outOfData)
    {
        auto c = readNextChar();

        if (c == quote)
            break;

        --input;

        if (c == '&')
        {
            if (! usingBuffer)
            {
                buffer.append (result);
                usingBuffer = true;
            }

            String entity;
            readEntity (entity);
            buffer.append (entity);
        }
        else
        {
            auto start = input;

            for (;;)
            {
                auto character = *input;

                if (character == quote)
                {
                    if (usingBuffer)
                        buffer.append (start, input);
                    else
                        result.appendCharPointer (start, input);

                    ++input;

                    if (usingBuffer)
                        result = buffer.toString();

                    return;
                }

                if (character == '&')
                {
                    if (usingBuffer)
                        buffer.append (start, input);
                    else
                        result.appendCharPointer (start, input);

                    break;
                }

                if (character == 0)
                {
                    setLastError ("unmatched quotes", false);
                    outOfData = true;
                    break;
                }

                ++input;
            }
        }
    }

    if (usingBuffer)
        result = buffer.toString();
}

XmlElement* XmlDocument::readNextElement (const bool alsoParseSubElements)
{
    XmlElement* node = nullptr;
    skipNextWhiteSpace();

    if (outOfData)
        return nullptr;

    ScopedDepthIncrement depthGuard (elementNestingDepth);

    if (elementNestingDepth > maxElementNestingDepth)
    {
        failBecauseLimitExceeded ("elements nested too deeply");
        return nullptr;
    }

    if (*input == '<')
    {
        ++input;
        auto endOfToken = XmlIdentifierChars::findEndOfToken (input);

        if (endOfToken == input)
        {
            // no tag name - but allow for a gap after the '<' before giving an error
            // (deliberately lenient: "< a>" is still accepted, while "</ a>" is a malformed end tag)
            skipNextWhiteSpace();
            endOfToken = XmlIdentifierChars::findEndOfToken (input);

            if (endOfToken == input)
            {
                setLastError ("tag name missing", false);
                return node;
            }
        }

        node = new XmlElement (input, endOfToken);
        input = endOfToken;
        LinkedListPointer<XmlElement::XmlAttributeNode>::Appender attributeAppender (node->attributes);

        // A fatal expansion limit must not allocate any more attributes.
        while (! limitExceeded && ! strictErrorSet)
        {
            skipNextWhiteSpace();
            auto c = *input;

            // empty tag..
            if (c == '/' && input[1] == '>')
            {
                input += 2;
                break;
            }

            // parse the guts of the element..
            if (c == '>')
            {
                ++input;

                if (alsoParseSubElements)
                    readChildElements (*node);

                break;
            }

            // get an attribute..
            if (XmlIdentifierChars::isIdentifierChar (c))
            {
                auto attNameEnd = XmlIdentifierChars::findEndOfToken (input);

                if (attNameEnd != input)
                {
                    auto attNameStart = input;
                    input = attNameEnd;
                    skipNextWhiteSpace();

                    if (readNextChar() == '=')
                    {
                        skipNextWhiteSpace();
                        auto nextChar = *input;

                        if (nextChar == '"' || nextChar == '\'')
                        {
                            auto* newAtt = new XmlElement::XmlAttributeNode (attNameStart, attNameEnd);
                            readQuotedString (newAtt->attribute.value);
                            attributeAppender.append (newAtt);
                            continue;
                        }

                        setFatalError ("unquoted attribute value");
                    }
                    else
                    {
                        setLastError ("expected '=' after attribute '"
                                        + String (attNameStart, attNameEnd) + "'", false);
                        return node;
                    }
                }
            }
            else
            {
                if (! outOfData)
                    setLastError ("illegal character found in " + node->getTagName() + ": '" + c + "'", false);
            }

            break;
        }
    }

    return node;
}

void XmlDocument::readChildElements (XmlElement& parent)
{
    LinkedListPointer<XmlElement>::Appender childAppender (parent.firstChildElement);

    for (;;)
    {
        auto preWhitespaceInput = input;
        skipNextWhiteSpace();

        if (outOfData)
        {
            setLastError ("unmatched tags", false);
            break;
        }

        if (*input == '<')
        {
            auto c1 = input[1];

            if (c1 == '/')
            {
                // our close tag: "</" name optional-whitespace ">", and the name must be ours
                input += 2;
                const auto nameStart = input;
                const auto nameEnd = XmlIdentifierChars::findEndOfToken (nameStart);
                input = nameEnd;
                input.incrementToEndOfWhitespace();

                if (nameEnd == nameStart || *input != '>')
                {
                    setFatalError ("malformed end tag");
                    break;
                }

                ++input;

                // compare in place: this is the hot path for large documents
                auto expectedName = parent.getTagName().getCharPointer();
                auto closingName = nameStart;

                while (closingName < nameEnd && ! expectedName.isEmpty() && *closingName == *expectedName)
                {
                    ++closingName;
                    ++expectedName;
                }

                if (closingName < nameEnd || ! expectedName.isEmpty())
                    setFatalError ("mismatched end tag");

                break;
            }

            if (c1 == '!' && CharacterFunctions::compareUpTo (input + 2, CharPointer_ASCII ("[CDATA["), 7) == 0)
            {
                input += 9;
                auto inputStart = input;

                for (;;)
                {
                    auto c0 = *input;

                    if (c0 == 0)
                    {
                        setLastError ("unterminated CDATA section", false);
                        outOfData = true;
                        break;
                    }

                    if (c0 == ']' && input[1] == ']' && input[2] == '>')
                    {
                        childAppender.append (XmlElement::createTextElement (String (inputStart, input)));
                        input += 3;
                        break;
                    }

                    ++input;
                }
            }
            else
            {
                // this is some other element, so parse and add it..
                if (auto* n = readNextElement (true))
                    childAppender.append (n);
                else
                    break;
            }
        }
        else  // must be a character block
        {
            input = preWhitespaceInput; // roll back to include the leading whitespace
            MemoryOutputStream textElementContent;
            bool contentShouldBeUsed = ! ignoreEmptyTextElements;

            for (;;)
            {
                if (limitExceeded || strictErrorSet)
                    return;

                auto c = *input;

                if (c == '<')
                {
                    if (input[1] == '!' && input[2] == '-' && input[3] == '-')
                    {
                        input += 4;
                        auto closeComment = input.indexOf (CharPointer_ASCII ("-->"));

                        if (closeComment < 0)
                        {
                            setFatalError ("unterminated comment");
                            return;
                        }

                        input += closeComment + 3;
                        continue;
                    }

                    if (input[1] == '?')
                    {
                        // a processing instruction inside element content is skipped just like
                        // a comment - if we left it for the outer loop, rolling back to the
                        // leading whitespace would put us in front of it again, forever.
                        input += 2;
                        auto closePI = input.indexOf (CharPointer_ASCII ("?>"));

                        if (closePI < 0)
                        {
                            setFatalError ("unterminated processing instruction");
                            return;
                        }

                        input += closePI + 2;
                        continue;
                    }

                    break;
                }

                if (c == 0)
                {
                    setLastError ("unmatched tags", false);
                    outOfData = true;
                    return;
                }

                if (c == '&')
                {
                    String entity;
                    readEntity (entity);
                    textElementContent << entity;
                    contentShouldBeUsed = contentShouldBeUsed || entity.containsNonWhitespaceChars();
                }
                else
                {
                    for (;; ++input)
                    {
                        auto nextChar = *input;

                        if (nextChar == '\r')
                        {
                            nextChar = '\n';

                            if (input[1] == '\n')
                                continue;
                        }

                        if (nextChar == '<' || nextChar == '&')
                            break;

                        if (nextChar == 0)
                        {
                            setLastError ("unmatched tags", false);
                            outOfData = true;
                            return;
                        }

                        textElementContent.appendUTF8Char (nextChar);
                        contentShouldBeUsed = contentShouldBeUsed || ! CharacterFunctions::isWhitespace (nextChar);
                    }
                }
            }

            if (contentShouldBeUsed)
                childAppender.append (XmlElement::createTextElement (textElementContent.toUTF8()));
        }

        // every pass must consume something, or malformed input could spin here forever
        if (input == preWhitespaceInput)
        {
            setLastError ("XML parser made no progress", false);
            outOfData = true;
            break;
        }
    }
}

void XmlDocument::readEntity (String& result)
{
    // skip over the ampersand
    ++input;

    if (input.compareIgnoreCaseUpTo (CharPointer_ASCII ("amp;"), 4) == 0)
    {
        input += 4;
        result += '&';
    }
    else if (input.compareIgnoreCaseUpTo (CharPointer_ASCII ("quot;"), 5) == 0)
    {
        input += 5;
        result += '"';
    }
    else if (input.compareIgnoreCaseUpTo (CharPointer_ASCII ("apos;"), 5) == 0)
    {
        input += 5;
        result += '\'';
    }
    else if (input.compareIgnoreCaseUpTo (CharPointer_ASCII ("lt;"), 3) == 0)
    {
        input += 3;
        result += '<';
    }
    else if (input.compareIgnoreCaseUpTo (CharPointer_ASCII ("gt;"), 3) == 0)
    {
        input += 3;
        result += '>';
    }
    else if (*input == '#')
    {
        int64_t charCode = 0;
        ++input;

        if (*input == 'x' || *input == 'X')
        {
            ++input;
            int numChars = 0;     // significant digits: leading zeros are valid and do not count
            bool sawDigit = false;

            while (input[0] != ';')
            {
                if (input.isEmpty())
                {
                    setLastError ("unexpected end of input", true);
                    return;
                }

                auto hexValue = CharacterFunctions::getHexDigitValue (input[0]);

                if (hexValue < 0)
                {
                    setFatalError ("malformed character reference");
                    return;
                }

                sawDigit = true;

                if ((charCode != 0 || hexValue != 0) && ++numChars > 8)
                {
                    setFatalError ("malformed character reference");
                    return;
                }

                charCode = (charCode << 4) | hexValue;
                ++input;
            }

            if (! sawDigit)
            {
                setFatalError ("malformed character reference");
                return;
            }

            ++input;
        }
        else if (input[0] >= '0' && input[0] <= '9')
        {
            int numChars = 0;

            for (;;)
            {
                const auto firstChar = input[0];

                if (firstChar == 0)
                {
                    setLastError ("unexpected end of input", true);
                    return;
                }

                if (firstChar == ';')
                    break;

                if (firstChar < '0' || firstChar > '9')
                {
                    setFatalError ("malformed character reference");
                    return;
                }

                // leading zeros are valid and do not count towards the digit limit
                if ((charCode != 0 || firstChar != '0') && ++numChars > 12)
                {
                    setFatalError ("malformed character reference");
                    return;
                }

                charCode = charCode * 10 + ((int) firstChar - '0');
                ++input;
            }

            ++input;
        }
        else
        {
            // "&#" with no digits at all; running off the end of the input is reported by the callers
            if (input[0] != 0)
                setFatalError ("malformed character reference");
            else
                setLastError ("illegal escape sequence", true);

            result += '&';
            return;
        }

        result << (juce_wchar) charCode;
    }
    else
    {
        auto entityNameStart = input;
        auto closingSemiColon = input.indexOf ((juce_wchar) ';');

        if (closingSemiColon < 0)
        {
            setFatalError ("unterminated entity reference");
            result += '&';
        }
        else
        {
            input += closingSemiColon + 1;
            result += expandNamedEntity (String (entityNameStart, (size_t) closingSemiColon));
        }
    }
}

String XmlDocument::expandNamedEntity (const String& entity)
{
    if (limitExceeded)
        return {};

    const auto found = internalEntities.find (entity);

    if (found != internalEntities.end())
    {
        // every expansion is charged, so many references to a large value stay bounded
        if (! chargeEntityExpansion ((size_t) found->second.length()))
            return {};

        return found->second;
    }

    // an entity that is not declared here may be declared in an external DTD, which is never read
    if (doctypeHasExternalId)
    {
        setFatalError ("unsupported DTD declaration");
        return {};
    }

    // Behaviour change: an unknown name always recovers as TEXT now. Before, a "name" that started with '<'
    // (malformed input such as "&<b/>;") was re-parsed as markup by the removed entity-markup branch.
    setLastError ("unknown entity", true);
    return entity;
}

}
