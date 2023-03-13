#include "Dump.h"

#include <fstream>

#include <fmt/format.h>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

#include "Filesystem/Ntfs/ShadowCopy/Snapshot.h"
#include "Filesystem/Ntfs/ShadowCopy/ShadowCopy.h"
#include "Text/Guid.h"
#include "Text/Hex.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

void Dump(const std::string& path, const SnapshotInformation& info)
{
    rapidjson::Document document;
    document.SetObject();

    {
        rapidjson::Value jsonBlock(rapidjson::Type::kObjectType);

        {
            rapidjson::Value guid;
            guid.SetString(Orc::ToString(info.Guid()).c_str(), document.GetAllocator());
            jsonBlock.AddMember("guid", guid, document.GetAllocator());
        }

        jsonBlock.AddMember("size", info.Size(), document.GetAllocator());
        jsonBlock.AddMember("position", info.LayerPosition(), document.GetAllocator());
        jsonBlock.AddMember(
            "creation_time", *reinterpret_cast<const uint64_t*>(&info.CreationTime()), document.GetAllocator());
        jsonBlock.AddMember("flags", info.Flags(), document.GetAllocator());

        {
            rapidjson::Value diffAreaBlock(rapidjson::Type::kObjectType);

            const auto& diffArea = info.DiffAreaInfo();
            diffAreaBlock.AddMember("allocated_size", diffArea.AllocatedSize(), document.GetAllocator());
            diffAreaBlock.AddMember(
                "application_info_offset", diffArea.ApplicationInfoOffset(), document.GetAllocator());
            diffAreaBlock.AddMember("bitmap_offset", diffArea.FirstBitmapOffset(), document.GetAllocator());
            diffAreaBlock.AddMember("previous_bitmap_offset", diffArea.PreviousBitmapOffset(), document.GetAllocator());
            diffAreaBlock.AddMember("diff_area_offset", diffArea.FirstDiffAreaTableOffset(), document.GetAllocator());
            diffAreaBlock.AddMember(
                "diff_location_table_offset", diffArea.FirstDiffLocationTableOffset(), document.GetAllocator());
            diffAreaBlock.AddMember("frn", diffArea.Frn(), document.GetAllocator());

            {
                rapidjson::Value guid;
                guid.SetString(Orc::ToString(diffArea.Guid()).c_str(), document.GetAllocator());
                diffAreaBlock.AddMember("guid", guid, document.GetAllocator());
            }

            document.AddMember("diff_area", diffAreaBlock, document.GetAllocator());
        }

        {
            rapidjson::Value appInfoBlock(rapidjson::Type::kObjectType);

            {
                rapidjson::Value shadowCopyId;
                shadowCopyId.SetString(Orc::ToString(info.ShadowCopyId()).c_str(), document.GetAllocator());
                appInfoBlock.AddMember("shadow_copy_id", shadowCopyId, document.GetAllocator());
            }

            {
                rapidjson::Value shadowCopySetId;
                shadowCopySetId.SetString(Orc::ToString(info.ShadowCopySetId()).c_str(), document.GetAllocator());
                appInfoBlock.AddMember("shadow_copy_set_id", shadowCopySetId, document.GetAllocator());
            }

            {
                rapidjson::Value machine;
                machine.SetString(ToUtf8(info.MachineString()).c_str(), document.GetAllocator());
                appInfoBlock.AddMember("machine", machine, document.GetAllocator());
            }

            {
                rapidjson::Value service;
                service.SetString(ToUtf8(info.ServiceString()).c_str(), document.GetAllocator());
                appInfoBlock.AddMember("service", service, document.GetAllocator());
            }

            appInfoBlock.AddMember(
                "volume_snapshot_attributes",
                static_cast<std::underlying_type_t<VolumeSnapshotAttributes>>(info.VolumeSnapshotAttributes()),
                document.GetAllocator());

            appInfoBlock.AddMember(
                "snapshot_context",
                static_cast<std::underlying_type_t<VolumeSnapshotAttributes>>(info.SnapshotContext()),
                document.GetAllocator());

            document.AddMember("application_information", appInfoBlock, document.GetAllocator());
        }
    }

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    std::ofstream ofs(path, std::ios_base::binary);
    ofs << buffer.GetString();
}

