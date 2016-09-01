/*
 * Copyright (c) 2015, Christian Menard
 * Copyright (c) 2015, Nils Asmussen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#ifndef __MEM_DTU_XFER_UNIT_HH__
#define __MEM_DTU_XFER_UNIT_HH__

#include "mem/dtu/dtu.hh"
#include "mem/dtu/noc_addr.hh"

#include <list>
#include <vector>

class XferUnit
{
  public:

    enum XferFlags
    {
        MESSAGE   = 1,
        MSGRECV   = 2,
        NOPF      = 4,
    };

    enum AbortType
    {
        ABORT_LOCAL,
        ABORT_REMOTE,
        ABORT_ABORT,
    };

  private:

    struct Buffer;
    struct Translation;

    struct TransferEvent : public Event
    {
        XferUnit& xfer;

        Buffer *buf;

        uint64_t id;
        Cycles startCycle;
        Dtu::TransferType type;
        Addr localAddr;
        NocAddr remoteAddr;
        size_t size;
        PacketPtr pkt;
        uint vpeId;
        Dtu::MessageHeader* header;
        uint flags;
        Dtu::Error result;
        Translation *trans;

        TransferEvent(XferUnit& _xfer)
            : xfer(_xfer),
              buf(),
              id(nextId++),
              startCycle(),
              type(),
              localAddr(),
              remoteAddr(),
              size(),
              pkt(),
              vpeId(),
              header(),
              flags(),
              result(Dtu::Error::NONE),
              trans()
        {}

        void finish()
        {
            assert(trans == NULL);

            setFlags(AutoDelete);
        }

        bool isWrite() const
        {
            return type == Dtu::TransferType::REMOTE_WRITE ||
                   type == Dtu::TransferType::LOCAL_WRITE;
        }
        bool isRead() const
        {
            return !isWrite();
        }
        bool isRemote() const
        {
            return type == Dtu::TransferType::REMOTE_READ ||
                   type == Dtu::TransferType::REMOTE_WRITE;
        }

        void process() override;

        void tryStart();

        void translateDone(bool success, const NocAddr &phys);

        void abort(Dtu::Error error);

        const char* description() const override { return "TransferEvent"; }

        const std::string name() const override { return xfer.dtu.name(); }

        static uint64_t nextId;
    };

    struct Translation : PtUnit::Translation
    {
        TransferEvent& event;

        Translation(TransferEvent& _event)
            : event(_event)
        {}

        void abort()
        {
            event.xfer.dtu.abortTranslate(this);
        }

        void finished(bool success, const NocAddr &phys) override
        {
            event.translateDone(success, phys);

            delete this;
        }
    };

    struct Buffer
    {
        Buffer(int _id, size_t size)
            : id(_id),
              event(),
              bytes(new uint8_t[size]),
              offset()
        {
        }

        ~Buffer()
        {
            delete[] bytes;
        }

        int id;
        TransferEvent *event;
        uint8_t *bytes;
        size_t offset;
    };

  public:

    XferUnit(Dtu &_dtu, size_t _blockSize, size_t _bufCount, size_t _bufSize);

    ~XferUnit();

    void regStats();

    void startTransfer(Dtu::TransferType type,
                       NocAddr remoteAddr,
                       Addr localAddr,
                       size_t size,
                       PacketPtr pkt,
                       uint vpeId,
                       Dtu::MessageHeader* header,
                       Cycles delay,
                       uint flags);

    size_t abortTransfers(AbortType type, int coreId, bool all = false);

    void recvMemResponse(uint64_t evId,
                         const void* data,
                         size_t size,
                         Tick headerDelay,
                         Tick payloadDelay);

  private:

    Buffer *getBuffer(uint64_t id);

    Buffer* allocateBuf(TransferEvent *event, uint flags);

  private:

    Dtu &dtu;

    size_t blockSize;

    size_t bufCount;
    size_t bufSize;
    Buffer **bufs;

    std::list<int> abortReqs;
    std::list<TransferEvent*> queue;

    Stats::Histogram reads;
    Stats::Histogram writes;
    Stats::Histogram bytesRead;
    Stats::Histogram bytesWritten;
    Stats::Scalar delays;
    Stats::Scalar pagefaults;
    Stats::Scalar aborts;
};

#endif
