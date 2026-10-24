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
static bool isValidXmlNameStartCharacter (juce_wchar character) noexcept
{
    return character == ':'
        || character == '_'
        || (character >= 'a'     && character <= 'z')
        || (character >= 'A'     && character <= 'Z')
        || (character >= 0xc0    && character <= 0xd6)
        || (character >= 0xd8    && character <= 0xf6)
        || (character >= 0xf8    && character <= 0x2ff)
        || (character >= 0x370   && character <= 0x37d)
        || (character >= 0x37f   && character <= 0x1fff)
        || (character >= 0x200c  && character <= 0x200d)
        || (character >= 0x2070  && character <= 0x218f)
        || (character >= 0x2c00  && character <= 0x2fef)
        || (character >= 0x3001  && character <= 0xd7ff)
        || (character >= 0xf900  && character <= 0xfdcf)
        || (character >= 0xfdf0  && character <= 0xfffd)
        || (character >= 0x10000 && character <= 0xeffff);
}

static bool isValidXmlNameBodyCharacter (juce_wchar character) noexcept
{
    return isValidXmlNameStartCharacter (character)
        || character == '-'
        || character == '.'
        || character == 0xb7
        || (character >= '0'    && character <= '9')
        || (character >= 0x300  && character <= 0x036f)
        || (character >= 0x203f && character <= 0x2040);
}

XmlElement::XmlAttributeNode::XmlAttributeNode (const XmlAttributeNode& other) noexcept
    : attribute (other.attribute)
{
}

XmlElement::XmlAttributeNode::XmlAttributeNode (const Identifier& n, const String& v) noexcept
    : attribute { n, v }
{
    jassert (isValidXmlName (attribute.name));
}

XmlElement::XmlAttributeNode::XmlAttributeNode (String::CharPointerType nameStart, String::CharPointerType nameEnd)
    : XmlAttributeNode ({ nameStart, nameEnd }, {})
{
}

//==============================================================================
XmlElement::XmlElement (const String& tag)
    : tagName (StringPool::getGlobalPool().getPooledString (tag))
{
    jassert (isValidXmlName (tagName));
}

XmlElement::XmlElement (const char* tag)
    : tagName (StringPool::getGlobalPool().getPooledString (tag))
{
    jassert (isValidXmlName (tagName));
}

XmlElement::XmlElement (StringRef tag)
    : tagName (StringPool::getGlobalPool().getPooledString (tag))
{
    jassert (isValidXmlName (tagName));
}

XmlElement::XmlElement (const Identifier& tag)
    : tagName (tag.toString())
{
    jassert (isValidXmlName (tagName));
}

XmlElement::XmlElement (String::CharPointerType tagNameStart, String::CharPointerType tagNameEnd)
    : tagName (StringPool::getGlobalPool().getPooledString (tagNameStart, tagNameEnd))
{
    jassert (isValidXmlName (tagName));
}

XmlElement::XmlElement (int /*dummy*/) noexcept
{
}

XmlElement::XmlElement (const XmlElement& other)
    : tagName (other.tagName)
{
    copyChildrenAndAttributesFrom (other);
}

XmlElement& XmlElement::operator= (const XmlElement& other)
{
    if (this != &other)
    {
        removeAllAttributes();
        deleteAllChildElements();
        tagName = other.tagName;
        copyChildrenAndAttributesFrom (other);
    }

    return *this;
}

XmlElement::XmlElement (XmlElement&& other) noexcept
    : nextListItem      (std::move (other.nextListItem)),
      firstChildElement (std::move (other.firstChildElement)),
      attributes        (std::move (other.attributes)),
      tagName           (std::move (other.tagName))
{
}

XmlElement& XmlElement::operator= (XmlElement&& other) noexcept
{
    jassert (this != &other); // hopefully the compiler should make this situation impossible!

    removeAllAttributes();
    deleteAllChildElements();

    nextListItem      = std::move (other.nextListItem);
    firstChildElement = std::move (other.firstChildElement);
    attributes        = std::move (other.attributes);
    tagName           = std::move (other.tagName);

    return *this;
}

void XmlElement::copyChildrenAndAttributesFrom (const XmlElement& other)
{
    jassert (firstChildElement.get() == nullptr);
    firstChildElement.addCopyOfList (other.firstChildElement);

    jassert (attributes.get() == nullptr);
    attributes.addCopyOfList (other.attributes);
}

XmlElement::~XmlElement() noexcept
{
    firstChildElement.deleteAll();
    attributes.deleteAll();
}

//==============================================================================
namespace XmlOutputFunctions
{
    namespace LegalCharLookupTable
    {
        template <int c>
        struct Bit
        {
            enum { v = ((c >= 'a' && c <= 'z')
                     || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9')
                     || c == ' ' || c == '.'  || c == ',' || c == ';'
                     || c == ':' || c == '-'  || c == '(' || c == ')'
                     || c == '_' || c == '+'  || c == '=' || c == '?'
                     || c == '!' || c == '$'  || c == '#' || c == '@'
                     || c == '[' || c == ']'  || c == '/' || c == '|'
                     || c == '*' || c == '%'  || c == '~' || c == '{'
                     || c == '}' || c == '\'' || c == '\\')
                        ? (1 << (c & 7)) : 0 };
        };

        template <int tableIndex>
        struct Byte
        {
            enum { v = (int) Bit<tableIndex * 8 + 0>::v | (int) Bit<tableIndex * 8 + 1>::v
                     | (int) Bit<tableIndex * 8 + 2>::v | (int) Bit<tableIndex * 8 + 3>::v
                     | (int) Bit<tableIndex * 8 + 4>::v | (int) Bit<tableIndex * 8 + 5>::v
                     | (int) Bit<tableIndex * 8 + 6>::v | (int) Bit<tableIndex * 8 + 7>::v };
        };

        static bool isLegal (uint32 c) noexcept
        {
            static const unsigned char legalChars[] = { Byte< 0>::v, Byte< 1>::v, Byte< 2>::v, Byte< 3>::v,
                                                        Byte< 4>::v, Byte< 5>::v, Byte< 6>::v, Byte< 7>::v,
                                                        Byte< 8>::v, Byte< 9>::v, Byte<10>::v, Byte<11>::v,
                                                        Byte<12>::v, Byte<13>::v, Byte<14>::v, Byte<15>::v };

            return c < sizeof (legalChars) * 8
                     && (legalChars[c >> 3] & (1 << (c & 7))) != 0;
        }
    }

    static void escapeIllegalXmlChars (OutputStream& outputStream, const String& text, bool changeNewLines)
    {
        auto t = text.getCharPointer();

        for (;;)
        {
            auto character = (uint32) t.getAndAdvance();

            if (character == 0)
                break;

            if (LegalCharLookupTable::isLegal (character))
            {
                outputStream << (char) character;
            }
            else
            {
                switch (character)
                {
                    case '&':   outputStream << "&amp;"; break;
                    case '"':   outputStream << "&quot;"; break;
                    case '>':   outputStream << "&gt;"; break;
                    case '<':   outputStream << "&lt;"; break;

                    case '\n':
                    case '\r':
                        if (! changeNewLines)
                        {
                            outputStream << (char) character;
                            break;
                        }
                        JUCE_FALLTHROUGH
                    default:
                        outputStream << "&#" << ((int) character) << ';';
                        break;
                }
            }
        }
    }

    static void writeSpaces (OutputStream& out, const size_t numSpaces)
    {
        out.writeRepeatedByte (' ', numSpaces);
    }
}

void XmlElement::writeElementAsText (OutputStream& outputStream,
                                     int indentationLevel,
                                     int lineWrapLength,
                                     const char* newLineChars) const
{
    if (indentationLevel >= 0)
        XmlOutputFunctions::writeSpaces (outputStream, (size_t) indentationLevel);

    if (! isTextElement())
    {
        outputStream.writeByte ('<');
        outputStream << tagName;

        {
            auto attIndent = (size_t) (indentationLevel + tagName.length() + 1);
            int lineLen = 0;

            for (const auto& [name, value] : getAttributeIterator())
            {
                if (lineLen > lineWrapLength && indentationLevel >= 0)
                {
                    outputStream << newLineChars;
                    XmlOutputFunctions::writeSpaces (outputStream, attIndent);
                    lineLen = 0;
                }

                auto startPos = outputStream.getPosition();
                outputStream.writeByte (' ');
                outputStream << name;
                outputStream.write ("=\"", 2);
                XmlOutputFunctions::escapeIllegalXmlChars (outputStream, value, true);
                outputStream.writeByte ('"');
                lineLen += (int) (outputStream.getPosition() - startPos);
            }
        }

        if (auto* child = firstChildElement.get())
        {
            outputStream.writeByte ('>');
            bool lastWasTextNode = false;

            for (; child != nullptr; child = child->nextListItem)
            {
                if (child->isTextElement())
                {
                    XmlOutputFunctions::escapeIllegalXmlChars (outputStream, child->getText(), false);
                    lastWasTextNode = true;
                }
                else
                {
                    if (indentationLevel >= 0 && ! lastWasTextNode)
                        outputStream << newLineChars;

                    child->writeElementAsText (outputStream,
                                               indentationLevel < 0 ? -1  // (single-line mode must stay single-line, even after a text node)
                                                                    : (lastWasTextNode ? 0 : indentationLevel + 2), lineWrapLength,
                                               newLineChars);
                    lastWasTextNode = false;
                }
            }

            if (indentationLevel >= 0 && ! lastWasTextNode)
            {
                outputStream << newLineChars;
                XmlOutputFunctions::writeSpaces (outputStream, (size_t) indentationLevel);
            }

            outputStream.write ("</", 2);
            outputStream << tagName;
            outputStream.writeByte ('>');
        }
        else
        {
            outputStream.write ("/>", 2);
        }
    }
    else
    {
        XmlOutputFunctions::escapeIllegalXmlChars (outputStream, getText(), false);
    }
}

XmlElement::TextFormat::TextFormat() {}

XmlElement::TextFormat XmlElement::TextFormat::singleLine() const
{
    auto f = *this;
    f.newLineChars = nullptr;
    return f;
}

XmlElement::TextFormat XmlElement::TextFormat::withoutHeader() const
{
    auto f = *this;
    f.addDefaultHeader = false;
    return f;
}

String XmlElement::toString (const TextFormat& options) const
{
    MemoryOutputStream mem (2048);
    writeTo (mem, options);
    return mem.toUTF8();
}

void XmlElement::writeTo (OutputStream& output, const TextFormat& options) const
{
    if (options.customHeader.isNotEmpty())
    {
        output << options.customHeader;

        if (options.newLineChars == nullptr)
            output.writeByte (' ');
        else
            output << options.newLineChars
                   << options.newLineChars;
    }
    else if (options.addDefaultHeader)
    {
        output << "<?xml version=\"1.0\" encoding=\"";

        if (options.customEncoding.isNotEmpty())
            output << options.customEncoding;
        else
            output << "UTF-8";

        output << "\"?>";

        if (options.newLineChars == nullptr)
            output.writeByte (' ');
        else
            output << options.newLineChars
                   << options.newLineChars;
    }

    if (options.dtd.isNotEmpty())
    {
        output << options.dtd;

        if (options.newLineChars == nullptr)
            output.writeByte (' ');
        else
            output << options.newLineChars;
    }

    writeElementAsText (output, options.newLineChars == nullptr ? -1 : 0,
                        options.lineWrapLength,
                        options.newLineChars);

    if (options.newLineChars != nullptr)
        output << options.newLineChars;
}

bool XmlElement::writeTo (const File& destinationFile, const TextFormat& options) const
{
    TemporaryFile tempFile (destinationFile);

    {
        FileOutputStream out (tempFile.getFile());

        if (! out.openedOk())
            return false;

        writeTo (out, options);
        out.flush(); // (called explicitly to force an fsync on posix)

        if (out.getStatus().failed())
            return false;
    }

    return tempFile.overwriteTargetFileWithTemporary();
}

