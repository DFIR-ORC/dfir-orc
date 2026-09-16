//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Utilities/Manifest.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "Utilities/Error.h"
#include "Utilities/Log.h"
#include "Utilities/Resource.h"
#include "Utilities/Scope.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace fs = std::filesystem;

namespace {

// An application manifest is embedded under RT_MANIFEST with this id for an executable image.
constexpr WORD kCreateProcessManifestResourceId = 1;

constexpr std::string_view kElementName = "requestedExecutionLevel";
constexpr std::string_view kLevelAttribute = "level";

constexpr std::string_view kAsInvoker = "asInvoker";
constexpr std::string_view kHighestAvailable = "highestAvailable";
constexpr std::string_view kRequireAdministrator = "requireAdministrator";

// Byte range of the 'level' attribute value inside a manifest blob, quotes excluded.
struct LevelAttribute
{
    size_t offset = 0;
    size_t length = 0;
};

// A <requestedExecutionLevel ...> start tag: where its '<' is, and where its (possibly prefixed) name ends.
struct ElementSpan
{
    size_t offset = 0;
    size_t nameEnd = 0;
};

bool IsXmlSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// End of an XML tag name: whitespace, self-close, tag close, or the attribute list.
bool IsNameTerminator(char c)
{
    return IsXmlSpace(c) || c == '/' || c == '>';
}

// If a start tag begins at @p open, return the byte length of its element name (prefix included), or 0 if
// @p open is not the '<' of a start tag.
size_t StartTagNameLength(std::string_view text, size_t open)
{
    if (open >= text.size() || text[open] != '<')
    {
        return 0;
    }

    size_t pos = open + 1;
    if (pos >= text.size() || IsNameTerminator(text[pos]) || text[pos] == '!' || text[pos] == '?')
    {
        return 0;  // not a start tag (comment, CDATA, PI, close tag, or empty name)
    }

    while (pos < text.size() && !IsNameTerminator(text[pos]))
    {
        ++pos;
    }

    return pos - (open + 1);
}

// The local name of an element name, i.e. the part after an optional 'prefix:' (namespace prefix).
std::string_view LocalName(std::string_view elementName)
{
    const size_t colon = elementName.rfind(':');
    return (colon == std::string_view::npos) ? elementName : elementName.substr(colon + 1);
}

bool MatchesAt(std::string_view text, size_t offset, std::string_view needle)
{
    return offset <= text.size() && text.size() - offset >= needle.size()
        && text.compare(offset, needle.size(), needle) == 0;
}

bool EqualsIgnoreCase(std::string_view left, std::string_view right)
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

std::string_view ToAttributeValue(ExecutionLevel level)
{
    switch (level)
    {
        case ExecutionLevel::AsInvoker:
            return kAsInvoker;
        case ExecutionLevel::HighestAvailable:
            return kHighestAvailable;
        case ExecutionLevel::RequireAdministrator:
            return kRequireAdministrator;
        default:
            return {};
    }
}

// Collect every <requestedExecutionLevel ...> start tag, skipping comments and CDATA so a commented out or
// quoted occurrence is never mistaken for the real element. The element name is matched by its local part,
// so a namespace-prefixed form (e.g. <asmv3:requestedExecutionLevel>) is recognised too.
void CollectElementOffsets(std::string_view text, std::vector<ElementSpan>& elements)
{
    size_t pos = 0;
    while (pos < text.size())
    {
        const size_t open = text.find('<', pos);
        if (open == std::string_view::npos)
        {
            return;
        }

        if (MatchesAt(text, open, "<!--"))
        {
            const size_t end = text.find("-->", open + 4);
            pos = (end == std::string_view::npos) ? text.size() : end + 3;
            continue;
        }

        if (MatchesAt(text, open, "<![CDATA["))
        {
            const size_t end = text.find("]]>", open + 9);
            pos = (end == std::string_view::npos) ? text.size() : end + 3;
            continue;
        }

        const size_t nameLength = StartTagNameLength(text, open);
        if (nameLength != 0 && LocalName(text.substr(open + 1, nameLength)) == kElementName)
        {
            elements.push_back({open, open + 1 + nameLength});
        }

        pos = open + 1;
    }
}

// Walk the attributes of the start tag @p element and return the byte range of the 'level' attribute
// value. Quote aware, so a '>' inside an attribute value does not end the element early.
[[nodiscard]] std::error_code
FindLevelAttribute(std::string_view text, const ElementSpan& element, LevelAttribute& attribute)
{
    size_t pos = element.nameEnd;

    while (pos < text.size())
    {
        while (pos < text.size() && IsXmlSpace(text[pos]))
        {
            ++pos;
        }

        if (pos >= text.size() || text[pos] == '/' || text[pos] == '>')
        {
            Log::Debug(L"No 'level' attribute on <requestedExecutionLevel>");
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }

        const size_t nameOffset = pos;
        while (pos < text.size() && !IsXmlSpace(text[pos]) && text[pos] != '=' && text[pos] != '/'
               && text[pos] != '>')
        {
            ++pos;
        }

        const std::string_view name = text.substr(nameOffset, pos - nameOffset);

        while (pos < text.size() && IsXmlSpace(text[pos]))
        {
            ++pos;
        }

        if (pos >= text.size() || text[pos] != '=')
        {
            Log::Debug(L"Malformed <requestedExecutionLevel> attribute list");
            return std::make_error_code(std::errc::bad_message);
        }

        ++pos;
        while (pos < text.size() && IsXmlSpace(text[pos]))
        {
            ++pos;
        }

        if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\''))
        {
            Log::Debug(L"Unquoted <requestedExecutionLevel> attribute value");
            return std::make_error_code(std::errc::bad_message);
        }

