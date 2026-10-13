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

std::unique_ptr<XmlElement> XmlDocument::getDocumentElement (const bool onlyReadOuterDocumentElement)
{
    if (originalText.isEmpty() && inputSource != nullptr)
    {
        std::unique_ptr<InputStream> in (inputSource->createInputStream());

        if (in != nullptr)
        {
            MemoryOutputStream data;
            data.writeFromInputStream (*in, onlyReadOuterDocumentElement ? 8192 : -1);

           #if JUCE_STRING_UTF_TYPE == 8
            if (data.getDataSize() > 2)
            {
                data.writeByte (0);
                auto* text = static_cast<const char*> (data.getData());

                if (CharPointer_UTF16::isByteOrderMarkBigEndian (text)
                      || CharPointer_UTF16::isByteOrderMarkLittleEndian (text))
                {
                    originalText = data.toString();
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
            originalText = data.toString();
           #endif
        }
    }

    return parseDocumentElement (originalText.getCharPointer(), onlyReadOuterDocumentElement);
}

std::unique_ptr<XmlElement> XmlDocument::getDocumentElementIfTagMatches (StringRef requiredTag)
{
    if (auto xml = getDocumentElement (true))
        if (xml->hasTagName (requiredTag))
            return getDocumentElement (false);

    return {};
}

// Untrusted input must not be able to overflow the stack, so the recursive descent
// through elements (including elements produced by DTD entity expansion) and through
// nested entity definitions is bounded (see XmlDocument::maxElementNestingDepth and
// maxEntityNestingDepth). Legitimate documents nest far below these limits.
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

    // Rebuild from right to left, just like the old in-place parameter substitution.
    // Inserted tokens are not re-scanned, but their declarations are visible to earlier
    // references. Index declarations so repeated unknown references also remain cheap.
    struct XmlDTDTokenList
    {
        explicit XmlDTDTokenList (const StringArray& tokens)
            : original (tokens), remaining (tokens.size())
        {
            for (int i = 2; i < original.size(); ++i)
                if (original[i - 1] == "%" && original[i - 2].equalsIgnoreCase ("<!entity"))
                    originalDefinitions.emplace (original[i], i);
        }

        const String& getToken (int index) const
        {
            return index < remaining ? original[index] : reversed[reversed.size() - 1 - (index - remaining)];
        }

        std::optional<std::pair<String, String>> findParameterEntity (const String& name) const
        {
            const auto found = originalDefinitions.find (name);

            // The first original declaration still wins if its value and filename
            // are entirely within the untouched prefix.
            if (found != originalDefinitions.end() && found->second + 2 < remaining)
                return std::make_pair (original[found->second + 1], original[found->second + 2]);

            // Substitution can also form a declaration across the prefix/suffix boundary.
            for (int i = jmax (2, remaining - 2); i < remaining + 2; ++i)
                if (getToken (i) == name && getToken (i - 1) == "%"
                                        && getToken (i - 2).equalsIgnoreCase ("<!entity"))
                    return std::make_pair (getToken (i + 1), getToken (i + 2));

            const auto expanded = expandedDefinitions.find (name);

            if (expanded != expandedDefinitions.end())
                return std::make_pair (reversed[expanded->second - 1], reversed[expanded->second - 2]);

            return {};
        }

        void append (const String& token)
        {
            reversed.add (token);
            const auto size = reversed.size();

            if (size >= 3 && token.equalsIgnoreCase ("<!entity") && reversed[size - 2] == "%")
                expandedDefinitions[reversed[size - 3]] = size - 3;
        }

        const StringArray& original;
        int remaining;
        StringArray reversed;
        std::map<String, int> originalDefinitions, expandedDefinitions;
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
    // once a limit has been exceeded the parse has failed, so keep that error
    if (limitExceeded)
        return;

    lastError = desc;
    errorOccurred = ! carryOn;
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

String XmlDocument::getFileContents (const String& filename)
{
    if (inputSource != nullptr)
    {
        std::unique_ptr<InputStream> in (inputSource->createInputStreamFor (filename.trim().unquoted()));

        if (in != nullptr)
        {
            // Bound both decoded expansion and cumulative input bytes. Separate counters
            // avoid charging normal external text twice, while counting even zero bytes
            // that disappear during decoding. Read one extra byte to detect overflow.
            const auto maxBytes = jmin (4 * (maxTotalEntityExpansion - totalEntityExpansion) + 4,
                                       maxTotalEntityExpansion - totalExternalEntityBytes);
            MemoryOutputStream data;
            data.writeFromInputStream (*in, (int64) maxBytes + 1);

            if (data.getDataSize() > maxBytes)
            {
                failBecauseLimitExceeded ("entity expansion too large");
                return {};
            }

            totalExternalEntityBytes += data.getDataSize();
            auto contents = data.toString();
            return chargeEntityExpansion ((size_t) contents.length()) ? contents : String();
        }
    }

    return {};
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
    dtdText.clear();
    tokenisedDTD.clear();
    errorOccurred = false;
    outOfData = false;
    needToLoadDTD = true;
    elementNestingDepth = 0;
    entityNestingDepth = 0;
    totalEntityExpansion = 0;
    totalExternalEntityBytes = 0;
    limitExceeded = false;

    if (textToParse.isEmpty())
    {
        lastError = "not enough input";
    }
    else if (! parseHeader())
    {
        lastError = "malformed header";
    }
    else if (! parseDTD())
    {
        lastError = "malformed DTD";
    }
    else
    {
        lastError.clear();
        std::unique_ptr<XmlElement> result (readNextElement (! onlyReadOuterDocumentElement));

        if (! errorOccurred && ! limitExceeded)
            return result;
    }

    return {};
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

bool XmlDocument::parseDTD()
{
    if (CharacterFunctions::compareUpTo (input, CharPointer_ASCII ("<!DOCTYPE"), 9) == 0)
    {
        input += 9;
        auto dtdStart = input;

        for (int n = 1; n > 0;)
        {
            auto c = readNextChar();

            if (outOfData)
                return false;

            if (c == '<')
                ++n;
            else if (c == '>')
                --n;
        }

        dtdText = String (dtdStart, input - 1).trim();
    }

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
                    outOfData = true;
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
                    outOfData = true;
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
        while (! limitExceeded)
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
                // our close tag..
                auto closeTag = input.indexOf ((juce_wchar) '>');

                if (closeTag >= 0)
                    input += closeTag + 1;

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
                if (limitExceeded)
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
                            setLastError ("unterminated comment", false);
                            outOfData = true;
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
                            setLastError ("unterminated processing instruction", false);
                            outOfData = true;
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

                    if (entity.startsWithChar ('<') && entity [1] != 0)
                    {
                        auto oldInput = input;
                        auto oldOutOfData = outOfData;

                        input = entity.getCharPointer();
                        outOfData = false;

                        while (auto* n = readNextElement (true))
                            childAppender.append (n);

                        input = oldInput;
                        outOfData = oldOutOfData || limitExceeded;
                    }
                    else
                    {
                        textElementContent << entity;
                        contentShouldBeUsed = contentShouldBeUsed || entity.containsNonWhitespaceChars();
                    }
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
            int numChars = 0;

            while (input[0] != ';')
            {
                if (input.isEmpty())
                {
                    setLastError ("unexpected end of input", true);
                    return;
                }

                auto hexValue = CharacterFunctions::getHexDigitValue (input[0]);

                if (hexValue < 0 || ++numChars > 8)
                {
                    setLastError ("illegal escape sequence", true);
                    break;
                }

                charCode = (charCode << 4) | hexValue;
                ++input;
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

                if (++numChars > 12)
                {
                    setLastError ("illegal escape sequence", true);
                    break;
                }

                charCode = charCode * 10 + ((int) firstChar - '0');
                ++input;
            }

            ++input;
        }
        else
        {
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
            outOfData = true;
            result += '&';
        }
        else
        {
            input += closingSemiColon + 1;
            result += expandExternalEntity (String (entityNameStart, (size_t) closingSemiColon));
        }
    }
}

String XmlDocument::expandEntity (const String& ent)
{
    if (ent.equalsIgnoreCase ("amp"))   return String::charToString ('&');
    if (ent.equalsIgnoreCase ("quot"))  return String::charToString ('"');
    if (ent.equalsIgnoreCase ("apos"))  return String::charToString ('\'');
    if (ent.equalsIgnoreCase ("lt"))    return String::charToString ('<');
    if (ent.equalsIgnoreCase ("gt"))    return String::charToString ('>');

    if (ent[0] == '#')
    {
        auto char1 = ent[1];

        if (char1 == 'x' || char1 == 'X')
            return String::charToString (static_cast<juce_wchar> (ent.substring (2).getHexValue32()));

        if (char1 >= '0' && char1 <= '9')
            return String::charToString (static_cast<juce_wchar> (ent.substring (1).getIntValue()));

        setLastError ("illegal escape sequence", false);
        return String::charToString ('&');
    }

    return expandExternalEntity (ent);
}

String XmlDocument::expandExternalEntity (const String& entity)
{
    if (limitExceeded)
        return {};

    ScopedDepthIncrement depthGuard (entityNestingDepth);

    if (entityNestingDepth > maxEntityNestingDepth)
    {
        failBecauseLimitExceeded ("entities nested too deeply");
        return {};
    }

    if (needToLoadDTD)
    {
        if (dtdText.isNotEmpty())
        {
            dtdText = dtdText.trimCharactersAtEnd (">");
            tokenisedDTD.addTokens (dtdText, true);

            if (tokenisedDTD[tokenisedDTD.size() - 2].equalsIgnoreCase ("system")
                 && tokenisedDTD[tokenisedDTD.size() - 1].isQuotedString())
            {
                auto fn = tokenisedDTD[tokenisedDTD.size() - 1];

                tokenisedDTD.clear();
                tokenisedDTD.addTokens (getFileContents (fn), true);
            }
            else
            {
                tokenisedDTD.clear();
                auto openBracket = dtdText.indexOfChar ('[');

                if (openBracket > 0)
                {
                    auto closeBracket = dtdText.lastIndexOfChar (']');

                    if (closeBracket > openBracket)
                        tokenisedDTD.addTokens (dtdText.substring (openBracket + 1,
                                                                   closeBracket), true);
                }
            }

            XmlDTDTokenList tokens (tokenisedDTD);

            while (tokens.remaining > 0)
            {
                const auto& token = tokenisedDTD[tokens.remaining - 1];
                StringArray replacement;

                if (token.startsWithChar ('%') && token.endsWithChar (';'))
                {
                    const auto name = token.substring (1, token.length() - 1);
                    String parsed;

                    if (const auto declaration = tokens.findParameterEntity (name))
                    {
                        if (! chargeEntityExpansion ((size_t) declaration->first.length()))
                            return {};

                        const auto value = declaration->first.trimCharactersAtEnd (">");
                        parsed = value.equalsIgnoreCase ("system")
                                   ? getFileContents (declaration->second.trimCharactersAtEnd (">"))
                                   : value.trim().unquoted();
                    }
                    else
                    {
                        if (! chargeEntityExpansion ((size_t) name.length()))
                            return {};

                        parsed = name;
                    }

                    if (limitExceeded)
                        return {};

                    replacement.addTokens (parsed, true);
                }
                else
                {
                    replacement.add (token);
                }

                --tokens.remaining;

                for (int i = replacement.size(); --i >= 0;)
                    tokens.append (replacement[i]);
            }

            std::reverse (tokens.reversed.begin(), tokens.reversed.end());
            tokenisedDTD.swapWith (tokens.reversed);
        }

        needToLoadDTD = false;
    }

    for (int i = 0; i < tokenisedDTD.size(); ++i)
    {
        if (tokenisedDTD[i] == entity)
        {
            if (tokenisedDTD[i - 1].equalsIgnoreCase ("<!entity"))
            {
                // The raw token is an upper bound for the trimmed, unquoted value, so charge for it
                // before any of the trimming below allocates
                if (! chargeEntityExpansion ((size_t) tokenisedDTD [i + 1].length()))
                    return {};

                auto ent = tokenisedDTD [i + 1].trimCharactersAtEnd (">").trim().unquoted();

                if (! ent.containsChar ('&'))
                    return ent;

                // Scan the source once and append replacements without copying the prefix.
                XmlEntityText buffer;
                auto text = ent.getCharPointer();

                while (! text.isEmpty())
                {
                    const auto literalStart = text;

                    while (! text.isEmpty() && *text != '&')
                        ++text;

                    buffer.append (literalStart, text);

                    if (text.isEmpty())
                        break;

                    auto ampersand = text++;
                    auto start = text;

                    while (! text.isEmpty() && *text != ';')
                        ++text;

                    if (text.isEmpty())
                    {
                        setLastError ("entity without terminating semi-colon", false);
                        buffer.append (ampersand, text);
                        break;
                    }

                    auto resolved = expandEntity (String (start, text));

                    if (limitExceeded || ! chargeEntityExpansion ((size_t) resolved.length()))
                        return {};

                    buffer.append (resolved);
                    ++text;
                }

                return buffer.toString();
            }
        }
    }

    setLastError ("unknown entity", true);
    return entity;
}

}