String XmlElement::createDocument (StringRef dtdToUse, bool allOnOneLine, bool includeXmlHeader,
                                   StringRef encodingType, int lineWrapLength) const
{
    TextFormat options;
    options.dtd = dtdToUse;
    options.customEncoding = encodingType;
    options.addDefaultHeader = includeXmlHeader;
    options.lineWrapLength = lineWrapLength;

    if (allOnOneLine)
        options.newLineChars = nullptr;

    return toString (options);
}

void XmlElement::writeToStream (OutputStream& output, StringRef dtdToUse,
                                bool allOnOneLine, bool includeXmlHeader,
                                StringRef encodingType, int lineWrapLength) const
{
    TextFormat options;
    options.dtd = dtdToUse;
    options.customEncoding = encodingType;
    options.addDefaultHeader = includeXmlHeader;
    options.lineWrapLength = lineWrapLength;

    if (allOnOneLine)
        options.newLineChars = nullptr;

    writeTo (output, options);
}

bool XmlElement::writeToFile (const File& file, StringRef dtdToUse,
                              StringRef encodingType, int lineWrapLength) const
{
    TextFormat options;
    options.dtd = dtdToUse;
    options.customEncoding = encodingType;
    options.lineWrapLength = lineWrapLength;

    return writeTo (file, options);
}

//==============================================================================
bool XmlElement::hasTagName (StringRef possibleTagName) const noexcept
{
    const bool matches = tagName.equalsIgnoreCase (possibleTagName);

    // XML tags should be case-sensitive, so although this method allows a
    // case-insensitive match to pass, you should try to avoid this.
    jassert ((! matches) || tagName == possibleTagName);

    return matches;
}

String XmlElement::getNamespace() const
{
    return tagName.upToFirstOccurrenceOf (":", false, false);
}

String XmlElement::getTagNameWithoutNamespace() const
{
    return tagName.fromLastOccurrenceOf (":", false, false);
}

bool XmlElement::hasTagNameIgnoringNamespace (StringRef possibleTagName) const
{
    return hasTagName (possibleTagName) || getTagNameWithoutNamespace() == possibleTagName;
}

XmlElement* XmlElement::getNextElementWithTagName (StringRef requiredTagName) const
{
    auto* e = nextListItem.get();

    while (e != nullptr && ! e->hasTagName (requiredTagName))
        e = e->nextListItem;

    return e;
}

void XmlElement::setTagName (StringRef newTagName)
{
    jassert (isValidXmlName (newTagName));
    tagName = StringPool::getGlobalPool().getPooledString (newTagName);
}

//==============================================================================
int XmlElement::getNumAttributes() const noexcept
{
    return attributes.size();
}

static const String& getEmptyStringRef() noexcept
{
    static String empty;
    return empty;
}

const String& XmlElement::getAttributeName (const int index) const noexcept
{
    if (auto* att = attributes[index].get())
        return att->attribute.name.toString();

    return getEmptyStringRef();
}

const String& XmlElement::getAttributeValue (const int index) const noexcept
{
    if (auto* att = attributes[index].get())
        return att->attribute.value;

    return getEmptyStringRef();
}

const XmlAttribute* XmlElement::getAttribute (StringRef attributeName) const noexcept
{
    for (const auto& att : getAttributeIterator())
        if (att.name == attributeName)
            return &att;

    return nullptr;
}

bool XmlElement::hasAttribute (StringRef attributeName) const noexcept
{
    return getAttribute (attributeName) != nullptr;
}

//==============================================================================
const String& XmlElement::getStringAttribute (StringRef attributeName) const noexcept
{
    if (auto* att = getAttribute (attributeName))
        return att->value;

    return getEmptyStringRef();
}

String XmlElement::getStringAttribute (StringRef attributeName, const String& defaultReturnValue) const
{
    if (auto* att = getAttribute (attributeName))
        return att->value;

    return defaultReturnValue;
}

int XmlElement::getIntAttribute (StringRef attributeName, const int defaultReturnValue) const
{
    if (auto* att = getAttribute (attributeName))
        return att->value.getIntValue();

    return defaultReturnValue;
}

double XmlElement::getDoubleAttribute (StringRef attributeName, const double defaultReturnValue) const
{
    if (auto* att = getAttribute (attributeName))
        return att->value.getDoubleValue();

    return defaultReturnValue;
}

bool XmlElement::getBoolAttribute (StringRef attributeName, const bool defaultReturnValue) const
{
    if (auto* att = getAttribute (attributeName))
    {
        auto firstChar = *(att->value.getCharPointer().findEndOfWhitespace());

        return firstChar == '1'
            || firstChar == 't'
            || firstChar == 'y'
            || firstChar == 'T'
            || firstChar == 'Y';
    }

    return defaultReturnValue;
}

bool XmlElement::compareAttribute (StringRef attributeName,
                                   StringRef stringToCompareAgainst,
                                   const bool ignoreCase) const noexcept
{
    if (auto* att = getAttribute (attributeName))
        return att->equals (attributeName, stringToCompareAgainst, ignoreCase);

    return false;
}

bool XmlElement::compareAttribute (const XmlAttribute& other, const bool ignoreCase) const noexcept
{
    return compareAttribute (other.name, other.value, ignoreCase);
}

//==============================================================================
void XmlElement::setAttribute (const Identifier& attributeName, const String& value)
{
    if (attributes == nullptr)
    {
        attributes = new XmlAttributeNode (attributeName, value);
    }
    else
    {
        for (auto* att = attributes.get(); ; att = att->nextListItem)
        {
            if (att->attribute.name == attributeName)
            {
                att->attribute.value = value;
                break;
            }

            if (att->nextListItem == nullptr)
            {
                att->nextListItem = new XmlAttributeNode (attributeName, value);
                break;
            }
        }
    }
}

void XmlElement::setAttribute (const Identifier& attributeName, const int number)
{
    setAttribute (attributeName, String (number));
}

void XmlElement::setAttribute (const Identifier& attributeName, const double number)
{
    setAttribute (attributeName, serialiseDouble (number));
}

void XmlElement::removeAttribute (const Identifier& attributeName) noexcept
{
    for (auto* att = &attributes; att->get() != nullptr; att = &(att->get()->nextListItem))
    {
        if (att->get()->attribute.name == attributeName)
        {
            delete att->removeNext();
            break;
        }
    }
}

void XmlElement::removeAllAttributes() noexcept
{
    attributes.deleteAll();
}

//==============================================================================
int XmlElement::getNumChildElements() const noexcept
{
    return firstChildElement.size();
}

XmlElement* XmlElement::getChildElement (const int index) const noexcept
{
    return firstChildElement[index].get();
}

XmlElement* XmlElement::getChildByName (StringRef childName) const noexcept
{
    jassert (! childName.isEmpty());

    for (auto* child = firstChildElement.get(); child != nullptr; child = child->nextListItem)
        if (child->hasTagName (childName))
            return child;

    return nullptr;
}

XmlElement* XmlElement::getChildByAttribute (StringRef attributeName, StringRef attributeValue) const noexcept
{
    jassert (! attributeName.isEmpty());

    for (auto* child = firstChildElement.get(); child != nullptr; child = child->nextListItem)
        if (child->compareAttribute (attributeName, attributeValue))
            return child;

    return nullptr;
}

void XmlElement::addChildElement (XmlElement* const newNode) noexcept
{
    if (newNode != nullptr)
    {
        // The element being added must not be a child of another node!
        jassert (newNode->nextListItem == nullptr);

        firstChildElement.append (newNode);
    }
}

void XmlElement::insertChildElement (XmlElement* const newNode, int indexToInsertAt) noexcept
{
    if (newNode != nullptr)
    {
        // The element being added must not be a child of another node!
        jassert (newNode->nextListItem == nullptr);

        firstChildElement.insertAtIndex (indexToInsertAt, newNode);
    }
}

void XmlElement::prependChildElement (XmlElement* newNode) noexcept
{
    if (newNode != nullptr)
    {
        // The element being added must not be a child of another node!
        jassert (newNode->nextListItem == nullptr);

        firstChildElement.insertNext (newNode);
    }
}

XmlElement* XmlElement::createNewChildElement (StringRef childTagName)
{
    auto newElement = new XmlElement (childTagName);
    addChildElement (newElement);
    return newElement;
}

bool XmlElement::replaceChildElement (XmlElement* const currentChildElement,
                                      XmlElement* const newNode) noexcept
{
    if (newNode != nullptr)
    {
        if (auto* p = firstChildElement.findPointerTo (currentChildElement))
        {
            if (currentChildElement != newNode)
                delete p->replaceNext (newNode);

            return true;
        }
    }

    return false;
}

void XmlElement::removeChildElement (XmlElement* const childToRemove,
                                     const bool shouldDeleteTheChild) noexcept
{
    if (childToRemove != nullptr)
    {
        jassert (containsChildElement (childToRemove));

        firstChildElement.remove (childToRemove);

        if (shouldDeleteTheChild)
            delete childToRemove;
    }
}

bool XmlElement::isEquivalentTo (const XmlElement* const other,
                                 const bool ignoreOrderOfAttributes) const noexcept
{
    if (this != other)
    {
        if (other == nullptr || tagName != other->tagName)
            return false;

        if (ignoreOrderOfAttributes)
        {
            int totalAtts = 0;

            for (auto* att = attributes.get(); att != nullptr; att = att->nextListItem)
            {
                if (! other->compareAttribute (att->attribute))
                    return false;

                ++totalAtts;
            }

            if (totalAtts != other->getNumAttributes())
                return false;
        }
        else
        {
            auto* thisAtt = attributes.get();
            auto* otherAtt = other->attributes.get();

            for (;;)
            {
                if (thisAtt == nullptr || otherAtt == nullptr)
                {
                    if (thisAtt == otherAtt) // both nullptr, so it's a match
                        break;

                    return false;
                }

                if (thisAtt->attribute != otherAtt->attribute)
                    return false;

                thisAtt = thisAtt->nextListItem;
                otherAtt = otherAtt->nextListItem;
            }
        }

        auto* thisChild = firstChildElement.get();
        auto* otherChild = other->firstChildElement.get();

        for (;;)
        {
            if (thisChild == nullptr || otherChild == nullptr)
            {
                if (thisChild == otherChild) // both 0, so it's a match
                    break;

                return false;
            }

            if (! thisChild->isEquivalentTo (otherChild, ignoreOrderOfAttributes))
                return false;

            thisChild = thisChild->nextListItem;
            otherChild = otherChild->nextListItem;
        }
    }

    return true;
}

void XmlElement::deleteAllChildElements() noexcept
{
    firstChildElement.deleteAll();
}

void XmlElement::deleteAllChildElementsWithTagName (StringRef name) noexcept
{
    for (auto* child = firstChildElement.get(); child != nullptr;)
    {
        auto* nextChild = child->nextListItem.get();

        if (child->hasTagName (name))
            removeChildElement (child, true);

        child = nextChild;
    }
}

bool XmlElement::containsChildElement (const XmlElement* const possibleChild) const noexcept
{
    return firstChildElement.contains (possibleChild);
}

XmlElement* XmlElement::findParentElementOf (const XmlElement* const elementToLookFor) noexcept
{
    if (this == elementToLookFor || elementToLookFor == nullptr)
        return nullptr;

    for (auto* child = firstChildElement.get(); child != nullptr; child = child->nextListItem)
    {
        if (elementToLookFor == child)
            return this;

        if (auto* found = child->findParentElementOf (elementToLookFor))
            return found;
    }

    return nullptr;
}

void XmlElement::getChildElementsAsArray (XmlElement** elems) const noexcept
{
    firstChildElement.copyToArray (elems);
}

void XmlElement::reorderChildElements (XmlElement** elems, int num) noexcept
{
    auto* e = elems[0];
    firstChildElement = e;

    for (int i = 1; i < num; ++i)
    {
        e->nextListItem = elems[i];
        e = e->nextListItem;
    }

    e->nextListItem = nullptr;
}

//==============================================================================
bool XmlElement::isTextElement() const noexcept
{
    return tagName.isEmpty();
}

static const String& getJuceXmlTextContentAttributeName()
{
    static String result { "text" };
    return result;
}

