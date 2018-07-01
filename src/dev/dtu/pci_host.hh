/*
 * Copyright (c) 2015 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DEV_DTU_PCI_HOST__
#define __DEV_DTU_PCI_HOST__

#include "dev/pci/host.hh"
#include "params/DtuPciHost.hh"

class DtuPciProxy;

/**
 * Implementation based on GenericPciHost.
 */
class DtuPciHost : public PciHost
{
  public:
    DtuPciHost(const DtuPciHostParams* p);
    virtual ~DtuPciHost();

  public: // PioDevice
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

    AddrRangeList getAddrRanges() const override;

  public: // PciHost
    Addr pioAddr(const PciBusAddr& bus_addr, Addr pci_addr) const override
    {
        return pciPioBase + pci_addr;
    }

    Addr memAddr(const PciBusAddr& bus_addr, Addr pci_addr) const override
    {
        return pciMemBase + pci_addr;
    }

    Addr dmaAddr(const PciBusAddr& bus_addr, Addr pci_addr) const override
    {
        return pciDmaBase + pci_addr;
    }

  protected: // Configuration address space handling
    /**
     * Decode a configuration space address.
     *
     *
     * @param addr Offset into the configuration space
     * @return Tuple containing the PCI bus address and an offset into
     *         the device's configuration space.
     */
    virtual std::pair<PciBusAddr, Addr> decodeAddress(Addr address);

  protected: // Interrupt handling
    void postInt(const PciBusAddr& addr, PciIntPin pin) override;
    void clearInt(const PciBusAddr& addr, PciIntPin pin) override;

  private:
    DtuPciProxy* pciProxy;

    const Addr confBase;
    const Addr confSize;
    const uint8_t confDeviceBits;

    const Addr pciPioBase;
    const Addr pciMemBase;
    const Addr pciDmaBase;
};

#endif // __DEV_DTU_PCI_HOST__