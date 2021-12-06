//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "OrcLib.h"

#include <functional>

#include "Flags.h"

#pragma managed(push, off)

namespace Orc {

class ObjectDirectory
{
public:
    enum ObjectType
    {
        Invalid,
        Type,
        Directory,
        Session,
        WindowStations,
        Event,
        KeyedEvent,
        Callback,
        Job,
        Mutant,
        Section,
        Semaphore,
        SymbolicLink,
        Device,
        Driver,
        ALPCPort,
        FilterConnectionPort,
        Key,
        File
    };

    typedef std::function<void(const ObjectType Type, const std::wstring& name, const std::wstring& path)>
        ObjectCallback;
    typedef std::function<void(
        const ObjectType Type,
        const std::wstring& name,
        const std::wstring& path,
        const std::wstring& linktarget,
        LARGE_INTEGER&& lnkcreationtime)>
        SymLinkCallback;

    static const FlagsDefinition g_ObjectTypeDefinition[];

    class ObjectInstance
    {
    public:
        ObjectType Type = ObjectType::Invalid;
        std::wstring Name;
        std::wstring Path;
        std::wstring LinkTarget;
        LARGE_INTEGER LinkCreationTime;

        ObjectInstance() { ZeroMemory(&LinkCreationTime, sizeof(LARGE_INTEGER)); }

        ObjectInstance(ObjectInstance&& other) noexcept = default;

        ObjectInstance(ObjectType type, const std::wstring& name, const std::wstring& path)
            : Type(type)
            , Name(name)
            , Path(path)
        {
            ZeroMemory(&LinkCreationTime, sizeof(LARGE_INTEGER));
        };

        ObjectInstance(
            ObjectType type,
            const std::wstring& name,
            const std::wstring& path,
            const std::wstring& linktarget,
            const LARGE_INTEGER& lnkcreationtime)
            : ObjectInstance(type, name, path)
        {
            LinkTarget = linktarget;
            LinkCreationTime = lnkcreationtime;
        }

        ObjectInstance(ObjectType type, std::wstring&& name, std::wstring&& path) noexcept
        {
            std::swap(Type, type);
            std::swap(Name, name);
            std::swap(Path, path);
            ZeroMemory(&LinkCreationTime, sizeof(LARGE_INTEGER));
        };

        ObjectInstance(
            ObjectType type,
            std::wstring&& name,
            std::wstring&& path,
            std::wstring&& linktarget,
            LARGE_INTEGER&& lnkcreationtime)
            : ObjectInstance(type, name, path)
        {
            std::swap(LinkTarget, linktarget);
            std::swap(LinkCreationTime, lnkcreationtime);
        }

        HRESULT Write(ITableOutput& output, const std::wstring& strDescription) const;
        HRESULT Write(IStructuredOutput& pWriter, LPCWSTR szElement = L"object") const;
    };

public:
    static ObjectType GetObjectType(const UNICODE_STRING& type);
    static ObjectType GetObjectType(const std::wstring& type);

    HRESULT
    ParseObjectDirectory(const std::wstring& aObjDir, std::vector<ObjectInstance>& objects, bool bRecursive = true);

    ~ObjectDirectory();
};
}  // namespace Orc
#pragma managed(pop)