const String& XmlElement::getText() const noexcept
{
    jassert (isTextElement());  // you're trying to get the text from an element that
                                // isn't actually a text element.. If this contains text sub-nodes, you
                                // probably want to use getAllSubText instead.

    return getStringAttribute (getJuceXmlTextContentAttributeName());
}

void XmlElement::setText (const String& newText)
{
    if (isTextElement())
        setAttribute (getJuceXmlTextContentAttributeName(), newText);
    else
        jassertfalse; // you can only change the text in a text element, not a normal one.
}

String XmlElement::getAllSubText() const
{
    if (isTextElement())
        return getText();

    if (getNumChildElements() == 1)
        return firstChildElement.get()->getAllSubText();

    MemoryOutputStream mem (1024);

    for (auto* child = firstChildElement.get(); child != nullptr; child = child->nextListItem)
        mem << child->getAllSubText();

    return mem.toUTF8();
}

String XmlElement::getChildElementAllSubText (StringRef childTagName, const String& defaultReturnValue) const
{
    if (auto* child = getChildByName (childTagName))
        return child->getAllSubText();

    return defaultReturnValue;
}

XmlElement* XmlElement::createTextElement (const String& text)
{
    auto e = new XmlElement ((int) 0);
    e->setAttribute (getJuceXmlTextContentAttributeName(), text);
    return e;
}

bool XmlElement::isValidXmlName (StringRef text) noexcept
{
    if (text.isEmpty() || ! isValidXmlNameStartCharacter (text.text.getAndAdvance()))
        return false;

    for (;;)
    {
        if (text.isEmpty())
            return true;

        if (! isValidXmlNameBodyCharacter (text.text.getAndAdvance()))
            return false;
    }
}

void XmlElement::addTextElement (const String& text)
{
    addChildElement (createTextElement (text));
}

void XmlElement::deleteAllTextElements() noexcept
{
    for (auto* child = firstChildElement.get(); child != nullptr;)
    {
        auto* next = child->nextListItem.get();

        if (child->isTextElement())
            removeChildElement (child, true);

        child = next;
    }
}

//==============================================================================
//==============================================================================
#if JUCE_UNIT_TESTS

class XmlElementTests final : public UnitTest
{
public:
    XmlElementTests()
        : UnitTest ("XmlElement", UnitTestCategories::xml)
    {}

