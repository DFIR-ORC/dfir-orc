//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

namespace Orc {

class VolumeReader;

class CompleteVolumeReader;
class ImageReader;
class InterfaceReader;
class MountedVolumeReader;
class OfflineMFTReader;
class PhysicalDiskReader;
class SnapshotVolumeReader;
class SystemStorageVolumeReader;
class VHDVolumeReader;

class VolumeReaderVisitor
{
public:
    virtual void Visit(const VolumeReader& element) {}

    virtual void Visit(const CompleteVolumeReader& element) {}
    virtual void Visit(const ImageReader& element) {}
    virtual void Visit(const InterfaceReader& element) {}
    virtual void Visit(const MountedVolumeReader& element) {}
    virtual void Visit(const OfflineMFTReader& element) {}
    virtual void Visit(const PhysicalDiskReader& element) {}
    virtual void Visit(const SnapshotVolumeReader& element) {}
    virtual void Visit(const SystemStorageVolumeReader& element) {}
    virtual void Visit(const VHDVolumeReader& element) {}
};

}  // namespace Orc
