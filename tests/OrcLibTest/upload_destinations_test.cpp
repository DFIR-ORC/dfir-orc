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
#include "Configuration/UploadDestinationReader.h"
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

// A top-level <upload> list must keep meaning exactly what it means today: one destination, one
// chain, required. This is the compatibility assertion the whole design rests on.
TEST_CLASS(LegacyUploadChainTest)
{
private:
    static HRESULT ReadLegacyXml(const std::string& xmlUtf8, std::vector<OutputSpec::UploadDestination>& out)
    {
        ConfigItem root;
        root.strName = L"dfir-orc";
        root.Flags = ConfigItem::MANDATORY;
        root.dwIndex = 0L;
        root.Status = ConfigItem::MISSING;

        HRESULT hr = Orc::Config::Common::upload_list(root, 0);
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
        hr = reader.ReadConfig(stream, root);
        if (FAILED(hr))
        {
            return hr;
        }

        return Orc::Config::ReadLegacyUploadChain(root.SubItems[0], out);
    }

public:
    TEST_METHOD(TwoSiblingsBecomeOneRequiredUnnamedDestination)
    {
        const std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <upload method=\"REST\" server=\"http://a.example\"/>\n"
            "  <upload method=\"REST\" server=\"http://b.example\"/>\n"
            "</dfir-orc>\n";

        std::vector<OutputSpec::UploadDestination> destinations;
        Assert::IsTrue(SUCCEEDED(ReadLegacyXml(xml, destinations)));

        Assert::AreEqual(size_t(1), destinations.size());
        Assert::IsTrue(destinations[0].Name.empty());
        Assert::IsTrue(destinations[0].Required);
        Assert::IsTrue(destinations[0].Artifacts);
        Assert::AreEqual(size_t(2), destinations[0].Chain.size());
        Assert::AreEqual(std::wstring(L"a.example"), destinations[0].Chain[0]->ServerName);
        Assert::AreEqual(std::wstring(L"b.example"), destinations[0].Chain[1]->ServerName);
    }

    // Today's rule, preserved: one bad entry does not cost the others. 'uri' together with 'server'
    // is a real Configure failure (E_INVALIDARG), unlike an unknown method, which is documented to
    // become NoUpload and configure fine.
    TEST_METHOD(UnconfigurableEntryIsSkippedWhileAnotherRemains)
    {
        const std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <upload method=\"REST\" server=\"http://a.example\" uri=\"http://a.example\"/>\n"
            "  <upload method=\"REST\" server=\"http://b.example\"/>\n"
            "</dfir-orc>\n";

        std::vector<OutputSpec::UploadDestination> destinations;
        Assert::IsTrue(SUCCEEDED(ReadLegacyXml(xml, destinations)));

        Assert::AreEqual(size_t(1), destinations.size());
        Assert::AreEqual(size_t(1), destinations[0].Chain.size());
        Assert::AreEqual(std::wstring(L"b.example"), destinations[0].Chain[0]->ServerName);
    }

    TEST_METHOD(NoUsableEntryFails)
    {
        const std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <upload method=\"REST\" server=\"http://a.example\" uri=\"http://a.example\"/>\n"
            "</dfir-orc>\n";

        std::vector<OutputSpec::UploadDestination> destinations;
        Assert::IsTrue(FAILED(ReadLegacyXml(xml, destinations)));
    }

    // The embedded WolfLauncher config and /local are read by two different functions into the SAME
    // vector. Two legacy lists used to concatenate into one fallback chain; they must still do so,
    // rather than becoming two independent destinations that each get a copy of the collection.
    TEST_METHOD(TwoLegacyListsConcatenateIntoOneChain)
    {
        const std::string first =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <upload method=\"REST\" server=\"http://a.example\"/>\n"
            "</dfir-orc>\n";
        const std::string second =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <upload method=\"REST\" server=\"http://b.example\"/>\n"
            "</dfir-orc>\n";

        std::vector<OutputSpec::UploadDestination> destinations;
        Assert::IsTrue(SUCCEEDED(ReadLegacyXml(first, destinations)));
        Assert::IsTrue(SUCCEEDED(ReadLegacyXml(second, destinations)));

        Assert::AreEqual(size_t(1), destinations.size(), L"two legacy lists must stay one destination");
        Assert::AreEqual(size_t(2), destinations[0].Chain.size());
        Assert::AreEqual(std::wstring(L"a.example"), destinations[0].Chain[0]->ServerName);
        Assert::AreEqual(std::wstring(L"b.example"), destinations[0].Chain[1]->ServerName);
    }
};

