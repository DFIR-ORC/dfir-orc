//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

namespace Orc {

class ByteStream;
class FileStream;
class TemporaryStream;
class MemoryStream;
class CryptoHashStream;
class FuzzyHashStream;
class NTFSStream;
class UncompressNTFSStream;
class XORStream;
class ResourceStream;
class PipeStream;
class OnDiskChunkStream;
class EncodeMessageStream;
class JournalingStream;
class AccumulatingStream;
class TeeStream;

class ByteStreamVisitor
{
public:
    virtual void Visit(ByteStream& element) {}
    virtual void Visit(FileStream& element) {}
    virtual void Visit(TemporaryStream& element) {}
    virtual void Visit(MemoryStream& element) {}
    virtual void Visit(CryptoHashStream& element) {}
    virtual void Visit(FuzzyHashStream& element) {}
    virtual void Visit(NTFSStream& element) {}
    virtual void Visit(UncompressNTFSStream& element) {}
    virtual void Visit(XORStream& element) {}
    virtual void Visit(ResourceStream& element) {}
    virtual void Visit(PipeStream& element) {}
    virtual void Visit(OnDiskChunkStream& element) {}
    virtual void Visit(EncodeMessageStream& element) {}
    virtual void Visit(JournalingStream& element) {}
    virtual void Visit(AccumulatingStream& element) {}
    virtual void Visit(TeeStream& element) {}
};

}  // namespace Orc