    void runTest() override
    {
        {
            beginTest ("Float formatting");

            auto element = std::make_unique<XmlElement> ("test");
            Identifier number ("number");

            std::map<double, String> tests;
            tests[1] = "1.0";
            tests[1.1] = "1.1";
            tests[1.01] = "1.01";
            tests[0.76378] = "0.76378";
            tests[-10] = "-10.0";
            tests[10.01] = "10.01";
            tests[0.0123] = "0.0123";
            tests[-3.7e-27] = "-3.7e-27";
            tests[1e+40] = "1.0e40";
            tests[-12345678901234567.0] = "-1.234567890123457e16";
            tests[192000] = "192000.0";
            tests[1234567] = "1.234567e6";
            tests[0.00006] = "0.00006";
            tests[0.000006] = "6.0e-6";

            for (auto& test : tests)
            {
                element->setAttribute (number, test.first);
                expectEquals (element->getStringAttribute (number), test.second);
            }
        }

        {
            beginTest ("Parser nesting depth limit");

            const auto nested = [] (int depth)
            {
                return String::repeatedString ("<a>", depth) + String::repeatedString ("</a>", depth);
            };

            XmlDocument atLimit (nested (XmlDocument::maxElementNestingDepth));
            expect (atLimit.getDocumentElement() != nullptr);
            expect (atLimit.getLastParseError().isEmpty());

            XmlDocument overLimit (nested (XmlDocument::maxElementNestingDepth + 1));
            expect (overLimit.getDocumentElement() == nullptr);
            expect (overLimit.getLastParseError().isNotEmpty());

            XmlDocument bomb (nested (100000));
            expect (bomb.getDocumentElement() == nullptr);
            expect (bomb.getLastParseError().isNotEmpty());

            // Entities are plain text and can neither produce elements nor refer to other entities, so
            // nesting cannot be reached through them: each of these declarations is rejected
            for (const auto* dtd : { "<!ENTITY e \"<a>&amp;e;</a>\">", "<!ENTITY e \"<a>&amp;t;</a>\"> <!ENTITY t 'x'>",
                                    "<!ENTITY e \"xx&e;\">" })
            {
                XmlDocument doc (String ("<!DOCTYPE a [ ") + dtd + " ]><a>&e;</a>");
                expect (doc.getDocumentElement() == nullptr, dtd);
                expectEquals (doc.getLastParseError(), String ("unsupported DTD declaration"), dtd);
            }

            // A long chain of entities, each referring to the previous one, is rejected at the first link
            {
                String dtd = " <!ENTITY e0 \"x\"> ";

                for (int i = 1; i < 200; ++i)
                    dtd << "<!ENTITY e" << i << " \"&e" << (i - 1) << ";\"> ";

                XmlDocument chain ("<!DOCTYPE a [" + dtd + "]><a>&e199;</a>");
                expect (chain.getDocumentElement() == nullptr);
                expectEquals (chain.getLastParseError(), String ("unsupported DTD declaration"));
            }

            // Replacements that keep producing '&' must make progress and finish promptly
            XmlDocument ampersands ("<!DOCTYPE a [ <!ENTITY e \"xx&amp;&amp;&amp;\"> ]><a>&e;</a>");
            auto parsed = ampersands.getDocumentElement();
            expect (parsed != nullptr && parsed->getAllSubText() == "xx&&&");

            // Exponential expansion ("billion laughs") is stopped by the expansion budget
            String laughs = " <!ENTITY l0 \"lol\"> ";

            for (int i = 1; i < 10; ++i)
                laughs << "<!ENTITY l" << i << " \"" << String::repeatedString ("&l" + String (i - 1) + ";", 10) << "\"> ";

            XmlDocument billionLaughs ("<!DOCTYPE a [" + laughs + "]><a>&l9;</a>");
            expect (billionLaughs.getDocumentElement() == nullptr);
            expectEquals (billionLaughs.getLastParseError(), String ("unsupported DTD declaration"));
        }

        {
            beginTest ("Entity buffers preserve native code units");

            const auto nativeBytes = [] (const String& value)
            {
                const auto start = value.getCharPointer();
                const auto end = start.findTerminatingNull();
                return String::toHexString (start.getAddress(), (int) getAddressDifference (end.getAddress(), start.getAddress()));
            };

            const auto character = String::charToString ((juce_wchar) 0x400000);
            const auto attribute = String ("x") + character + "y";
            const auto entity = String ("p") + character + "q&r";

           #if JUCE_STRING_UTF_TYPE == 8
            expectEquals (nativeBytes (attribute), String ("78 f0 80 80 80 79"));
            expectEquals (nativeBytes (entity), String ("70 f0 80 80 80 71 26 72"));
           #endif

            XmlDocument direct ("<a v='x&#x400000;y'>x&#x400000;y</a>");
            auto parsed = direct.getDocumentElement();
            expect (parsed != nullptr);

            if (parsed != nullptr)
            {
                expectEquals (nativeBytes (parsed->getStringAttribute ("v")), nativeBytes (attribute));
                MemoryOutputStream expectedText;
                expectedText << "x" << character << "y";
                auto* text = parsed->getFirstChildElement();
                expect (text != nullptr && text->isTextElement());

                if (text != nullptr)
                    expectEquals (nativeBytes (text->getText()), nativeBytes (String (expectedText.toUTF8())));
            }

            expect (direct.getLastParseError().isEmpty());

            XmlDocument indirect ("<!DOCTYPE a [ <!ENTITY e 'p&#x400000;q&amp;r'> ]><a v='&e;s'>&e;s</a>");
            parsed = indirect.getDocumentElement();
            expect (parsed != nullptr);

            if (parsed != nullptr)
            {
                expectEquals (nativeBytes (parsed->getStringAttribute ("v")), nativeBytes (entity + "s"));
                MemoryOutputStream expectedText;
                expectedText << entity << "s";
                auto* text = parsed->getFirstChildElement();
                expect (text != nullptr && text->isTextElement());

                if (text != nullptr)
                    expectEquals (nativeBytes (text->getText()), nativeBytes (String (expectedText.toUTF8())));
            }

            expect (indirect.getLastParseError().isEmpty());
        }

        {
            beginTest ("Entity values are decoded once and every expansion is charged");

            const auto references = String::repeatedString ("x&amp;", 16384);
            const auto expanded = String::repeatedString ("x&", 16384);

            for (const auto complete : { true, false })
            {
                const auto quoted = "'" + references + (complete ? "'" : "");
                XmlDocument attributeDoc (String{});
                attributeDoc.input = quoted.getCharPointer();
                String value;
                attributeDoc.readQuotedString (value);
                expectEquals (value, expanded);
                expectEquals (attributeDoc.getLastParseError(), complete ? String() : String ("unmatched quotes"));
            }

            // one declaration decodes to one million '&'; each reference to it is charged against the
            // expansion budget, so 8 references fit and 9 do not
            const auto value = String::repeatedString ("&amp;", 1000000);

            for (const auto* prefix : { "<a>", "<a v='" })
            {
                for (const auto count : { 8, 9 })
                {
                    const auto isText = String (prefix) == "<a>";
                    XmlDocument doc ("<!DOCTYPE a [ <!ENTITY e \"" + value + "\"> ]>" + prefix
                                     + String::repeatedString ("&e;", count) + (isText ? "</a>" : "'/>"));
                    auto parsed = doc.getDocumentElement();
                    expect ((parsed != nullptr) == (count == 8));

                    if (parsed != nullptr)
                    {
                        expectEquals (isText ? parsed->getAllSubText() : parsed->getStringAttribute ("v"),
                                      String::repeatedString ("&", 1000000 * count));
                        expect (doc.getLastParseError().isEmpty());
                    }
                    else
                    {
                        expectEquals (doc.getLastParseError(), String ("entity expansion too large"));
                    }
                }
            }
        }

        {
            beginTest ("Character references stop at the terminator");

            for (const auto* reference : { "&#", "&#x", "&#X", "&#12", "&#x41" })
            {
                for (const auto* prefix : { "<a>", "<a v='" })
                {
                    const auto text = String (prefix) + reference;
                    XmlDocument doc (text);
                    expect (doc.getDocumentElement() == nullptr);
                    expectEquals (doc.getLastParseError(), String (String (prefix) == "<a>" ? "unmatched tags" : "unmatched quotes"));
                    expect (doc.input == doc.originalText.getCharPointer() + text.length());

                    XmlDocument accepted (String (prefix) + "&#65;&#x42;" + (String (prefix) == "<a>" ? "</a>" : "'/>"));
                    auto parsed = accepted.getDocumentElement();
                    expect (parsed != nullptr);

                    if (parsed != nullptr)
                        expectEquals (String (prefix) == "<a>" ? parsed->getAllSubText() : parsed->getStringAttribute ("v"),
                                      String ("AB"));

                    expect (accepted.getLastParseError().isEmpty());
                }
            }
        }

        {
            beginTest ("Malformed references preserve lenient recovery");

            // Unknown entities and a zero character reference keep recovering as HEAD did.
            // References with no digits, or a non-digit before the terminator, used to be
            // accepted and silently changed or dropped content: they now fail the parse
            // (text == nullptr).
            struct Row
            {
                const char* reference;
                const char* text;
                const char* error;
            };

            for (const auto& row : { Row { "&#xZ;t", nullptr, "malformed character reference" },
                                    Row { "&bogus;t", "bogust", "unknown entity" },
                                    Row { "&#0;t", "t", "" },
                                    Row { "&#1Z;t", nullptr, "malformed character reference" },
                                    Row { "&#x;", nullptr, "malformed character reference" } })
            {
                for (const auto attribute : { false, true })
                {
                    XmlDocument doc (String (attribute ? "<a v='" : "<a>") + row.reference
                                     + (attribute ? "'/>" : "</a>"));
                    auto parsed = doc.getDocumentElement();
                    expect ((parsed != nullptr) == (row.text != nullptr));

                    if (parsed != nullptr)
                        expectEquals (attribute ? parsed->getStringAttribute ("v") : parsed->getAllSubText(),
                                      String (row.text));

                    expectEquals (doc.getLastParseError(), String (row.error));
                }
            }

            // A declared entity expands while an undeclared name stays lenient: it recovers as text and
            // leaves a non-fatal "unknown entity" error.
            XmlDocument recovered ("<!DOCTYPE a [ <!ENTITY e 'x&amp;&amp;'> ]><a v='&e;'>&bogus;t</a>");
            auto parsed = recovered.getDocumentElement();
            expect (parsed != nullptr && parsed->getStringAttribute ("v") == "x&&"
                                      && parsed->getAllSubText() == "bogust");
            expectEquals (recovered.getLastParseError(), String ("unknown entity"));
        }

        {
            beginTest ("Parsing stops after an expansion-limit failure");

            // nine references to a one-million character value exceed the budget; no later attribute is read
            const String suffix ("' x='1' y='2'/>");
            XmlDocument doc ("<!DOCTYPE a [ <!ENTITY e \"" + String::repeatedString ("&amp;", 1000000) + "\"> ]><a v='"
                             + String::repeatedString ("&e;", 9) + suffix);
            expect (doc.getDocumentElement() == nullptr);
            expect (doc.outOfData);
            expectEquals (String (doc.input), suffix);
            expectEquals (doc.getLastParseError(), String ("entity expansion too large"));
        }

        {
            beginTest ("Attribute parsing stops after fatal errors");

            // an unterminated entity reference is fatal, and no further attribute is allocated
            const String suffix ("bad x='1' y='2'/>");
            XmlDocument doc ("<a v='&" + suffix);
            expect (doc.getDocumentElement() == nullptr);
            expect (doc.outOfData);
            expectEquals (String (doc.input), suffix);
            expectEquals (doc.getLastParseError(), String ("unterminated entity reference"));

            XmlDocument accepted ("<a v='x' x='1' y='2'/>");
            auto parsed = accepted.getDocumentElement();
            expect (parsed != nullptr && parsed->getNumAttributes() == 3);
            expect (accepted.getLastParseError().isEmpty());
        }

        {
            beginTest ("DTD state is reset between parses");

            {
                XmlDocument doc (String{});
                const auto first = String ("<!DOCTYPE a [ <!ENTITY e 'OLD'> ]>")
                                 + String::repeatedString ("<a>", XmlDocument::maxElementNestingDepth)
                                 + "&e;<a/>"
                                 + String::repeatedString ("</a>", XmlDocument::maxElementNestingDepth);
                expect (doc.parseDocumentElement (first.getCharPointer(), false) == nullptr);
                expectEquals (doc.getLastParseError(), String ("elements nested too deeply"));

                const String missing = "<a>&e;</a>";
                auto parsed = doc.parseDocumentElement (missing.getCharPointer(), false);
                expect (parsed != nullptr && parsed->getAllSubText() == "e");
                expectEquals (doc.getLastParseError(), String ("unknown entity"));

                const String defined = "<!DOCTYPE a [ <!ENTITY e 'NEW'> ]><a>&e;</a>";
                parsed = doc.parseDocumentElement (defined.getCharPointer(), false);
                expect (parsed != nullptr && parsed->getAllSubText() == "NEW");
                expect (doc.getLastParseError().isEmpty());
            }
        }

        {
            beginTest ("Only elements consume the element depth allowance");

            for (const auto* filler : { "<!--c-->", "<?p?>", "<!--c--> <?p?> ", " " })
            {
                for (const auto extraElement : { false, true })
                {
                    XmlDocument doc (String::repeatedString ("<a>", XmlDocument::maxElementNestingDepth)
                                     + "<!--leading-->" + filler + (extraElement ? "<b/>" : "")
                                     + String::repeatedString ("</a>", XmlDocument::maxElementNestingDepth));
                    auto parsed = doc.getDocumentElement();
                    expect ((parsed == nullptr) == extraElement);
                    expectEquals (doc.getLastParseError(), extraElement ? String ("elements nested too deeply") : String());
                }
            }
        }

        {
            beginTest ("Processing instructions and comments in element content");

            // These used to spin forever (a PI followed by text) or are the neighbouring
            // cases that share the same code path. The PI/comment is skipped, text is kept.
            const auto check = [this] (const char* xml, const String& expectedText, int expectedChildren)
            {
                XmlDocument doc (xml);
                auto parsed = doc.getDocumentElement();
                expect (parsed != nullptr, xml);

                if (parsed != nullptr)
                {
                    expect (doc.getLastParseError().isEmpty(), xml);
                    expectEquals (parsed->getAllSubText(), expectedText);
                    expectEquals (parsed->getNumChildElements(), expectedChildren);
                }
            };

            check ("<a><?p?>t</a>",          "t",  1);
            check ("<a><?p?> t</a>",         " t", 1);
            check ("<a>x<?p?>y</a>",         "xy", 1);
            check ("<a>x<?p?> </a>",         "x ", 1);
            check ("<a>x<?p?><b/></a>",      "x",  2);
            check ("<a>x<?p?><!--c--> </a>", "x ", 1);
            check ("<a><!--c-->t</a>",       "t",  1);
            check ("<a>t<!--c-->u</a>",       "tu", 1);
            check ("<a><?p?><b/></a>",       "",   1);

            for (const auto ignoreEmpty : { true, false })
            {
                XmlDocument doc ("<!DOCTYPE a [ <!ENTITY e ''> ]><a>&e;<?p?> </a>");
                doc.setEmptyTextElementsIgnored (ignoreEmpty);
                auto parsed = doc.getDocumentElement();
                expect (parsed != nullptr && parsed->getAllSubText() == (ignoreEmpty ? "" : " "));
                expect (parsed != nullptr && parsed->getNumChildElements() == (ignoreEmpty ? 0 : 1));
                expect (doc.getLastParseError().isEmpty());
            }

            XmlDocument unterminated ("<a>t<?p</a>");
            expect (unterminated.getDocumentElement() == nullptr);
            expect (unterminated.getLastParseError().isNotEmpty());
        }

        {
            beginTest ("Malformed UTF-8 lead bytes never carry the parser past the end");

            // The parser counts characters with getAndAdvance(), which only consumes real
            // continuation bytes, and then replays that count with CharPointer_UTF8::operator+=,
            // which skips a malformed lead byte's full advertised width. Each input below ends
            // right at the buffer end, so a replay that crosses the terminator reads off the heap.
            struct Row
            {
                const char* name;
                std::vector<uint8> bytes;
                const char* expectedError; // nullptr when only completion is checked
            };

            const Row rows[]
            {
                { "comment",
                  { 0x3c, 0x72, 0x3e, 0x3c, 0x21, 0x2d, 0x2d, 0x20, 0x63, 0x6f, 0xb7, 0x3e, 0x36, 0xff,
                    0x6d, 0x6d, 0x65, 0x6e, 0x20, 0x2d, 0x2d, 0x3e },
                  "unmatched tags" },
                { "nested closing tags",
                  { 0x3c, 0x61, 0x3e, 0x3c, 0x62, 0x3e, 0x3c, 0x63, 0x3e, 0x3c, 0x64, 0x3e, 0x3c, 0x65, 0x3e,
                    0x64, 0x65, 0x65, 0x70, 0x3c, 0x2f, 0x64, 0x3e, 0x3c, 0x2f, 0x63, 0x3e, 0x3c, 0x2f, 0x62,
                    0x3e, 0x3c, 0x2f, 0x61, 0x99, 0x1d, 0xfe, 0x75, 0xe7, 0xdd, 0x3e },
                  "mismatched end tag" },
                { "processing instruction",
                  { 0x3c, 0x72, 0x3e, 0x3c, 0x3f, 0x78, 0x20, 0x63, 0x6f, 0xb7, 0x3e, 0x36, 0xff,
                    0x6d, 0x6d, 0x65, 0x6e, 0x20, 0x3f, 0x3e },
                  nullptr },
                { "closing tag",
                  { 0x3c, 0x61, 0x3e, 0x64, 0x3c, 0x2f, 0x61, 0x99, 0x1d, 0xfe, 0x75, 0xe7, 0xdd, 0x3e },
                  nullptr },
                { "named entity",
                  { 0x3c, 0x61, 0x3e, 0x26, 0x61, 0xff, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x3b },
                  nullptr }
            };

            for (const auto& row : rows)
            {
                auto raw = row.bytes;
                raw.push_back (0);

                // Build the String from the raw bytes so the malformed sequences survive intact
                const String text { CharPointer_UTF8 (reinterpret_cast<const char*> (raw.data())) };
                expectEquals ((int) text.getNumBytesAsUTF8(), (int) row.bytes.size(), row.name);

                XmlDocument doc (text);
                auto parsed = doc.getDocumentElement();
                const auto* end = text.getCharPointer().getAddress() + row.bytes.size();

                expect (doc.input.getAddress() <= end, row.name);
                expect (parsed != nullptr || doc.getLastParseError().isNotEmpty(), row.name);

                if (row.expectedError != nullptr)
                {
                    expect (parsed == nullptr, row.name);
                    expectEquals (doc.getLastParseError(), String (row.expectedError));
                }
            }

            // Direct pointer check: skipping must stop at the terminator even when a lead byte
            // advertises more continuation bytes than the string contains.
            {
                const char padded[] { 'a', (char) 0xff, 0, 'X', 'X', 'X', 'X', 'X', 'X', 'X' };
                CharPointer_UTF8 p (padded);
                p += 5;
                expect (p.getAddress() == padded + 2);
                expect (p.isEmpty());

                const char plain[] { 'a', 'b', 'c', 0 };
                CharPointer_UTF8 q (plain);
                q += 3;
                expect (q.getAddress() == plain + 3);
            }
        }

        {
            beginTest ("Single-line output with mixed content");

            // A text node used to reset the indentation to 0 for the elements after it, which
            // meant a null newline string was then written for any nested element.
            const String longValue ("0123456789012345678901234567890123456789");

            const String documents[]
            {
                "<a>x<b><c/></b></a>",
                "<a><b>x<c><d/></c></b></a>",
                "<a>x<b y=\"1\"><c/>t<d><e/></d></b>z</a>",
                "<a>x<b p=\"" + longValue + "\" q=\"" + longValue + "\" r=\"" + longValue + "\"><c u=\"" + longValue + "\" v=\"" + longValue + "\"/></b></a>",
                "<a>x<b>y<c>z<d>w<e/></d></c></b></a>",
                "<a><b/>x<c><d/></c></a>"
            };

            const String header ("<?xml version=\"1.0\" encoding=\"UTF-8\"?> ");

            JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS

            for (const auto& document : documents)
            {
                const auto parsed = parseXML (document);
                expect (parsed != nullptr, document);

                if (parsed == nullptr)
                    continue;

                const auto check = [this, &document] (const String& result, const String& expected, const char* api)
                {
                    expectEquals (result, expected);
                    expect (! result.containsAnyOf ("\r\n"), String (api) + ": " + document);
                };

                for (const auto lineWrapLength : { 60, 5, 0 })
                {
                    auto format = XmlElement::TextFormat().singleLine().withoutHeader();
                    format.lineWrapLength = lineWrapLength;

                    auto withHeader = XmlElement::TextFormat().singleLine();
                    withHeader.lineWrapLength = lineWrapLength;

                    check (parsed->toString (format), document, "toString");
                    check (parsed->toString (withHeader), header + document, "toString with header");

                    {
                        MemoryOutputStream out;
                        parsed->writeTo (out, format);
                        check (out.toString(), document, "writeTo");
                    }

                    {
                        MemoryOutputStream out;
                        parsed->writeTo (out, withHeader);
                        check (out.toString(), header + document, "writeTo with header");
                    }

                    check (parsed->createDocument ({}, true, false, "UTF-8", lineWrapLength), document, "createDocument");
                    check (parsed->createDocument ({}, true, true, "UTF-8", lineWrapLength), header + document, "createDocument with header");

                    {
                        MemoryOutputStream out;
                        parsed->writeToStream (out, {}, true, false, "UTF-8", lineWrapLength);
                        check (out.toString(), document, "writeToStream");
                    }

                    {
                        MemoryOutputStream out;
                        parsed->writeToStream (out, {}, true, true, "UTF-8", lineWrapLength);
                        check (out.toString(), header + document, "writeToStream with header");
                    }
                }
            }

            JUCE_END_IGNORE_DEPRECATION_WARNINGS

            // The multi-line layout must not have changed
            {
                const auto parsed = parseXML ("<a>x<b><c/></b></a>");
                expect (parsed != nullptr);

                if (parsed != nullptr)
                {
                    auto format = XmlElement::TextFormat().withoutHeader();
                    format.newLineChars = "\n";
                    expectEquals (parsed->toString (format), String ("<a>x<b>\n  <c/>\n</b>\n</a>\n"));
                }
            }
        }

        {
            const auto expectRejected = [this] (const String& text, const String& error)
            {
                XmlDocument doc (text);
                expect (doc.getDocumentElement() == nullptr, text);
                expectEquals (doc.getLastParseError(), error, text);
            };

            // a malformed input paired with its own well-formed neighbour; the neighbour's FULL tree is
            // checked through its single-line, header-less serialisation
            const auto expectPair = [this, &expectRejected] (const String& bad, const String& error,
                                                              const String& good, const String& goodTree)
            {
                expectRejected (bad, error);
                XmlDocument doc (good);
                auto parsed = doc.getDocumentElement();
                expect (parsed != nullptr, good);
                expect (doc.getLastParseError().isEmpty(), good);

                if (parsed != nullptr)
                    expectEquals (parsed->toString (XmlElement::TextFormat().singleLine().withoutHeader()), goodTree, good);
            };

            // every malformed input below is paired with its own well-formed neighbour
            struct Pair { const char* bad; const char* error; const char* good; const char* goodTree; };

            const auto runPairs = [&expectPair] (std::initializer_list<Pair> pairs)
            {
                for (const auto& pair : pairs)
                    expectPair (pair.bad, pair.error, pair.good, pair.goodTree);
            };

            beginTest ("Unquoted attribute values are rejected");

            runPairs ({ { "<a x=1 y=\"2\"><b/></a>", "unquoted attribute value", "<a x=\"1\" y=\"2\"><b/></a>", "<a x=\"1\" y=\"2\"><b/></a>" },
                        { "<a x=>",                  "unquoted attribute value", "<a x=\"\"/>",                  "<a x=\"\"/>" },
                        { "<a x=1/>",                "unquoted attribute value", "<a x=\"1\"/>",                 "<a x=\"1\"/>" },
                        { "<a y=\"2\" x=1/>",        "unquoted attribute value", "<a y=\"2\" x=\"1\"/>",         "<a y=\"2\" x=\"1\"/>" },
                        { "<a x= 1/>",               "unquoted attribute value", "<a x= \"1\"/>",                "<a x=\"1\"/>" },
                        { "<a x=",                   "unquoted attribute value", "<a x=\"1\"/>",                 "<a x=\"1\"/>" } });

            {
                auto good = XmlDocument::parse ("<a x=\"1\" y='2'><b/></a>");
                expect (good != nullptr);

                if (good != nullptr)
                {
                    expectEquals (good->getStringAttribute ("x"), String ("1"));
                    expectEquals (good->getStringAttribute ("y"), String ("2"));
                    expectEquals (good->getNumChildElements(), 1);
                }
            }

            beginTest ("Content after the root element is rejected");

            runPairs ({ { "<a/><b>data</b>",               "content after the root element", "<a/><!-- <b>data</b> -->",       "<a/>" },
                        { "<a/>junk",                      "content after the root element", "<a/> <!-- junk -->",             "<a/>" },
                        { "<a></a>x",                      "content after the root element", "<a></a>\n",                      "<a/>" },
                        { "<a/><b/>",                      "content after the root element", "<a/><?b?>",                      "<a/>" },
                        { "<a/>&amp;",                     "content after the root element", "<a/>\r\n",                       "<a/>" },
                        { "<a/><!-- unterminated",         "content after the root element", "<a/><!-- terminated -->",        "<a/>" },
                        { "<a/><?pi unterminated",         "content after the root element", "<a/><?pi terminated?>",          "<a/>" },
                        { "<a/> <!-- c --> <?p x?> <b/>",  "content after the root element", "<a/> <!-- c --> <?p x?> ",       "<a/>" },
                        { "<a/>\n<!-- c -->\n</a>",        "content after the root element", "<a/>\n<!-- c -->\n",             "<a/>" } });

            for (const auto* text : { "<a x='1'><b/></a>\n", "<a x='1'><b/></a>\r\n <!-- c --> <?p x?> \n",
                                      "<?xml version=\"1.0\"?><!-- lead --><a x='1'><b/></a><!-- trail -->" })
            {
                XmlDocument doc (text);
                auto good = doc.getDocumentElement();
                expect (good != nullptr, text);
                expect (doc.getLastParseError().isEmpty(), text);

                if (good != nullptr)
                {
                    expectEquals (good->getStringAttribute ("x"), String ("1"));
                    expectEquals (good->getNumChildElements(), 1);
                }
            }

            {
                // The outer-element-only mode never looks past the root start tag
                XmlDocument outer ("<a/>junk");
                expect (outer.getDocumentElement (true) != nullptr);
                expect (outer.getLastParseError().isEmpty());
            }

            beginTest ("End tags are checked");

            runPairs ({ { "<a></a <b/></a>",   "malformed end tag",  "<a></a>",             "<a/>"     },
                        { "<a></>",            "malformed end tag",  "<a></a>",             "<a/>"     },
                        { "<a></a",            "malformed end tag",  "<a></a>",             "<a/>"     },
                        { "<a></ a>",          "malformed end tag",  "<a></a >",            "<a/>"     },
                        { "<a></a b>",         "malformed end tag",  "<a></a >",            "<a/>"     },
                        { "<a><b></a></b>",    "mismatched end tag", "<a><b></b></a>",      "<a><b/></a>"     },
                        { "<a>text</wrong>",   "mismatched end tag", "<a>text</a>",         "<a>text</a>" },
                        { "<a x='1'></b>",     "mismatched end tag", "<a x='1'></a>",       "<a x=\"1\"/>"     } });

            for (const auto* text : { "<a>t</a >", "<ns:a>t</ns:a>", "<a>t</a\n>" })
            {
                auto good = XmlDocument::parse (text);
                expect (good != nullptr, text);

                if (good != nullptr)
                    expectEquals (good->getAllSubText(), String ("t"));
            }

            beginTest ("Missing terminators are errors");

            runPairs ({ { "<a x=\"&\" y=\"2\"><b/></a>", "unterminated entity reference",        "<a x=\"&amp;\" y=\"2\"><b/></a>", "<a x=\"&amp;\" y=\"2\"><b/></a>"  },
                        { "<a>&amp</a>",                 "unterminated entity reference",        "<a>&amp;</a>",                    "<a>&amp;</a>" },
                        { "<a <!-- <b/></a>",            "unterminated comment",                 "<a <!-- <b/> --> ></a>",          "<a/>"  },
                        { "<a>t<!-- x</a>",              "unterminated comment",                 "<a>t<!-- x --></a>",              "<a>t</a>" },
                        { "<a><b><!-- x</b></a>",        "unterminated comment",                 "<a><b><!-- x --></b></a>",        "<a><b/></a>"  },
                        { "<a <? x></a>",                "unterminated processing instruction",  "<a <? x ?>></a>",                 "<a/>"  },
                        { "<a><?p </a>",                 "unterminated processing instruction",  "<a><?p ?></a>",                   "<a/>"  },
                        { "<!-- x",                      "unterminated comment",                 "<!-- x --><a/>",                  "<a/>"  } });

            {
                auto good = XmlDocument::parse ("<a x=\"&amp;\" y=\"2\"><!-- c --><?p x?><b/></a>");
                expect (good != nullptr);

                if (good != nullptr)
                {
                    expectEquals (good->getStringAttribute ("x"), String ("&"));
                    expectEquals (good->getStringAttribute ("y"), String ("2"));
                    expectEquals (good->getNumChildElements(), 1);
                }

                auto commentInTag = XmlDocument::parse ("<a <!-- c --> x=\"1\"/>");
                expect (commentInTag != nullptr && commentInTag->getStringAttribute ("x") == "1");
            }

            beginTest ("Malformed character references are errors");

            runPairs ({ { "<a>&#x;</a>",     "malformed character reference", "<a>&#x41;</a>",   "<a>A</a>" },
                        { "<a>&#xZ;</a>",    "malformed character reference", "<a>&#x5A;</a>",   "<a>Z</a>" },
                        { "<a>&#1Z;</a>",    "malformed character reference", "<a>&#90;</a>",    "<a>Z</a>" },
                        { "<a>&#;</a>",      "malformed character reference", "<a>&#59;</a>",    "<a>;</a>" },
                        { "<a>&#Zq;</a>",    "malformed character reference", "<a>&#90;q</a>",   "<a>Zq</a>" },
                        { "<a v=\"&#x;\"/>", "malformed character reference", "<a v=\"&#x41;\"/>", "<a v=\"A\"/>" },
                        { "<a v=\"&#12a;\"/>", "malformed character reference", "<a v=\"&#12;\"/>", "<a v=\"&#12;\"/>" },
                        { "<a v=\"&#;\"/>",  "malformed character reference", "<a v=\"&#59;\"/>", "<a v=\";\"/>" },
                        { "<a>&#x4G;</a>",   "malformed character reference", "<a>&#x4F;</a>",   "<a>O</a>" } });

            {
                // Writers emit numeric references for control characters; they must still parse
                XmlDocument doc ("<a v='&#65;&#x42;&#x1f;&#31;'>&#65;&#x42;&#1;&#x1F;</a>");
                auto good = doc.getDocumentElement();
                expect (good != nullptr);
                expect (doc.getLastParseError().isEmpty());

                if (good != nullptr)
                {
                    expectEquals (good->getStringAttribute ("v"),
                                  "AB" + String::charToString (0x1f) + String::charToString (0x1f));
                    expectEquals (good->getAllSubText(),
                                  "AB" + String::charToString (1) + String::charToString (0x1f));
                }
            }

            beginTest ("Over-long character references and sticky errors");

            // more than 8 significant hex / 12 significant decimal digits used to be accepted with a skipped character
            runPairs ({ { "<a>&#x000000000Z;</a>",      "malformed character reference", "<a>&#x000000041;</a>",     "<a>A</a>" },
                        { "<a>&#x123456789;</a>",       "malformed character reference", "<a>&#x00000041;</a>",      "<a>A</a>" },
                        { "<a>&#1234567890123;</a>",    "malformed character reference", "<a>&#000000000065;</a>",   "<a>A</a>" },
                        { "<a>&#0000000000065Z;</a>",   "malformed character reference", "<a>&#0000000000065;</a>",  "<a>A</a>" },
                        { "<a v=\"&#x000000000Z;\"/>",   "malformed character reference", "<a v=\"&#x000000041;\"/>",  "<a v=\"A\"/>" },
                        { "<a v=\"&#1234567890123;\"/>", "malformed character reference", "<a v=\"&#65;\"/>",          "<a v=\"A\"/>" } });

            for (const auto* text : { "<a>&#1;&#31;&#x1F;</a>" })
            {
                auto good = XmlDocument::parse (text);
                expect (good != nullptr, text);
            }

            // leading zeros are valid XML and do not count towards the digit limit
            for (const auto* text : { "<a>&#x0000000041;</a>", "<a>&#00000000000065;</a>", "<a v=\"&#x000000000041;\"/>" })
            {
                auto good = XmlDocument::parse (text);
                expect (good != nullptr, text);

                if (good != nullptr)
                    expectEquals (good->hasAttribute ("v") ? good->getStringAttribute ("v") : good->getAllSubText(), String ("A"));
            }

            // a recorded error is not replaced by the header / DTD error that follows it
            runPairs ({ { "<?<?xml",              "unterminated processing instruction", "<?pi?><a/>",                  "<a/>" },
                        { "<!--<!DOCTYPE a><a/>", "unterminated comment",                "<!-- c --><!DOCTYPE a><a/>",    "<a/>" } });

            {
                auto good = XmlDocument::parse ("<?pi?><!DOCTYPE a><a/>");
                expect (good != nullptr);
            }

            beginTest ("Trailing content can be ignored on request");

            {
                // strict by default (rejected), tolerant when asked (the real root, exactly); a stale tail that
                // repeats the old root close must leave the real root intact
                struct Row { const char* text; const char* root; };

                for (const auto& row : { Row { "<a x='1'><b/></a>leftover</a>", "<a x=\"1\"><b/></a>" },
                                         Row { "<a x='1'><b/></a></b></a>",     "<a x=\"1\"><b/></a>" },
                                         Row { "<a x='1'><b/></a>junk",         "<a x=\"1\"><b/></a>" },
                                         Row { "<a x='1'><b/></a><c/>",         "<a x=\"1\"><b/></a>" },
                                         Row { "<a x=\"1\"/>junk",              "<a x=\"1\"/>" },
                                         Row { "<a></a>junk",                   "<a/>" },
                                         Row { "<a><!-- x --></a>junk",         "<a/>" } })
                {
                    expectRejected (row.text, "content after the root element");

                    XmlDocument tolerant (row.text);
                    tolerant.setTrailingContentIgnored (true);
                    auto parsed = tolerant.getDocumentElement();
                    expect (parsed != nullptr, row.text);
                    expect (tolerant.getLastParseError().isEmpty(), row.text);

                    if (parsed != nullptr)
                        expectEquals (parsed->toString (XmlElement::TextFormat().singleLine().withoutHeader()),
                                      String (row.root), row.text);
                }

                // the opt-in relaxes nothing inside the root element: each malformed input keeps its error
                // and its own well-formed twin (with the same tail) parses to the exact root
                struct Strict { const char* bad; const char* error; const char* good; const char* root; };

                for (const auto& row : { Strict { "<a x=1/>junk",           "unquoted attribute value", "<a x=\"1\"/>junk",        "<a x=\"1\"/>" },
                                         Strict { "<a></b>junk",            "mismatched end tag",       "<a></a>junk",             "<a/>" },
                                         Strict { "<a><!-- x</a>junk",      "unterminated comment",     "<a><!-- x --></a>junk",   "<a/>" } })
                {
                    XmlDocument tolerant (row.bad);
                    tolerant.setTrailingContentIgnored (true);
                    expect (tolerant.getDocumentElement() == nullptr, row.bad);
                    expectEquals (tolerant.getLastParseError(), String (row.error), row.bad);

                    XmlDocument twin (row.good);
                    twin.setTrailingContentIgnored (true);
                    auto parsed = twin.getDocumentElement();
                    expect (parsed != nullptr, row.good);

                    if (parsed != nullptr)
                        expectEquals (parsed->toString (XmlElement::TextFormat().singleLine().withoutHeader()),
                                      String (row.root), row.good);
                }

                XmlDocument unset ("<a/>junk");
                unset.setTrailingContentIgnored (true);
                unset.setTrailingContentIgnored (false);
                expect (unset.getDocumentElement() == nullptr);
                expectEquals (unset.getLastParseError(), String ("content after the root element"));
            }

            const auto expectTree = [this] (const String& text, const String& tree)
            {
                XmlDocument doc (text);
                auto parsed = doc.getDocumentElement();
                expect (parsed != nullptr, text);
                expect (doc.getLastParseError().isEmpty(), text);

                if (parsed != nullptr)
                    expectEquals (parsed->toString (XmlElement::TextFormat().singleLine().withoutHeader()), tree, text);
            };

            beginTest ("DTD R1: the DOCTYPE is read with a real scanner");

            runPairs ({ { "<!DOCTYPE a [><a/>",                       "malformed DTD", "<!DOCTYPE a []><a/>",                        "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY e \"x>]><a/>",        "malformed DTD", "<!DOCTYPE a [<!ENTITY e \"x\">]><a/>",        "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY e 'x>]><a/>",         "malformed DTD", "<!DOCTYPE a [<!ENTITY e 'x'>]><a/>",          "<a/>" },
                        { "<!DOCTYPE a [<!-- c ]><a/>",                "malformed DTD", "<!DOCTYPE a [<!-- c -->]><a/>",               "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY e \"x\">",            "malformed DTD", "<!DOCTYPE a [<!ENTITY e \"x\">]><a/>",        "<a/>" },
                        { "<!DOCTYPE a",                               "malformed DTD", "<!DOCTYPE a><a/>",                            "<a/>" },
                        { "<!DOCTYPE a SYSTEM \"x.dtd",                "malformed DTD", "<!DOCTYPE a SYSTEM \"x.dtd\"><a/>",           "<a/>" },
                        { "<!DOCTYPE a PUBLIC \"p\"><a/>",             "malformed DTD", "<!DOCTYPE a PUBLIC \"p\" \"s\"><a/>",         "<a/>" },
                        { "<!DOCTYPEa><a/>",                           "malformed DTD", "<!DOCTYPE a><a/>",                            "<a/>" },
                        { "<!DOCTYPE a [ junk ]><a/>",                 "malformed DTD", "<!DOCTYPE a [ ]><a/>",                        "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY e \"x\"]><a/>",       "malformed DTD", "<!DOCTYPE a [<!ENTITY e \"x\">]><a/>",        "<a/>" } });

            // a '>' in a quoted value or a comment, and an apostrophe in a comment, do not end or derail the DOCTYPE
            expectTree ("<!DOCTYPE a [<!ENTITY e \"x > y\">]><a>&e;</a>", "<a>x &gt; y</a>");
            expectTree ("<!DOCTYPE a [<!-- > --><!ENTITY e \"x\">]><a>&e;</a>", "<a>x</a>");
            expectTree ("<!DOCTYPE a [<!-- it's --><!ENTITY e \"yes\">]><a>&e;</a>", "<a>yes</a>");
            expectTree ("<!DOCTYPE html><html/>", "<html/>");

            beginTest ("DTD R2: only simple internal text entities are supported");

            runPairs ({ { "<!DOCTYPE a [<!ELEMENT a EMPTY><!ATTLIST a x CDATA \"d\">]><a/>",   "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ELEMENT a EMPTY><!ATTLIST a x CDATA #IMPLIED>]><a x=\"1\"/>", "<a x=\"1\"/>" },
                        { "<!DOCTYPE a [<!ATTLIST a x CDATA #FIXED \"d\">]><a x=\"d\"/>",       "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ATTLIST a x CDATA #REQUIRED>]><a x=\"d\"/>",          "<a x=\"d\"/>" },
                        { "<!DOCTYPE a [<!ENTITY % p \"x\"> %p;]><a/>",                        "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY p \"x\">]><a>&p;</a>",                         "<a>x</a>" },
                        { "<!DOCTYPE a [%p;]><a/>",                                            "unsupported DTD declaration",
                          "<!DOCTYPE a []><a/>",                                                "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY % d \"<!ATTLIST a x CDATA 'v'>\"> %d;]><a/>",  "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ELEMENT a EMPTY>]><a/>",                              "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY % p SYSTEM \"x.dtd\"> %p;]><a/>",              "unsupported DTD declaration",
                          "<!DOCTYPE a [<!NOTATION n SYSTEM \"x\">]><a/>",                      "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY % p PUBLIC \"i\" \"x.dtd\"> %p;]><a/>",        "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ELEMENT a (b|c)*>]><a/>",                             "<a/>" },
                        { "<!DOCTYPE a [<?p <!ENTITY e \"BAD\"> ?> <!ENTITY e \"GOOD\">]><a>&e;</a>", "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"GOOD\">]><a>&e;</a>",                      "<a>GOOD</a>" },
                        { "<!DOCTYPE a [<![INCLUDE[<!ENTITY e \"x\">]]>]><a/>",                 "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"x\">]><a/>",                               "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY e SYSTEM \"e.xml\">]><a>x</a>",                "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"e\">]><a>x</a>",                           "<a>x</a>" },
                        { "<!DOCTYPE a [<!ENTITY e PUBLIC \"i\" \"e.xml\">]><a v=\"&e;\"/>",    "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"e\">]><a v=\"&e;\"/>",                     "<a v=\"e\"/>" },
                        { "<!DOCTYPE a [<!ENTITY e \"x\" NDATA n>]><a/>",                       "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"x\">]><a/>",                               "<a/>" },
                        { "<!DOCTYPE a [<!FOO>]><a/>",                                          "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ELEMENT a ANY>]><a/>",                                "<a/>" } });

            // glued declarations, single quotes and ordinary entities in text and attributes
            expectTree ("<!DOCTYPE a [<!ENTITY t \"text\"><!ENTITY v \"val\">]><a x=\"&v;\">&t;</a>", "<a x=\"val\">text</a>");
            expectTree ("<!DOCTYPE a [<!ELEMENT a ANY><!ENTITY t 'two words'><!ATTLIST a x CDATA #IMPLIED>]><a x=\"&t;\">&t;</a>",
                        "<a x=\"two words\">two words</a>");

            beginTest ("DTD R3: an entity value is text only");

            runPairs ({ { "<!DOCTYPE a [<!ENTITY e \"<b/>\">]><a>&e;</a>",                         "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"b\">]><a>&e;</a>",                          "<a>b</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"<b/>lost<c/>\">]><a>&e;</a>",                "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"lost\">]><a>&e;</a>",                       "<a>lost</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"<b/>\">]><a>before&e;after</a>",             "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"-\">]><a>before&e;after</a>",               "<a>before-after</a>" },
                        { "<!DOCTYPE a [<!ENTITY e2 \"b\"> <!ENTITY e \"a&e2;\">]><a>&e;</a>",    "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e2 \"b\"> <!ENTITY e \"a&amp;e2;\">]><a>&e;</a>", "<a>a&amp;e2;</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"x&amp\">]><a>&e;</a>",                      "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"x&amp;\">]><a>&e;</a>",                     "<a>x&amp;</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"x&foo;\">]><a/>",                          "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"x&apos;\">]><a/>",                          "<a/>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#x;\">]><a>&e;</a>",                      "malformed character reference",
                          "<!DOCTYPE a [<!ENTITY e \"&#x41;\">]><a>&e;</a>",                     "<a>A</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#1Z;\">]><a>&e;</a>",                     "malformed character reference",
                          "<!DOCTYPE a [<!ENTITY e \"&#65;\">]><a>&e;</a>",                      "<a>A</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#;\">]><a>&e;</a>",                       "malformed character reference",
                          "<!DOCTYPE a [<!ENTITY e \"&#59;\">]><a>&e;</a>",                      "<a>;</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#x123456789;\">]><a>&e;</a>",             "malformed character reference",
                          "<!DOCTYPE a [<!ENTITY e \"&#x0000000041;\">]><a>&e;</a>",             "<a>A</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#1234567890123;\">]><a v=\"&e;\"/>",      "malformed character reference",
                          "<!DOCTYPE a [<!ENTITY e \"&#00000000000065;\">]><a v=\"&e;\"/>",      "<a v=\"A\"/>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#12\">]><a/>",                            "malformed character reference",
                          "<!DOCTYPE a [<!ENTITY e \"&#12;\">]><a/>",                            "<a/>" } });

            // escaped markup stays TEXT, the value is decoded once, quotes of the other kind are plain characters,
            // an empty value is allowed, and the first declaration of a name wins
            expectTree ("<!DOCTYPE a [<!ENTITY e \"&lt;b/&gt;\">]><a>&e;</a>", "<a>&lt;b/&gt;</a>");
            expectTree ("<!DOCTYPE a [<!ENTITY e \"&amp;lt;\">]><a>&e;</a>", "<a>&amp;lt;</a>");
            expectTree ("<!DOCTYPE a [<!ENTITY e 'say \"hi\"'>]><a>&e;</a>", "<a>say &quot;hi&quot;</a>");
            expectTree ("<!DOCTYPE a [<!ENTITY e \"it's\">]><a>&e;</a>", "<a>it's</a>");
            expectTree ("<!DOCTYPE a [<!ENTITY e \"\">]><a>x&e;y</a>", "<a>xy</a>");
            expectTree ("<!DOCTYPE a [<!ENTITY e \"first\"> <!ENTITY e \"second\">]><a>&e;</a>", "<a>first</a>");
            expectTree ("<!DOCTYPE a [<!ENTITY e \"m\">]><a>x&e;y</a>", "<a>xmy</a>");

            beginTest ("DTD R3: references that would be markup, and parameter references, are unsupported");

            runPairs ({ { "<!DOCTYPE a [<!ENTITY e \"&#60;b/&gt;\">]><a>&e;</a>",   "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"&#62;b/&gt;\">]><a>&e;</a>",    "<a>&gt;b/&gt;</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#x3C;b/&gt;\">]><a>&e;</a>",  "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"&#x3E;b/&gt;\">]><a>&e;</a>",   "<a>&gt;b/&gt;</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#60;\">]><a v=\"&e;\"/>",    "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"&#62;\">]><a v=\"&e;\"/>",     "<a v=\"&gt;\"/>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#38;lt;\">]><a>&e;</a>",      "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"&amp;lt;\">]><a>&e;</a>",       "<a>&amp;lt;</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#x26;\">]><a v=\"&e;\"/>",   "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"&amp;\">]><a v=\"&e;\"/>",     "<a v=\"&amp;\"/>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#4294967356;b/&gt;\">]><a>&e;</a>", "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"&#4294967358;b/&gt;\">]><a>&e;</a>",   "<a>&gt;b/&gt;</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"&#4294967334;\">]><a v=\"&e;\"/>",   "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"&#4294967335;\">]><a v=\"&e;\"/>",   "<a v=\"'\"/>" },
                        { "<!DOCTYPE a [<!ENTITY e '%p;'>]><a>&e;</a>",                 "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e 'p'>]><a>&e;</a>",                   "<a>p</a>" },
                        { "<!DOCTYPE a [<!ENTITY e \"a%p;b\">]><a v=\"&e;\"/>",      "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"apb\">]><a v=\"&e;\"/>",        "<a v=\"apb\"/>" },
                        { "<!DOCTYPE a [<!ENTITY e %p;>]><a/>",                         "unsupported DTD declaration",
                          "<!DOCTYPE a [<!ENTITY e \"p\">]><a/>",                      "<a/>" },
                        { "<!DOCTYPE a [<!ELEMENT a (it's)>]><a/>",                     "malformed DTD",
                          "<!DOCTYPE a [<!ELEMENT a (its)>]><a/>",                      "<a/>" },
                        { "<!DOCTYPE a [<!ELEMENT a <!ENTITY e 'y' >]><a/>",            "malformed DTD",
                          "<!DOCTYPE a [<!ELEMENT a ANY>]><a/>",                        "<a/>" },
                        { "<!DOCTYPE a [<!ATTLIST a x <b CDATA #IMPLIED>]><a/>",        "malformed DTD",
                          "<!DOCTYPE a [<!ATTLIST a x CDATA #IMPLIED>]><a/>",           "<a/>" } });

            beginTest ("DTD R4: undeclared names and external identifiers");

            runPairs ({ { "<!DOCTYPE a PUBLIC \"p\" \"s\"><a>&e;</a>",                              "unsupported DTD declaration",
                          "<!DOCTYPE a PUBLIC \"p\" \"s\" [<!ENTITY e \"x\">]><a>&e;</a>",         "<a>x</a>" },
                        { "<!DOCTYPE a SYSTEM \"x.dtd\"><a v=\"&e;\"/>",                           "unsupported DTD declaration",
                          "<!DOCTYPE a SYSTEM \"x.dtd\" [<!ENTITY e \"x\">]><a v=\"&e;\"/>",       "<a v=\"x\"/>" } });

            {
                // without an external identifier an undeclared name keeps JUCE's lenient behaviour
                XmlDocument lenient ("<!DOCTYPE a><a>&e;</a>");
                auto parsed = lenient.getDocumentElement();
                expect (parsed != nullptr && parsed->getAllSubText() == "e");
                expectEquals (lenient.getLastParseError(), String ("unknown entity"));
            }

            beginTest ("DTD R5/R6: external DTDs and entities are never read");

            {
                struct CountingSource final : InputSource
                {
                    CountingSource (String t, int& d, int& e) : text (std::move (t)), documentReads (d), externalReads (e) {}

                    InputStream* createInputStream() override
                    {
                        ++documentReads;
                        return new MemoryInputStream (text.toRawUTF8(), text.getNumBytesAsUTF8(), true);
                    }

                    InputStream* createInputStreamFor (const String&) override   { ++externalReads; return nullptr; }
                    int64 hashCode() const override                              { return 0; }

                    String text;
                    int& documentReads;
                    int& externalReads;
                };

                const String svg ("<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
                                  "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\"><svg width=\"1\"><rect/></svg>");
                const String systemDoctype ("<!DOCTYPE a SYSTEM \"x.dtd\"><a/>");

                for (const auto& text : { svg, systemDoctype })
                {
                    // from a String the document itself is never read from the source; from a File it is read once
                    for (const auto fromFile : { false, true })
                    {
                        int documentReads = 0, externalReads = 0;
                        std::unique_ptr<TemporaryFile> temp;

                        if (fromFile)
                        {
                            temp = std::make_unique<TemporaryFile> (".xml");
                            expect (temp->getFile().replaceWithText (text));
                        }

                        auto doc = fromFile ? std::make_unique<XmlDocument> (temp->getFile())
                                            : std::make_unique<XmlDocument> (text);
                        doc->setInputSource (new CountingSource (text, documentReads, externalReads));
                        auto parsed = doc->getDocumentElement();
                        expect (parsed != nullptr, text);
                        expect (doc->getLastParseError().isEmpty(), text);
                        expectEquals (externalReads, 0, text);
                        expectEquals (documentReads, fromFile ? 1 : 0, text);
                    }
                }

                expectTree (svg, "<svg width=\"1\"><rect/></svg>");

                // Illustrator-style internal subset of simple text entities used in attributes
                expectTree ("<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
                            "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" [ "
                            "<!ENTITY ns_svg \"http://www.w3.org/2000/svg\"> "
                            "<!ENTITY ns_xlink \"http://www.w3.org/1999/xlink\"> ]>"
                            "<svg xmlns=\"&ns_svg;\" xmlns:xlink=\"&ns_xlink;\"/>",
                            "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\"/>");

                // a real file next to the document that declares the entity is not fetched; everything lives in a
                // unique directory created (and removed) by this test
                {
                    struct DirectoryGuard
                    {
                        explicit DirectoryGuard (File f) : directory (std::move (f)) {}
                        ~DirectoryGuard()   { directory.deleteRecursively(); }
                        File directory;
                    };

                    const DirectoryGuard guard (File::createTempFile ("juce_xml_dtd_test"));
                    const auto& directory = guard.directory;
                    expect (directory.createDirectory().wasOk());
                    const auto dtdFile = directory.getChildFile ("external.dtd");
                    const auto docFile = directory.getChildFile ("document.xml");
                    expect (dtdFile.replaceWithText ("<!ENTITY e 'FROM-FILE'>"));
                    expect (docFile.replaceWithText ("<!DOCTYPE a SYSTEM \"" + dtdFile.getFileName() + "\"><a>&e;</a>"));
                    XmlDocument external (docFile);
                    expect (external.getDocumentElement() == nullptr);
                    expectEquals (external.getLastParseError(), String ("unsupported DTD declaration"));
                }

                // the hostile fixtures of the removed parameter-entity / external-read features are rejected at once,
                // with no hang and no external read
                {
                    int documentReads = 0, externalReads = 0;
                    const auto reject = [&] (const String& text)
                    {
                        XmlDocument doc (text);
                        doc.setInputSource (new CountingSource ("x", documentReads, externalReads));
                        expect (doc.getDocumentElement() == nullptr);
                        expectEquals (doc.getLastParseError(), String ("unsupported DTD declaration"));
                    };

                    reject ("<!DOCTYPE a [ <!ENTITY % p \"<!ENTITY e 'x'>\"> %p; ]><a>&e;</a>");
                    reject ("<!DOCTYPE a [ <!ENTITY % p SYSTEM 'external.dtd'> " + String::repeatedString ("%p; ", 4096) + " ]><a>&e;</a>");
                    reject ("<!DOCTYPE a [ " + String::repeatedString ("%a; ", 16384) + "<!ENTITY e 'x'> ]><a>&e;</a>");
                    reject ("<!DOCTYPE a [ <!ENTITY % p0 \"x\"> <!ENTITY % p1 \"%p0;\"> %p1; ]><a>&e;</a>");
                    // the original shapes: a 65,536 character parameter value used 64/96/128 times
                    const auto declaration = "<!ENTITY e '" + String::repeatedString ("x", 65536) + "'>";

                    for (const auto repetitions : { 64, 96, 128 })
                        reject ("<!DOCTYPE a [ <!ENTITY % p \"" + declaration + "\"> " + String::repeatedString ("%p; ", repetitions)
                                + "]><a>" + String::repeatedString ("&e;", 32) + "</a>");

                    // long parameter chains (at and over the old depth cap) and a cycle
                    for (const auto depth : { 32, 33 })
                    {
                        String dtd = "<!ENTITY % p0 \"<!ENTITY e 'x'>\"> ";

                        for (int i = 1; i < depth; ++i)
                            dtd << "<!ENTITY % p" << i << " \"%p" << (i - 1) << ";\"> ";

                        reject ("<!DOCTYPE a [" + dtd + "%p" + String (depth - 1) + "; ]><a>&e;</a>");
                    }

                    reject ("<!DOCTYPE a [ <!ENTITY % p \"%p;\"> %p; ]><a>&e;</a>");

                    // the 8,192-declaration list delivered through a parameter entity
                    reject ("<!DOCTYPE a [ <!ENTITY % p \"" + String::repeatedString ("<!ENTITY e 'x'> ", 8192)
                            + "<!ENTITY tail 'y'>\"> %p; ]><a>&e;&tail;</a>");

                    reject ("<!DOCTYPE a SYSTEM 'external.dtd'><a>&e;</a>");
                    reject ("<!DOCTYPE a [ <!ENTITY e SYSTEM 'external.dtd'> ]><a>&e;</a>");
                    expectEquals (externalReads, 0);
                    expectEquals (documentReads, 0);
                }

                // many ordinary declarations of one name are cheap, and the first wins
                expectTree ("<!DOCTYPE a [ " + String::repeatedString ("<!ENTITY e 'x'> ", 8192) + "<!ENTITY tail 'y'> ]><a>&e;&tail;</a>",
                            "<a>xy</a>");
            }

            beginTest ("A document is reusable after an embedded NUL rejection");

            {
                TemporaryFile badFile (".xml"), goodFile (".xml");
                const char embedded[] = "<a/>\0<b/>";
                expect (badFile.getFile().replaceWithData (embedded, sizeof (embedded) - 1));
                expect (goodFile.getFile().replaceWithData ("<a x='1'><b/></a>", 17));

                XmlDocument doc (badFile.getFile());
                expect (doc.getDocumentElement() == nullptr);
                expectEquals (doc.getLastParseError(), String ("embedded null in input"));

                doc.setInputSource (new FileInputSource (goodFile.getFile()));
                auto good = doc.getDocumentElement();
                expect (good != nullptr);
                expect (doc.getLastParseError().isEmpty());

                if (good != nullptr)
                {
                    expectEquals (good->getStringAttribute ("x"), String ("1"));
                    expectEquals (good->getNumChildElements(), 1);
                }
            }

            beginTest ("Outer-element probes do not truncate a following full parse");

            for (const auto bigEndian : { false, true })
            {
                for (int pad = 0; pad < 12; ++pad)
                {
                    // The root start tag is far longer than the 8192-byte probe, so the probe
                    // ends somewhere inside it; padding moves the cut across every token position.
                    String text ("<ROOT");
                    int numAttributes = 1;
                    text << " first=\"" << String::repeatedString ("v", pad + 1) << "\"";

                    while (text.length() < 6000)
                    {
                        text << " a" << numAttributes << "=\"x\"";
                        ++numAttributes;
                    }

                    text << "><child n=\"1\"/><child n=\"2\">tail text</child></ROOT>";

                    MemoryOutputStream bytes;
                    bytes.writeByte (bigEndian ? (char) 0xfe : (char) 0xff);
                    bytes.writeByte (bigEndian ? (char) 0xff : (char) 0xfe);

                    for (auto p = text.getCharPointer(); ! p.isEmpty();)
                    {
                        const auto c = (uint32) p.getAndAdvance();
                        const auto high = (char) ((c >> 8) & 0xff);
                        const auto low = (char) (c & 0xff);
                        bytes.writeByte (bigEndian ? high : low);
                        bytes.writeByte (bigEndian ? low : high);
                    }

                    TemporaryFile temp (".xml");
                    expect (temp.getFile().replaceWithData (bytes.getData(), bytes.getDataSize()));
                    expect (temp.getFile().getSize() > 8192);

                    const auto label = String (bigEndian ? "BE" : "LE") + " pad " + String (pad);

                    XmlDocument doc (temp.getFile());
                    auto probed = doc.getDocumentElementIfTagMatches ("ROOT");
                    auto full = doc.getDocumentElement();

                    for (auto* tree : { probed.get(), full.get() })
                    {
                        expect (tree != nullptr, label);

                        if (tree != nullptr)
                        {
                            expectEquals (tree->getNumAttributes(), numAttributes, label);
                            expectEquals (tree->getNumChildElements(), 2, label);

                            expectEquals (tree->getStringAttribute ("first"), String::repeatedString ("v", pad + 1), label);

                            for (int i = 1; i < numAttributes; ++i)
                                expectEquals (tree->getStringAttribute ("a" + String (i)), String ("x"), label);

                            if (auto* first = tree->getChildElement (0))
                                expectEquals (first->getStringAttribute ("n"), String ("1"), label);

                            if (auto* second = tree->getChildElement (1))
                            {
                                expectEquals (second->getStringAttribute ("n"), String ("2"), label);
                                expectEquals (second->getAllSubText(), String ("tail text"), label);
                            }
                        }
                    }

                    XmlDocument other (temp.getFile());
                    expect (other.getDocumentElementIfTagMatches ("OTHER") == nullptr, label);
                }
            }

            beginTest ("A short root start tag in a large UTF-16 file parses completely");

            for (const auto bigEndian : { false, true })
            {
                String text ("<ROOT kind=\"big\">");

                for (int i = 0; i < 700; ++i)
                    text << "<item n=\"" << i << "\">text " << i << "</item>";

                text << "</ROOT>";

                MemoryOutputStream bytes;
                bytes.writeByte (bigEndian ? (char) 0xfe : (char) 0xff);
                bytes.writeByte (bigEndian ? (char) 0xff : (char) 0xfe);

                for (auto p = text.getCharPointer(); ! p.isEmpty();)
                {
                    const auto c = (uint32) p.getAndAdvance();
                    const auto high = (char) ((c >> 8) & 0xff);
                    const auto low = (char) (c & 0xff);
                    bytes.writeByte (bigEndian ? high : low);
                    bytes.writeByte (bigEndian ? low : high);
                }

                TemporaryFile temp (".xml");
                expect (temp.getFile().replaceWithData (bytes.getData(), bytes.getDataSize()));
                expect (temp.getFile().getSize() > 8192);

                const auto label = String (bigEndian ? "BE" : "LE");
                XmlDocument doc (temp.getFile());
                auto probed = doc.getDocumentElementIfTagMatches ("ROOT");
                auto full = doc.getDocumentElement();

                for (auto* tree : { probed.get(), full.get() })
                {
                    expect (tree != nullptr, label);

                    if (tree != nullptr)
                    {
                        expectEquals (tree->getStringAttribute ("kind"), String ("big"), label);
                        expectEquals (tree->getNumChildElements(), 700, label);

                        if (auto* last = tree->getChildElement (699))
                        {
                            expectEquals (last->getStringAttribute ("n"), String ("699"), label);
                            expectEquals (last->getAllSubText(), String ("text 699"), label);
                        }
                    }
                }
            }

            beginTest ("Embedded NUL bytes are rejected but trailing padding is not");

            const auto parseBytes = [this] (const MemoryBlock& bytes, String& error)
            {
                TemporaryFile temp (".xml");
                expect (temp.getFile().replaceWithData (bytes.getData(), bytes.getSize()));
                XmlDocument doc (temp.getFile());
                auto xml = doc.getDocumentElement();
                error = doc.getLastParseError();
                return xml;
            };

            const auto utf16Bytes = [] (std::initializer_list<const char*> parts)
            {
                // little-endian with a BOM; an empty part is a single NUL code unit
                MemoryOutputStream out;
                out.writeByte ((char) 0xff);
                out.writeByte ((char) 0xfe);

                for (const auto* part : parts)
                {
                    if (*part == 0)
                    {
                        out.writeByte (0);
                        out.writeByte (0);
                        continue;
                    }

                    for (const auto* c = part; *c != 0; ++c)
                    {
                        out.writeByte (*c);
                        out.writeByte (0);
                    }
                }

                return MemoryBlock (out.getData(), out.getDataSize());
            };

            {
                String error;
                const auto flat = [] (const XmlElement* e)
                {
                    return e != nullptr ? e->toString (XmlElement::TextFormat().singleLine().withoutHeader()) : String ("<null>");
                };

                const auto expectBad = [&] (const MemoryBlock& bytes, const String& label)
                {
                    expect (parseBytes (bytes, error) == nullptr, label);
                    expectEquals (error, String ("embedded null in input"), label);
                };

                const auto expectGood = [&] (const MemoryBlock& bytes, const String& expected, const String& label)
                {
                    auto good = parseBytes (bytes, error);
                    expectEquals (flat (good.get()), expected, label);
                    expect (error.isEmpty(), label);
                };

                // every embedded-NUL input is paired with its own trailing-padding twin
                const char embedded[] = "<a/>\0<b/>";
                expectBad (MemoryBlock (embedded, sizeof (embedded) - 1), "utf8 embedded");
                const char embeddedTwin[] = "<a/>\0\0\0";
                expectGood (MemoryBlock (embeddedTwin, sizeof (embeddedTwin) - 1), "<a/>", "utf8 embedded twin");

                const char single[] = "<a x='1'/>\0 ";
                expectBad (MemoryBlock (single, sizeof (single) - 1), "utf8 single");
                const char singleTwin[] = "<a x='1'/>\0\0";
                expectGood (MemoryBlock (singleTwin, sizeof (singleTwin) - 1), "<a x=\"1\"/>", "utf8 single twin");

                expectBad (utf16Bytes ({ "<a/>", "", "<b/>" }), "utf16 embedded");
                expectGood (utf16Bytes ({ "<a/>", "", "" }), "<a/>", "utf16 embedded twin");

                expectGood (utf16Bytes ({ "<a x='1'><b/></a>", "", "", "" }), "<a x=\"1\"><b/></a>", "utf16 padding");

                for (const auto padding : { 1, 3, 16 })
                {
                    const String xml ("<a x='1'><b/></a>");
                    MemoryBlock utf8 (xml.toRawUTF8(), xml.getNumBytesAsUTF8());
                    utf8.ensureSize (utf8.getSize() + (size_t) padding, true);
                    expectGood (utf8, "<a x=\"1\"><b/></a>", "utf8 padding " + String (padding));
                }
            }

            beginTest ("Writer output round-trips, with and without trailing content");

            std::vector<std::unique_ptr<XmlElement>> originals;

            {
                auto controls = std::make_unique<XmlElement> ("controls");

                for (int c = 1; c < 0x20; ++c)
                {
                    controls->setAttribute (Identifier (String::formatted ("c%02x", c)),
                                            "p" + String::charToString ((juce_wchar) c) + "q");

                    // a raw carriage return in text is normalised by the parser, so text skips 0x0d
                    if (c != 0x0d)
                        controls->createNewChildElement ("t")->addTextElement ("p" + String::charToString ((juce_wchar) c) + "q");
                }

                originals.push_back (std::move (controls));
            }

            {
                auto specials = std::make_unique<XmlElement> ("specials");
                const auto unicode = String::fromUTF8 ("caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80");
                specials->setAttribute ("punct", "a&b<c>d\"e'f");
                specials->setAttribute ("unicode", unicode);
                specials->setAttribute ("empty", "");
                specials->setAttribute ("multi", "line1\nline2\r\nline3\tx");
                specials->createNewChildElement ("text")->addTextElement ("x & y < z > \" ' w");
                specials->createNewChildElement ("unicode")->addTextElement (unicode);
                specials->createNewChildElement ("empty");
                originals.push_back (std::move (specials));
            }

            {
                auto mixed = std::make_unique<XmlElement> ("mixed");
                mixed->addTextElement ("lead ");
                mixed->createNewChildElement ("b")->setAttribute ("n", "1");
                mixed->addTextElement (" mid ");
                auto* c = mixed->createNewChildElement ("c");
                auto* d = c->createNewChildElement ("d");
                d->addTextElement ("deep");
                d->createNewChildElement ("e");
                c->createNewChildElement ("f")->createNewChildElement ("g")->setAttribute ("k", "v");
                mixed->addTextElement (" tail");
                originals.push_back (std::move (mixed));
            }

            originals.push_back (std::make_unique<XmlElement> ("empty"));

            const char* const suffixes[] = { "", "\n", "\r\n  \n", "<!-- c -->", "\n<?pi x?>\n" };
            int rowNumber = 0;

            for (const auto& original : originals)
            {
                std::vector<String> serialised;
                serialised.push_back (original->toString());
                serialised.push_back (original->toString (XmlElement::TextFormat().singleLine()));
                serialised.push_back (original->toString (XmlElement::TextFormat().withoutHeader()));

                {
                    auto format = XmlElement::TextFormat();
                    format.newLineChars = "\r\n";
                    serialised.push_back (original->toString (format));
                    format.newLineChars = "\n";
                    serialised.push_back (original->toString (format));
                    format.lineWrapLength = 5;
                    serialised.push_back (original->toString (format));
                }

                JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
                serialised.push_back (original->createDocument (""));
                JUCE_END_IGNORE_DEPRECATION_WARNINGS

                const auto expected = original->toString();

                for (const auto& text : serialised)
                {
                    ++rowNumber;

                    for (const auto* suffix : suffixes)
                    {
                        const auto label = original->getTagName() + " #" + String (rowNumber) + " suffix " + String (suffix).replace ("\n", "\\n");
                        auto parsed = XmlDocument::parse (text + suffix);
                        expect (parsed != nullptr, label);

                        if (parsed != nullptr)
                        {
                            expect (parsed->isEquivalentTo (original.get(), false), label);
                            expectEquals (parsed->toString(), expected, label);
                        }
                    }

                    for (const auto padding : { 1, 3, 16 })
                    {
                        const auto label = original->getTagName() + " #" + String (rowNumber) + " NUL padding " + String (padding);
                        MemoryBlock bytes (text.toRawUTF8(), text.getNumBytesAsUTF8());
                        bytes.ensureSize (bytes.getSize() + (size_t) padding, true);

                        String error;
                        auto parsed = parseBytes (bytes, error);
                        expect (parsed != nullptr, label);

                        if (parsed != nullptr)
                        {
                            expect (parsed->isEquivalentTo (original.get(), false), label);
                            expectEquals (parsed->toString(), expected, label);
                        }
                    }
                }
            }
        }
    }
};

static XmlElementTests xmlElementTests;

#endif

} // namespace juce