// Filters inherit <uploads> -> <destination> -> <upload>, nearest wins, each attribute
// independently. Resolution is driven by attribute PRESENCE, so include="" means "deliberately
// empty, do not inherit".
TEST_CLASS(UploadDestinationReaderTest)
{
private:
    static HRESULT Read(
        const std::string& xmlUtf8,
        std::vector<OutputSpec::UploadDestination>& destinations,
        bool& deleteWhenDone)
    {
        ConfigItem root;
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
        hr = reader.ReadConfig(stream, root);
        if (FAILED(hr))
        {
            return hr;
        }

        return Orc::Config::ReadUploadDestinations(root.SubItems[0], destinations, deleteWhenDone);
    }

    static std::string Wrap(const std::string& uploadsAttrs, const std::string& body)
    {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<dfir-orc>\n"
               "  <uploads "
            + uploadsAttrs + ">\n" + body
            + "  </uploads>\n"
              "</dfir-orc>\n";
    }

    static const char* OneUpload() { return "      <upload method=\"REST\" server=\"http://a.example\"/>\n"; }

    // A real Configure failure: 'uri' cannot be combined with 'server'. An unknown method= is NOT one,
    // it becomes NoUpload and configures fine.
    static const char* BadUpload()
    {
        return "      <upload method=\"REST\" server=\"http://a.example\" uri=\"http://a.example\"/>\n";
    }

public:
    TEST_METHOD(DestinationInheritsUploadsExclude)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "exclude=\"*.log\"",
            std::string("    <destination name=\"hq\" include=\"*.7z\">\n") + OneUpload() + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        const OutputSpec::Upload& upload = *destinations[0].Chain[0];
        Assert::AreEqual(size_t(1), upload.FilterInclude.size());
        Assert::AreEqual(std::wstring(L"*.7z"), upload.FilterInclude[0]);
        Assert::AreEqual(size_t(1), upload.FilterExclude.size());
        Assert::AreEqual(std::wstring(L"*.log"), upload.FilterExclude[0]);
    }

    TEST_METHOD(DestinationExcludeOverridesUploadsExclude)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "exclude=\"*.log\"",
            std::string("    <destination name=\"nas\" exclude=\"*Thumbs*\">\n") + OneUpload()
                + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        const OutputSpec::Upload& upload = *destinations[0].Chain[0];
        Assert::AreEqual(size_t(1), upload.FilterExclude.size());
        Assert::AreEqual(std::wstring(L"*Thumbs*"), upload.FilterExclude[0]);
    }

    TEST_METHOD(UploadLevelWinsOverBoth)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "include=\"*.a\"",
            "    <destination name=\"hq\" include=\"*.b\">\n"
            "      <upload method=\"REST\" server=\"http://a.example\" include=\"*.c\"/>\n"
            "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        Assert::AreEqual(size_t(1), destinations[0].Chain[0]->FilterInclude.size());
        Assert::AreEqual(std::wstring(L"*.c"), destinations[0].Chain[0]->FilterInclude[0]);
    }

    // An explicitly empty attribute is a decision, not an absence: it must not inherit.
    TEST_METHOD(EmptyIncludeDoesNotInherit)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "include=\"*.a\"",
            std::string("    <destination name=\"hq\" include=\"\">\n") + OneUpload() + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        Assert::IsTrue(destinations[0].Chain[0]->FilterInclude.empty());
    }

    // Review Focus 5: a stray separator must not leave an empty pattern, which PathMatchSpecW would
    // then get a say over.
    TEST_METHOD(StraySeparatorYieldsNoEmptyPattern)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            std::string("    <destination name=\"hq\" include=\"*.7z,\">\n") + OneUpload() + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        const OutputSpec::Upload& upload = *destinations[0].Chain[0];
        Assert::AreEqual(size_t(1), upload.FilterInclude.size());
        Assert::AreEqual(std::wstring(L"*.7z"), upload.FilterInclude[0]);
    }

    TEST_METHOD(RequiredAndArtifactsDefaultToYes)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml =
            Wrap("", std::string("    <destination name=\"hq\">\n") + OneUpload() + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        Assert::IsTrue(destinations[0].Required);
        Assert::IsTrue(destinations[0].Artifacts);
        Assert::IsFalse(deleteWhenDone);
    }

    TEST_METHOD(RequiredAndArtifactsReadExplicitNo)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "delete_when_done=\"yes\"",
            std::string("    <destination name=\"nas\" required=\"no\" artifacts=\"no\">\n") + OneUpload()
                + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        Assert::IsFalse(destinations[0].Required);
        Assert::IsFalse(destinations[0].Artifacts);
        Assert::IsTrue(deleteWhenDone);
    }

    // Review Focus 4: two destinations that differ only in case are indistinguishable in every log
    // line, so the refusal has to be case-insensitive.
    TEST_METHOD(DuplicateNameIsRefusedCaseInsensitively)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            std::string("    <destination name=\"hq\">\n") + OneUpload() + "    </destination>\n"
                + "    <destination name=\"HQ\">\n" + OneUpload() + "    </destination>\n");

        Assert::IsTrue(FAILED(Read(xml, destinations, deleteWhenDone)));
    }

    // Review Focus 3: a config that asked for uploads and declared none must be refused, not run
    // silently with no upload at all.
    TEST_METHOD(EmptyUploadsBlockIsRefused)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        Assert::IsTrue(FAILED(Read(Wrap("", ""), destinations, deleteWhenDone)));
    }

    TEST_METHOD(DestinationWithNoUploadIsRefused)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap("", "    <destination name=\"hq\"/>\n");
        Assert::IsTrue(FAILED(Read(xml, destinations, deleteWhenDone)));
    }

    // A required destination that cannot be configured at all is refused rather than dropped:
    // dropping it would silently reduce the redundancy the operator asked for.
    TEST_METHOD(UnconfigurableRequiredDestinationIsRefused)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            std::string("    <destination name=\"hq\">\n") + BadUpload() + "    </destination>\n"
                + "    <destination name=\"nas\">\n" + OneUpload() + "    </destination>\n");

        Assert::IsTrue(FAILED(Read(xml, destinations, deleteWhenDone)));
    }

    TEST_METHOD(UnconfigurableOptionalDestinationIsDropped)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            std::string("    <destination name=\"nas\" required=\"no\">\n") + BadUpload() + "    </destination>\n"
                + "    <destination name=\"hq\">\n" + OneUpload() + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        Assert::AreEqual(size_t(1), destinations.size());
        Assert::AreEqual(std::wstring(L"hq"), destinations[0].Name);
    }

    // The inherited filters are applied by NodeList index, not chain position. With a dropped entry
    // ahead of it, a surviving entry must still get the destination's filters, not the next one's.
    TEST_METHOD(InheritanceSurvivesADroppedEntryEarlierInTheChain)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            std::string("    <destination name=\"hq\" include=\"*.7z\">\n") + BadUpload()
                + "      <upload method=\"REST\" server=\"http://b.example\"/>\n"
                + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        Assert::AreEqual(size_t(1), destinations[0].Chain.size());
        Assert::AreEqual(size_t(1), destinations[0].Chain[0]->FilterInclude.size());
        Assert::AreEqual(std::wstring(L"*.7z"), destinations[0].Chain[0]->FilterInclude[0]);
    }

    // delete_when_done defaults to NO and destroys evidence, so it must require an explicit yes.
    // Reading it with the yes-by-default rule made every unrecognised value delete the collection.
    TEST_METHOD(DeleteWhenDoneRequiresAnExplicitYes)
    {
        // Whitespace is trimmed, so " no " is still a no; what must never enable it is a value the
        // reader does not positively recognise.
        for (const char* value : {"off", "n", "disabled", "", " no ", "false"})
        {
            std::vector<OutputSpec::UploadDestination> destinations;
            bool deleteWhenDone = true;  // seeded true, so a failure to clear it is caught
            const std::string xml = Wrap(
                std::string("delete_when_done=\"") + value + "\"",
                std::string("    <destination name=\"hq\">\n") + OneUpload() + "    </destination>\n");

            Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
            Assert::IsFalse(deleteWhenDone, L"an unrecognised delete_when_done value must not delete");
        }
    }

    TEST_METHOD(DeleteWhenDoneAcceptsTheUsualAffirmatives)
    {
        for (const char* value : {"yes", "YES", "true", "1", " yes "})
        {
            std::vector<OutputSpec::UploadDestination> destinations;
            bool deleteWhenDone = false;
            const std::string xml = Wrap(
                std::string("delete_when_done=\"") + value + "\"",
                std::string("    <destination name=\"hq\">\n") + OneUpload() + "    </destination>\n");

            Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
            Assert::IsTrue(deleteWhenDone);
        }
    }

    // The schema marks 'name' MANDATORY, but CheckConfig only tests presence, so an empty value gets
    // through and leaves a destination that cannot be named in a log line.
    TEST_METHOD(EmptyDestinationNameIsRefused)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml =
            Wrap("", std::string("    <destination name=\"\">\n") + OneUpload() + "    </destination>\n");

        Assert::IsTrue(FAILED(Read(xml, destinations, deleteWhenDone)));
    }

    // move deletes the source after upload, so a peer destination would find nothing. Refused where
    // it is written, not discovered mid-transfer.
    TEST_METHOD(MoveWithTwoDestinationsIsRefused)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            "    <destination name=\"hq\">\n"
            "      <upload method=\"REST\" server=\"http://a.example\" operation=\"move\"/>\n"
            "    </destination>\n"
            "    <destination name=\"nas\">\n"
            "      <upload method=\"REST\" server=\"http://b.example\"/>\n"
            "    </destination>\n");

        Assert::IsTrue(FAILED(Read(xml, destinations, deleteWhenDone)));
    }

    // Refused whichever destination writes it: the second one deleting the file is just as fatal to
    // the first as the other way round.
    TEST_METHOD(MoveOnTheSecondDestinationIsAlsoRefused)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            "    <destination name=\"hq\">\n"
            "      <upload method=\"REST\" server=\"http://a.example\"/>\n"
            "    </destination>\n"
            "    <destination name=\"nas\">\n"
            "      <upload method=\"REST\" server=\"http://b.example\" operation=\"move\"/>\n"
            "    </destination>\n");

        Assert::IsTrue(FAILED(Read(xml, destinations, deleteWhenDone)));
    }

    // One destination is the historical case and keeps working: move is only dangerous next to a peer,
    // and a fallback inside one chain that finds the file gone is existing behaviour.
    TEST_METHOD(MoveWithOneDestinationIsAccepted)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            "    <destination name=\"hq\">\n"
            "      <upload method=\"REST\" server=\"http://a.example\" operation=\"move\"/>\n"
            "      <upload method=\"REST\" server=\"http://b.example\" operation=\"move\"/>\n"
            "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        Assert::AreEqual(size_t(1), destinations.size());
        Assert::AreEqual(size_t(2), destinations[0].Chain.size());
    }

    // An optional destination dropped for being unconfigurable leaves one destination behind, so the
    // move on the survivor must not be refused by a count taken before the drop.
    TEST_METHOD(MoveIsJudgedOnSurvivingDestinations)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        const std::string xml = Wrap(
            "",
            std::string("    <destination name=\"nas\" required=\"no\">\n") + BadUpload() + "    </destination>\n"
                + "    <destination name=\"hq\">\n"
                + "      <upload method=\"REST\" server=\"http://b.example\" operation=\"move\"/>\n"
                + "    </destination>\n");

        Assert::IsTrue(SUCCEEDED(Read(xml, destinations, deleteWhenDone)));
        Assert::AreEqual(size_t(1), destinations.size());
    }
};

