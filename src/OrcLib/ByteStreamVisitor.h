//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
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
    virtual void Visit(ByteStream&) {}
    virtual void Visit(FileStream&) {}
    virtual void Visit(TemporaryStream&) {}
    virtual void Visit(MemoryStream&) {}
    virtual void Visit(CryptoHashStream&) {}
    virtual void Visit(FuzzyHashStream&) {}
    virtual void Visit(NTFSStream&) {}
    virtual void Visit(UncompressNTFSStream&) {}
    virtual void Visit(ResourceStream&) {}
    virtual void Visit(PipeStream&) {}
    virtual void Visit(OnDiskChunkStream&) {}
    virtual void Visit(EncodeMessageStream&) {}
    virtual void Visit(JournalingStream&) {}
    virtual void Visit(AccumulatingStream&) {}
    virtual void Visit(TeeStream&) {}
};

}  // namespace Orc