void Dump(const std::string& path, const Snapshot& snapshot)
{
    rapidjson::Document document;
    document.SetObject();

    {
        rapidjson::Value cowArray(rapidjson::Type::kArrayType);

        std::vector<std::pair<Snapshot::BlockOffset, Snapshot::CopyOnWrite>> copyOnWrites;
        std::copy(
            std::cbegin(snapshot.CopyOnWrites()), std::cend(snapshot.CopyOnWrites()), std::back_inserter(copyOnWrites));
        std::sort(std::begin(copyOnWrites), std::end(copyOnWrites), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

        for (const auto& [offset, cow] : copyOnWrites)
        {
            rapidjson::Value jsonBlock(rapidjson::Type::kObjectType);

            jsonBlock.AddMember("offset", offset, document.GetAllocator());
            jsonBlock.AddMember("cow", cow.offset, document.GetAllocator());
            jsonBlock.AddMember("forward", cow.forward, document.GetAllocator());

            cowArray.PushBack(jsonBlock, document.GetAllocator());
        }

        document.AddMember("copy_on_writes", cowArray, document.GetAllocator());
    }

    {
        rapidjson::Value overlayArray(rapidjson::Type::kArrayType);

        std::vector<std::pair<Snapshot::BlockOffset, Snapshot::Overlay>> overlays;
        std::copy(std::cbegin(snapshot.Overlays()), std::cend(snapshot.Overlays()), std::back_inserter(overlays));
        std::sort(std::begin(overlays), std::end(overlays), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

        for (const auto& [offset, overlay] : overlays)
        {
            rapidjson::Value jsonBlock(rapidjson::Type::kObjectType);

            jsonBlock.AddMember("offset", offset, document.GetAllocator());
            jsonBlock.AddMember("overlay", overlay.offset, document.GetAllocator());
            jsonBlock.AddMember("bitmap", overlay.bitmap, document.GetAllocator());

            overlayArray.PushBack(jsonBlock, document.GetAllocator());
        }

        document.AddMember("overlays", overlayArray, document.GetAllocator());
    }

    {
        std::string bitmapHex;
        Text::ToHex(std::cbegin(snapshot.Bitmap()), std::cend(snapshot.Bitmap()), std::back_inserter(bitmapHex));

        rapidjson::Value bitmap(rapidjson::Type::kStringType);
        bitmap.SetString(bitmapHex.c_str(), document.GetAllocator());
        document.AddMember("bitmap", bitmap, document.GetAllocator());
    }

    {
        std::string bitmapHex;
        Text::ToHex(std::cbegin(snapshot.Bitmap()), std::cend(snapshot.Bitmap()), std::back_inserter(bitmapHex));

        rapidjson::Value previousBitmap(rapidjson::Type::kStringType);
        previousBitmap.SetString(bitmapHex.c_str(), document.GetAllocator());
        document.AddMember("previous_bitmap", previousBitmap, document.GetAllocator());
    }

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    std::ofstream ofs(path, std::ios_base::binary);
    ofs << buffer.GetString();
}

void Dump(const std::string& path, const ShadowCopy& vss)
{
    rapidjson::Document document;
    document.SetObject();
    rapidjson::Value blockArray(rapidjson::Type::kArrayType);

    std::vector<std::pair<ShadowCopy::BlockOffset, ShadowCopy::Block>> blocks;
    std::copy(std::cbegin(vss.Blocks()), std::cend(vss.Blocks()), std::back_inserter(blocks));
    std::sort(
        std::begin(blocks), std::end(blocks), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    for (const auto& [offset, block] : blocks)
    {
        rapidjson::Value jsonBlock(rapidjson::Type::kObjectType);

        {
            jsonBlock.AddMember("offset", offset, document.GetAllocator());
        }

        {
            if (block.m_copyOnWrite)
            {
                jsonBlock.AddMember("cow", block.m_copyOnWrite->offset, document.GetAllocator());
            }
            else
            {
                jsonBlock.AddMember("cow", rapidjson::Type::kNullType, document.GetAllocator());
            }
        }

        {
            if (block.m_overlay)
            {
                rapidjson::Value jsonOverlay(rapidjson::Type::kObjectType);
                jsonOverlay.AddMember("offset", block.m_overlay->offset, document.GetAllocator());
                jsonOverlay.AddMember("bitmap", block.m_overlay->bitmap, document.GetAllocator());

                jsonBlock.AddMember("overlay", jsonOverlay, document.GetAllocator());
            }
            else
            {
                jsonBlock.AddMember("overlay", rapidjson::Type::kNullType, document.GetAllocator());
            }
        }

        blockArray.PushBack(jsonBlock, document.GetAllocator());
    }

    document.AddMember("blocks", blockArray, document.GetAllocator());

    std::string bitmapHex;
    Text::ToHex(std::cbegin(vss.Bitmap()), std::cend(vss.Bitmap()), std::back_inserter(bitmapHex));

    rapidjson::Value bitmap(rapidjson::Type::kStringType);
    bitmap.SetString(bitmapHex.c_str(), document.GetAllocator());
    document.AddMember("bitmap", bitmap, document.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    std::ofstream ofs(path, std::ios_base::binary);
    ofs << buffer.GetString();
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