// An empty filter attribute must mean "no filter" at every level. Upload::Configure used a bare
// boost::split, which turns "" into one empty pattern: PathMatchSpecW(name, L"") matches nothing, so a
// destination written to take everything silently received no collected data at all.
TEST_CLASS(UploadFilterEdgeTest)
{
private:
    static HRESULT ReadInlineUpload(const std::string& attrs, OutputSpec::Upload& upload)
    {
        ConfigItem root;
        root.strName = L"dfir-orc";
        root.Flags = ConfigItem::MANDATORY;
        root.dwIndex = 0L;
        root.Status = ConfigItem::MISSING;
        if (auto hr = Orc::Config::Common::upload(root, 0); FAILED(hr))
        {
            return hr;
        }

        const std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<dfir-orc>\n  <upload method=\"REST\" "
                                "server=\"http://a.example\" "
            + attrs + "/>\n</dfir-orc>\n";
        auto stream = std::make_shared<MemoryStream>();
        if (auto hr = stream->OpenForReadOnly(const_cast<char*>(xml.data()), xml.size()); FAILED(hr))
        {
            return hr;
        }

        ConfigFileReader reader;
        if (auto hr = reader.ReadConfig(stream, root); FAILED(hr))
        {
            return hr;
        }

        return upload.Configure(root.SubItems[0]);
    }

public:
    TEST_METHOD(EmptyIncludeIsNoFilterNotAnUnmatchablePattern)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload("include=\"\"", upload)));
        Assert::IsTrue(upload.FilterInclude.empty());
        Assert::IsTrue(upload.IsFileUploaded(L"a.7z"), L"an empty include must not exclude everything");
    }

    TEST_METHOD(StraySeparatorYieldsNoEmptyPatternAtTheUploadLevel)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload("include=\"*.7z,\"", upload)));
        Assert::AreEqual(size_t(1), upload.FilterInclude.size());
        Assert::AreEqual(std::wstring(L"*.7z"), upload.FilterInclude[0]);
    }

    TEST_METHOD(EmptyExcludeExcludesNothing)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload("exclude=\"\"", upload)));
        Assert::IsTrue(upload.FilterExclude.empty());
        Assert::IsTrue(upload.IsFileUploaded(L"a.7z"));
    }
};

