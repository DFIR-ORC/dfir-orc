//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#include "stdafx.h"

#include "OutputSpec.h"
#include "Configuration/ConfigFile.h"
#include "Configuration/ConfigFile_Common.h"
#include "Configuration/ConfigFileReader.h"
#include "MemoryStream.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

// The <uploads> schema registers under a bare root, so a test reads real XML through the real
// config engine rather than hand-building a ConfigItem tree that could drift from the schema.
TEST_CLASS(UploadDestinationSchemaTest)
{
private:
    static HRESULT ReadUploadsXml(const std::string& xmlUtf8, ConfigItem& root)
    {
        root.strName = L"dfir-orc";
        root.Flags = ConfigItem::MANDATORY;
        root.dwIndex = 0L;
        root.Status = ConfigItem::MISSING;

        HRESULT hr = Orc::Config::Common::uploads(root, 0);
        if (FAILED(hr))
        {
            return hr;
        }

        auto stream = std::make_shared<MemoryStream>();
        hr = stream->OpenForReadOnly(const_cast<char*>(xmlUtf8.data()), xmlUtf8.size());
        if (FAILED(hr))
        {
            return hr;
        }

        ConfigFileReader reader;
        return reader.ReadConfig(stream, root);
    }

public:
    TEST_METHOD(TwoDestinationsEachWithTheirOwnChain)
    {
        const std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <uploads delete_when_done=\"yes\">\n"
            "    <destination name=\"hq\">\n"
            "      <upload method=\"REST\" server=\"http://hq1.example\"/>\n"
            "      <upload method=\"REST\" server=\"http://hq2.example\"/>\n"
            "    </destination>\n"
            "    <destination name=\"nas\" required=\"no\" artifacts=\"no\" include=\"GetThis*\">\n"
            "      <upload method=\"FILECOPY\" path=\"\\\\nas\\drop\"/>\n"
            "    </destination>\n"
            "  </uploads>\n"
            "</dfir-orc>\n";

        ConfigItem root;
        Assert::IsTrue(SUCCEEDED(ReadUploadsXml(xml, root)));

        const ConfigItem& uploads = root.SubItems[0];
        Assert::IsTrue(uploads.Status == ConfigItem::PRESENT);
        Assert::AreEqual(
            std::wstring(L"yes"), (const std::wstring&)uploads.SubItems[CONFIG_UPLOADS_DELETE_WHEN_DONE]);

        const ConfigItem& destinations = uploads.SubItems[CONFIG_UPLOADS_DESTINATION];
        Assert::AreEqual(size_t(2), destinations.NodeList.size());

        Assert::AreEqual(
            std::wstring(L"hq"), (const std::wstring&)destinations.NodeList[0].SubItems[CONFIG_DESTINATION_NAME]);
        Assert::AreEqual(size_t(2), destinations.NodeList[0].SubItems[CONFIG_DESTINATION_UPLOAD].NodeList.size());

        Assert::AreEqual(
            std::wstring(L"nas"), (const std::wstring&)destinations.NodeList[1].SubItems[CONFIG_DESTINATION_NAME]);
        Assert::AreEqual(
            std::wstring(L"no"), (const std::wstring&)destinations.NodeList[1].SubItems[CONFIG_DESTINATION_REQUIRED]);
        Assert::AreEqual(
            std::wstring(L"GetThis*"),
            (const std::wstring&)destinations.NodeList[1].SubItems[CONFIG_DESTINATION_FILTER_INC]);
        Assert::AreEqual(size_t(1), destinations.NodeList[1].SubItems[CONFIG_DESTINATION_UPLOAD].NodeList.size());
    }

    // The <upload> inside <destination> must be the SAME schema as the top-level list: a <rest> step
    // list that parsed at top level and not here would be the drift this design exists to avoid.
    TEST_METHOD(NestedUploadKeepsTheRestStepSchema)
    {
        const std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <uploads>\n"
            "    <destination name=\"hq\">\n"
            "      <upload method=\"REST\" server=\"http://hq1.example\">\n"
            "        <rest http_method=\"PUT\" path=\"/up/{upload.filename}\" body=\"{upload.local_path}\"/>\n"
            "      </upload>\n"
            "    </destination>\n"
            "  </uploads>\n"
            "</dfir-orc>\n";

        ConfigItem root;
        Assert::IsTrue(SUCCEEDED(ReadUploadsXml(xml, root)));

        const ConfigItem& uploadNode =
            root.SubItems[0].SubItems[CONFIG_UPLOADS_DESTINATION].NodeList[0].SubItems[CONFIG_DESTINATION_UPLOAD];
        Assert::AreEqual(size_t(1), uploadNode.NodeList.size());
        Assert::AreEqual(size_t(1), uploadNode.NodeList[0].SubItems[CONFIG_UPLOAD_REST_LIST].NodeList.size());
    }
};

}  // namespace Orc::Test