        const char quote = text[pos];
        ++pos;

        const size_t valueOffset = pos;
        const size_t valueEnd = text.find(quote, valueOffset);
        if (valueEnd == std::string_view::npos)
        {
            Log::Debug(L"Unterminated <requestedExecutionLevel> attribute value");
            return std::make_error_code(std::errc::bad_message);
        }

        if (name == kLevelAttribute)
        {
            attribute.offset = valueOffset;
            attribute.length = valueEnd - valueOffset;
            return {};
        }

        pos = valueEnd + 1;
    }

    Log::Debug(L"Unterminated <requestedExecutionLevel> element");
    return std::make_error_code(std::errc::bad_message);
}

// Locate the single 'level' attribute value of a manifest blob. Offsets are relative to the start of
// @p manifest so a caller can splice the buffer directly.
[[nodiscard]] std::error_code LocateLevelAttribute(const std::vector<uint8_t>& manifest, LevelAttribute& attribute)
{
    if (manifest.size() >= 2
        && ((manifest[0] == 0xFF && manifest[1] == 0xFE) || (manifest[0] == 0xFE && manifest[1] == 0xFF)))
    {
        // RT_MANIFEST is UTF-8 by specification. Rather than grow a second code path for a blob no
        // Windows toolchain produces, treat it as unreadable and let the caller keep its own level. A
        // valid manifest we cannot read is worth a warning, since the capsule keeps its default level.
        Log::Warn(L"Manifest is UTF-16 encoded, cannot read its requested execution level");
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    std::string_view text(reinterpret_cast<const char*>(manifest.data()), manifest.size());

    std::vector<ElementSpan> elements;
    CollectElementOffsets(text, elements);

    if (elements.empty())
    {
        Log::Debug(L"No <requestedExecutionLevel> element in manifest");
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    if (elements.size() > 1)
    {
        // Ambiguous: picking one would silently change a level the other declares. Fail closed.
        Log::Warn(L"Multiple <requestedExecutionLevel> elements in manifest (count: {})", elements.size());
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    return FindLevelAttribute(text, elements.front(), attribute);
}

[[nodiscard]] std::error_code ReadManifestResource(const fs::path& binary, std::vector<uint8_t>& manifest)
{
    manifest.clear();

    ScopedModule module;
    if (auto ec = LoadResourceModule(binary, module); ec)
    {
        return ec;  // LoadResourceModule logged it
    }

    HRSRC hResource =
        FindResourceW(module.get(), MAKEINTRESOURCEW(kCreateProcessManifestResourceId), RT_MANIFEST);
    if (!hResource)
    {
        // No application manifest: not an error, the caller keeps its own level.
        Log::Debug(L"No RT_MANIFEST (path: {})", binary);
        return {};
    }

    HGLOBAL hLoaded = LoadResource(module.get(), hResource);
    if (!hLoaded)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed LoadResource for RT_MANIFEST (path: {}) [{}]", binary, ec);
        return ec;
    }

    const DWORD size = SizeofResource(module.get(), hResource);
    if (size == 0)
    {
        Log::Debug(L"Empty RT_MANIFEST (path: {})", binary);
        return {};
    }

    const void* data = LockResource(hLoaded);
    if (!data)
    {
        return std::make_error_code(std::errc::bad_address);
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    manifest.assign(bytes, bytes + size);
    return {};
}

}  // namespace

const wchar_t* GetExecutionLevelName(ExecutionLevel level)
{
    switch (level)
    {
        case ExecutionLevel::AsInvoker:
            return L"asInvoker";
        case ExecutionLevel::HighestAvailable:
            return L"highestAvailable";
        case ExecutionLevel::RequireAdministrator:
            return L"requireAdministrator";
        default:
            return L"none";
    }
}

ExecutionLevel ParseExecutionLevel(std::string_view value)
{
    if (EqualsIgnoreCase(value, kAsInvoker))
    {
        return ExecutionLevel::AsInvoker;
    }

    if (EqualsIgnoreCase(value, kHighestAvailable))
    {
        return ExecutionLevel::HighestAvailable;
    }

    if (EqualsIgnoreCase(value, kRequireAdministrator))
    {
        return ExecutionLevel::RequireAdministrator;
    }

    return ExecutionLevel::None;
}

std::error_code ReadRequestedExecutionLevel(const std::vector<uint8_t>& manifest, ExecutionLevel& level)
{
    level = ExecutionLevel::None;

    if (manifest.empty())
    {
        return {};
    }

    LevelAttribute attribute;
    auto ec = LocateLevelAttribute(manifest, attribute);
    if (ec)
    {
        // Nothing unambiguous to read: report no level rather than an error, the caller keeps its own.
        return {};
    }

    std::string_view text(reinterpret_cast<const char*>(manifest.data()), manifest.size());
    level = ParseExecutionLevel(text.substr(attribute.offset, attribute.length));

    return {};
}

std::error_code ReadRequestedExecutionLevel(const fs::path& binary, ExecutionLevel& level)
{
    level = ExecutionLevel::None;

    std::vector<uint8_t> manifest;
    auto ec = ReadManifestResource(binary, manifest);
    if (ec)
    {
        return ec;
    }

    return ReadRequestedExecutionLevel(manifest, level);
}

std::error_code SetRequestedExecutionLevel(std::vector<uint8_t>& manifest, ExecutionLevel level)
{
    const std::string_view value = ToAttributeValue(level);
    if (value.empty())
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    LevelAttribute attribute;
    auto ec = LocateLevelAttribute(manifest, attribute);
    if (ec)
    {
        return ec;
    }

    const auto begin = manifest.begin() + static_cast<ptrdiff_t>(attribute.offset);
    manifest.erase(begin, begin + static_cast<ptrdiff_t>(attribute.length));
    manifest.insert(
        manifest.begin() + static_cast<ptrdiff_t>(attribute.offset), value.begin(), value.end());

    return {};
}
