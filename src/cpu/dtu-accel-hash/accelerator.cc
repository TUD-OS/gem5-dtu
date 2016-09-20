/*
 * Copyright (c) 2016, Nils Asmussen
 * Copyright (c) 2015, Christian Menard
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

#include "cpu/dtu-accel-hash/algorithm.hh"
#include "cpu/dtu-accel-hash/accelerator.hh"
#include "debug/DtuAccel.hh"
#include "debug/DtuAccelAccess.hh"
#include "debug/DtuAccelState.hh"
#include "mem/dtu/dtu.hh"
#include "mem/dtu/regfile.hh"
#include "sim/dtu_memory.hh"

#include <iomanip>

static const unsigned EP_RECV       = 3;
static const size_t MSG_SIZE        = 64;
static const size_t BUF_SIZE        = 4096;
static const size_t CLIENTS         = 8;

static const char *stateNames[] =
{
    "IDLE",
    "READ_REP",
    "READ_MSG",
    "READ_DATA",
    "STORE_REPLY",
    "SEND_REPLY",
    "REPLY_WAIT",
    "ACK_MSG",
};

Addr
DtuAccelHash::getBufAddr(size_t id)
{
    // don't use the first page (m3's pager doesn't map it)
    return DtuTlb::PAGE_SIZE + BUF_SIZE * id;
}

Addr
DtuAccelHash::getRegAddr(DtuReg reg)
{
    return static_cast<Addr>(reg) * sizeof(RegFile::reg_t);
}

Addr
DtuAccelHash::getRegAddr(CmdReg reg)
{
    Addr result = sizeof(RegFile::reg_t) * numDtuRegs;

    result += static_cast<Addr>(reg) * sizeof(RegFile::reg_t);

    return result;
}

Addr
DtuAccelHash::getRegAddr(unsigned reg, unsigned epid)
{
    Addr result = sizeof(RegFile::reg_t) * (numDtuRegs + numCmdRegs);

    result += epid * numEpRegs * sizeof(RegFile::reg_t);

    result += reg * sizeof(RegFile::reg_t);

    return result;
}

bool
DtuAccelHash::CpuPort::recvTimingResp(PacketPtr pkt)
{
    dtutest.completeRequest(pkt);
    return true;
}

void
DtuAccelHash::CpuPort::recvReqRetry()
{
    dtutest.recvRetry();
}

DtuAccelHash::DtuAccelHash(const DtuAccelHashParams *p)
  : MemObject(p),
    system(p->system),
    tickEvent(this),
    port("port", this),
    state(State::READ_REP),
    algos(),
    chunkSize(system->cacheLineSize()),
    msgAddr(),
    masterId(system->getMasterId(name())),
    id(p->id),
    atomic(system->isAtomicMode()),
    reg_base(p->regfile_base_addr),
    retryPkt(nullptr)
{
    algos[static_cast<size_t>(Algorithm::SHA1)] =
        new DtuAccelHashAlgorithmSHA1();
    algos[static_cast<size_t>(Algorithm::SHA224)] =
        new DtuAccelHashAlgorithmSHA224();
    algos[static_cast<size_t>(Algorithm::SHA256)] =
        new DtuAccelHashAlgorithmSHA256();
    algos[static_cast<size_t>(Algorithm::SHA384)] =
        new DtuAccelHashAlgorithmSHA384();
    algos[static_cast<size_t>(Algorithm::SHA512)] =
        new DtuAccelHashAlgorithmSHA512();

    // kick things into action
    schedule(tickEvent, curTick());
}

BaseMasterPort &
DtuAccelHash::getMasterPort(const std::string& if_name, PortID idx)
{
    if (if_name == "port")
        return port;
    else
        return MemObject::getMasterPort(if_name, idx);
}

bool
DtuAccelHash::sendPkt(PacketPtr pkt)
{
    DPRINTF(DtuAccelAccess, "Send %s %s request at address 0x%x\n",
        atomic ? "atomic" : "timed",
        pkt->isWrite() ? "write" : "read",
        pkt->getAddr());

    if (atomic)
    {
        port.sendAtomic(pkt);
        completeRequest(pkt);
    }
    else if (!port.sendTimingReq(pkt))
    {
        retryPkt = pkt;
        return false;
    }

    return true;
}

void
DtuAccelHash::recvRetry()
{
    assert(retryPkt);
    if (port.sendTimingReq(retryPkt))
    {
        DPRINTF(DtuAccelAccess, "Proceeding after successful retry\n");

        retryPkt = nullptr;
    }
}

PacketPtr
DtuAccelHash::createPacket(Addr paddr,
                          size_t size,
                          MemCmd cmd = MemCmd::WriteReq)
{
    Request::Flags flags;

    auto req = new Request(paddr, size, flags, masterId);
    req->setThreadContext(id, 0);

    auto pkt = new Packet(req, cmd);
    auto pkt_data = new uint8_t[size];
    pkt->dataDynamic(pkt_data);

    return pkt;
}

PacketPtr
DtuAccelHash::createDtuRegisterPkt(Addr reg,
                                  RegFile::reg_t value,
                                  MemCmd cmd = MemCmd::WriteReq)
{
    auto pkt = createPacket(reg_base + reg, sizeof(RegFile::reg_t), cmd);
    *pkt->getPtr<RegFile::reg_t>() = value;
    return pkt;
}

void
DtuAccelHash::completeRequest(PacketPtr pkt)
{
    Request* req = pkt->req;

    DPRINTF(DtuAccelState, "[%s] Got response from memory\n",
        stateNames[static_cast<size_t>(state)]);

    DPRINTF(DtuAccelAccess, "Completing %s at address %x:%zu %s\n",
        pkt->isWrite() ? "write" : "read",
        req->getPaddr(),
        req->getSize(),
        pkt->isError() ? "error" : "success");

    const uint8_t *pkt_data = pkt->getConstPtr<uint8_t>();

    if (pkt->isError())
    {
        warn("%s access failed at %#x\n",
             pkt->isWrite() ? "Write" : "Read", req->getPaddr());
    }
    else
    {
        switch(state)
        {
            case State::IDLE:
            {
                assert(false);
                break;
            }

            case State::READ_REP:
            {
                const RegFile::reg_t *regs = pkt->getConstPtr<RegFile::reg_t>();
                // TODO we need to use GET_MSG
                if ((regs[0] & 0xFFFF) > 0)
                {
                    msgOffset = (regs[2] >> 16) & 0xFFFF;
                    msgAddr = regs[1] + msgOffset;
                    DPRINTF(DtuAccel, "Received message @ %p+%x\n",
                        msgAddr, msgOffset);
                    state = State::READ_MSG;
                }
                else
                    state = State::IDLE;
                break;
            }
            case State::READ_MSG:
            {
                const Dtu::MessageHeader *header =
                    pkt->getPtr<Dtu::MessageHeader>();
                const uint64_t *args =
                    reinterpret_cast<const uint64_t*>(
                        pkt_data + sizeof(Dtu::MessageHeader));

                DPRINTF(DtuAccel, "  label=%p algo=%d size=%p\n",
                    header->label, args[0], args[1]);
                dataAddr = getBufAddr(header->label);
                if (header->length != sizeof(uint64_t) * 2 ||
                    static_cast<Algorithm>(args[0]) >= Algorithm::COUNT ||
                    args[1] > BUF_SIZE ||
                    (args[1] % 64) != 0)
                {
                    reply.count = 0;
                    replyOffset = 0;
                    state = State::STORE_REPLY;
                }
                else
                {
                    algo = static_cast<Algorithm>(args[0]);
                    remSize = dataSize = args[1];
                    state = State::READ_DATA;
                }
                break;
            }
            case State::READ_DATA:
            {
                DtuAccelHashAlgorithm *al = algos[static_cast<size_t>(algo)];

                if (remSize == dataSize)
                    al->start();
                al->update(static_cast<const void*>(pkt_data), pkt->getSize());

                dataAddr += pkt->getSize();
                remSize -= pkt->getSize();

                if (remSize == 0)
                {
                    reply.count = al->get(reply.bytes);
                    assert(reply.count <= sizeof(reply.bytes));

                    std::ostringstream ss;
                    ss << std::hex;
                    for (size_t i = 0; i < reply.count; i++)
                    {
                        ss << std::setw(2) << std::setfill('0')
                           << (int)reply.bytes[i];
                    }
                    DPRINTF(DtuAccel, "Hash: %s\n", ss.str().c_str());

                    replyOffset = 0;
                    state = State::STORE_REPLY;
                }
                break;
            }
            case State::STORE_REPLY:
            {
                replyOffset += pkt->getSize();
                if (replyOffset == sizeof(uint64_t) + reply.count)
                    state = State::SEND_REPLY;
                break;
            }
            case State::SEND_REPLY:
            {
                state = State::REPLY_WAIT;
                break;
            }
            case State::REPLY_WAIT:
            {
                RegFile::reg_t reg =
                    *reinterpret_cast<const RegFile::reg_t*>(pkt_data);
                if ((reg & 0x7) == 0)
                    state = State::ACK_MSG;
                break;
            }
            case State::ACK_MSG:
            {
                state = State::READ_REP;
                break;
            }
        }
    }

    delete pkt->req;
    // the packet will delete the data
    delete pkt;

    // kick things into action again
    schedule(tickEvent, clockEdge(Cycles(1)));
}

void
DtuAccelHash::wakeup()
{
    if (state == State::IDLE)
    {
        state = State::READ_REP;
        schedule(tickEvent, clockEdge(Cycles(1)));
    }
}

void
DtuAccelHash::tick()
{
    PacketPtr pkt = nullptr;

    DPRINTF(DtuAccelState, "[%s] tick\n",
        stateNames[static_cast<size_t>(state)]);

    switch(state)
    {
        case State::IDLE:
        {
            break;
        }
        case State::READ_REP:
        {
            pkt = createPacket(reg_base + getRegAddr(0, EP_RECV),
                               sizeof(RegFile::reg_t) * 3,
                               MemCmd::ReadReq);
            break;
        }
        case State::READ_MSG:
        {
            pkt = createPacket(msgAddr, MSG_SIZE, MemCmd::ReadReq);
            break;
        }
        case State::READ_DATA:
        {
            size_t size = std::min(chunkSize, remSize);
            pkt = createPacket(dataAddr, size, MemCmd::ReadReq);
            break;
        }
        case State::STORE_REPLY:
        {
            size_t rem = sizeof(uint64_t) + reply.count - replyOffset;
            size_t size = std::min(chunkSize, rem);
            pkt = createPacket(getBufAddr(CLIENTS) + replyOffset,
                               size,
                               MemCmd::WriteReq);
            memcpy(pkt->getPtr<uint8_t>(), (char*)&reply + replyOffset, size);
            break;
        }
        case State::SEND_REPLY:
        {
            static_assert(static_cast<int>(CmdReg::COMMAND) == 0, "");
            static_assert(static_cast<int>(CmdReg::ABORT) == 1, "");
            static_assert(static_cast<int>(CmdReg::DATA_ADDR) == 2, "");
            static_assert(static_cast<int>(CmdReg::DATA_SIZE) == 3, "");
            static_assert(static_cast<int>(CmdReg::OFFSET) == 4, "");

            pkt = createPacket(reg_base + getRegAddr(CmdReg::COMMAND),
                               sizeof(RegFile::reg_t) * 5,
                               MemCmd::WriteReq);

            RegFile::reg_t *regs = pkt->getPtr<RegFile::reg_t>();
            regs[0] = Dtu::Command::REPLY | (EP_RECV << 3);
            regs[1] = 0;
            regs[2] = getBufAddr(CLIENTS);
            regs[3] = sizeof(uint64_t) + reply.count;
            regs[4] = msgOffset;
            break;
        }
        case State::REPLY_WAIT:
        {
            Addr regAddr = getRegAddr(CmdReg::COMMAND);
            pkt = createDtuRegisterPkt(regAddr, 0, MemCmd::ReadReq);
            break;
        }
        case State::ACK_MSG:
        {
            // TODO this is currently broken (OFFSET needs to be written)
            Addr regAddr = getRegAddr(CmdReg::COMMAND);
            uint64_t val = Dtu::Command::ACK_MSG | (EP_RECV << 3);
            pkt = createDtuRegisterPkt(regAddr, val, MemCmd::WriteReq);
            break;
        }
    }

    if (pkt != nullptr)
        sendPkt(pkt);
}

DtuAccelHash*
DtuAccelHashParams::create()
{
    return new DtuAccelHash(this);
}