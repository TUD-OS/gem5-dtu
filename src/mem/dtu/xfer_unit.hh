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

class XferUnit
{
  private:

    struct Transfer
    {
        Dtu::NocPacketType type;
        Addr sourceAddr;
        NocAddr targetAddr;
        Addr size;
    };

    struct TransferEvent : public Event
    {
        XferUnit& xfer;

        Addr blockSize;

        Transfer trans;

        TransferEvent(XferUnit& _xfer, Addr _blockSize)
            : xfer(_xfer),
              blockSize(_blockSize),
              trans()
        {}

        void process() override;

        const char* description() const override { return "TransferEvent"; }

        const std::string name() const override { return xfer.dtu.name(); }
    };

  public:

    XferUnit(Dtu &_dtu, Addr _blockSize)
        : dtu(_dtu),
          blockSize(_blockSize),
          transferEvent(*this, _blockSize)
    {}

    void startTransfer(Dtu::NocPacketType type,
                       NocAddr targetAddr,
                       Addr sourceAddr,
                       Addr size);

    void continueTransfer()
    {
        transferEvent.process();
    }

    void sendToNoc(Dtu::NocPacketType type,
                   NocAddr targetAddr,
                   const void* data,
                   Addr size,
                   Tick spmPktHeaderDelay,
                   Tick spmPktPayloadDelay);

    void forwardToNoc(const void* data,
                      Addr size,
                      Tick spmPktHeaderDelay,
                      Tick spmPktPayloadDelay);

  private:

    Dtu &dtu;

    Addr blockSize;

    TransferEvent transferEvent;
};

#endif