// The two config files (embedded WolfLauncher config and /local) are read into one vector by two
// different functions, so the "one form or the other, never both" rule has to hold across them and not
// only within a single file.
TEST_CLASS(MixedUploadFormTest)
{
private:
    static HRESULT ReadLegacy(std::vector<OutputSpec::UploadDestination>& out)
    {
        ConfigItem root;
        root.strName = L"dfir-orc";
        root.Flags = ConfigItem::MANDATORY;
        root.dwIndex = 0L;
        root.Status = ConfigItem::MISSING;
        if (auto hr = Orc::Config::Common::upload_list(root, 0); FAILED(hr))
        {
            return hr;
        }

        const std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <upload method=\"REST\" server=\"http://a.example\"/>\n"
            "</dfir-orc>\n";
        auto stream = std::make_shared<MemoryStream>();
        if (auto hr = stream->OpenForReadOnly(const_cast<char*>(xml.data()), xml.size()); FAILED(hr))
        {
            return hr;
        }

        ConfigFileReader reader;
        if (auto hr = reader.ReadConfig(stream, root); FAILED(hr))
        {
            return hr;
        }

        return Orc::Config::ReadLegacyUploadChain(root.SubItems[0], out);
    }

    static HRESULT ReadUploads(std::vector<OutputSpec::UploadDestination>& out, bool& deleteWhenDone)
    {
        ConfigItem root;
        root.strName = L"dfir-orc";
        root.Flags = ConfigItem::MANDATORY;
        root.dwIndex = 0L;
        root.Status = ConfigItem::MISSING;
        if (auto hr = Orc::Config::Common::uploads(root, 0); FAILED(hr))
        {
            return hr;
        }

        const std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <uploads>\n"
            "    <destination name=\"hq\">\n"
            "      <upload method=\"REST\" server=\"http://b.example\"/>\n"
            "    </destination>\n"
            "  </uploads>\n"
            "</dfir-orc>\n";
        auto stream = std::make_shared<MemoryStream>();
        if (auto hr = stream->OpenForReadOnly(const_cast<char*>(xml.data()), xml.size()); FAILED(hr))
        {
            return hr;
        }

        ConfigFileReader reader;
        if (auto hr = reader.ReadConfig(stream, root); FAILED(hr))
        {
            return hr;
        }

        return Orc::Config::ReadUploadDestinations(root.SubItems[0], out, deleteWhenDone);
    }

public:
    TEST_METHOD(UploadsBlockAfterALegacyListIsRefused)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        Assert::IsTrue(SUCCEEDED(ReadLegacy(destinations)));
        Assert::IsTrue(FAILED(ReadUploads(destinations, deleteWhenDone)));
    }

    TEST_METHOD(LegacyListAfterAnUploadsBlockIsRefused)
    {
        std::vector<OutputSpec::UploadDestination> destinations;
        bool deleteWhenDone = false;
        Assert::IsTrue(SUCCEEDED(ReadUploads(destinations, deleteWhenDone)));
        Assert::IsTrue(FAILED(ReadLegacy(destinations)));
    }
};

}  // namespace Orc::Test
