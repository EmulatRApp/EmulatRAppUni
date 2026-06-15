# SRM Firmware Implementation Guide

## ASA-EMulatR — Alpha AXP Architecture Emulator
### Interface Documentation: MMIOManager to PuTTY / PCI-ISA

**Project Architect:** Timothy Peer  
**Date:** 2026-03-14 (Rev 2)  
**Status:** Phase 3-5 Implementation Reference

---

## 1. Architecture Overview

The SRM firmware runs on the emulated Alpha CPU. When it accesses hardware,
the physical address flows through a layered dispatch chain that routes each
access to the correct device model. Two primary paths exist:

**Path A: Console Output (MMIOManager → PuTTY)**

Firmware writes a character to the UART transmit register. The byte flows
through the chipset, ISA bridge, and UART model to a TCP socket connected
to PuTTY.

**Path B: PCI Device Access (MMIOManager → PCI Config/IO → ISA Devices)**

Firmware reads PCI configuration space to discover devices, then accesses
device registers through PCI I/O or memory windows.

Both paths share the same entry point: GuestMemory routes the physical
address to MMIOManager, which dispatches to the Tsunami Pchip.

---

## 2. Dispatch Model: Function Pointers, Not VTables

### 2.1 Design Decision

All device dispatch in the emulator uses **function-pointer-plus-context**
rather than C++ virtual interfaces. This matches the pattern already
established by MMIOManager and eliminates vtable overhead on the hot path.

### 2.2 Rationale

The UART polling loop is the hottest I/O path during SRM operation. Each
`>>>` prompt character involves a LSR read (check TX ready), a THR write
(transmit byte), and potentially an RBR read (check for input). These
accesses traverse three dispatch layers (Pchip → ISA Bridge → UART).

Performance comparison per dispatch call:

| Method | Mechanism | Cost (predicted) | Cost (mispredicted) |
|--------|-----------|-----------------|-------------------|
| Virtual interface | vtable load + indirect call | 2-5ns | 15-25ns |
| Thin adapter | 2x vtable dispatch | 4-10ns | 30-50ns |
| Function pointer | inline load + indirect call | 1-3ns | 10-15ns |

During PCI enumeration (32 device slots), vtable branch prediction thrashes
across different device types. Function pointers stored inline in the
registration struct are already in cache from the lookup, reducing the
mispredict penalty.

### 2.3 Pattern

All dispatch registrations follow this structure:

```cpp
struct DeviceEntry {
    void*   ctx;                                              // device instance
    RetType (*readFn)(void* ctx, ArgTypes...);                // read handler
    void    (*writeFn)(void* ctx, ArgTypes..., ValueType);    // write handler
};
```

The device provides static functions that cast `ctx` back to the concrete type:

```cpp
static quint64 mmioRead(void* ctx, quint64 offset, quint8 width) noexcept {
    return static_cast<MyDevice*>(ctx)->read(offset, width);
}
```

This pattern is used at every dispatch boundary:

| Layer | Registration Struct | Context |
|-------|-------------------|---------|
| MMIOManager → Chip component | `Handlers { ctx, read, write }` | Cchip/Dchip/Pchip* |
| Pchip → PCI device | `PciDeviceEntry { ctx, configRead, configWrite }` | IsaBridge* |
| Pchip → I/O port handler | `IoPortEntry { startPort, endPort, ctx, ioRead, ioWrite }` | IsaBridge* |
| ISA Bridge → ISA device | `IsaDeviceEntry { startPort, endPort, ctx, ioRead, ioWrite }` | Uart16550* |

### 2.4 Consistency with Existing Code

MMIOManager already uses this pattern for all MMIO regions. The Tsunami
chipset, Pchip, ISA bridge, and UART all follow the same convention.
No virtual interfaces exist in the dispatch chain.

---

## 3. Complete Dispatch Chain

```
+------------------------------------------------------------------+
|                        Alpha CPU                                  |
|  HW_LD / HW_ST / LDQ / STQ with physical address                |
+------------------------------------------------------------------+
        |
        | Physical Address (PA)
        v
+------------------------------------------------------------------+
|                      GuestMemory                                  |
|  findRoute(PA) → Route 1 (RAM) or Route 3 (Tsunami CSR)         |
|                                                                   |
|  Route 1: PA < RAM ceiling        → SafeMemory                  |
|  Route 2: PA in PCI BAR range     → MMIOManager                 |
|  Route 3: PA in 0x8018000000 range → MMIOManager                |
+------------------------------------------------------------------+
        |
        | Route 3: PA in Tsunami CSR space
        v
+------------------------------------------------------------------+
|                     MMIOManager                                   |
|  findRegion(PA) → binary search sorted region list               |
|                                                                   |
|  Region: Pchip  0x8018000000 - 0x801A000000                     |
|  Region: Cchip  0x801A000000 - 0x801B000000                     |
|  Region: Dchip  0x801B000000 - 0x801C000000                     |
|                                                                   |
|  Calls: handlers.read(ctx, offset, width) → quint64              |
|  Calls: handlers.write(ctx, offset, width, value)                |
+------------------------------------------------------------------+
        |                    |                    |
        v                    v                    v
   +-----------+      +-----------+        +-----------+
   |  Pchip    |      |  Cchip    |        |  Dchip    |
   |  CSR +    |      |  SysConf  |        |  DRAM     |
   |  PCI +    |      |  IRQ Mask |        |  Config   |
   |  I/O      |      |  IPI      |        |  Revision |
   +-----------+      +-----------+        +-----------+
        |
        | Pchip internal dispatch (by offset sub-range)
        |
   +----+----------+-----------+
   |                |           |
   v                v           v
CSR Regs     PCI Config    PCI I/O
(WSBA,WSM)   Type 0        Dense
             Decode        Decode
                |           |
                v           v
          +-----------+  +-----------+
          | PCI Device|  | I/O Port  |
          | Registry  |  | Registry  |
          | (fn ptr)  |  | (fn ptr)  |
          +-----------+  +-----------+
                |           |
                v           v
          +----------+  +----------+
          |ISA Bridge|  |ISA Bridge|
          |cfg header|  |I/O route |
          +----------+  +----------+
                           |
                    +------+------+
                    v             v
              +---------+   +---------+
              | UART    |   | UART    |
              | COM1    |   | COM2    |
              | (OPA0)  |   | (OPA1)  |
              +---------+   +---------+
                    |             |
                    v             v
              +---------+   +---------+
              |SRMConsole|  |SRMConsole|
              |Device   |   |Device   |
              |TCP:5555 |   |TCP:5678 |
              +---------+   +---------+
                    |             |
                    v             v
              +---------+   +---------+
              | PuTTY   |   | PuTTY   |
              | (OPA0)  |   | (OPA1)  |
              +---------+   +---------+
```

---

## 4. Interface Specifications

### 4.1 GuestMemory → MMIOManager

**File:** `memoryLib/GuestMemory.h`, `memoryLib/GuestMemory.cpp`

GuestMemory is the single PA router. It classifies every physical address
into a route target and dispatches to the appropriate subsystem.

**Route Table (configured in initDefaultPARoutes):**

| Route | PA Range | Target | Source |
|-------|----------|--------|--------|
| 1 | 0x0 to RAM ceiling | SafeMemory | INI: RamBase + RamSize |
| 2 | MmioBase to MmioBase+MmioSize | MMIOManager | INI: MmioBase, MmioSize |
| 3 | TsunamiBase to TsunamiBase+TsunamiSize | MMIOManager | INI: TsunamiBase, TsunamiSize |

**INI Configuration ([MemoryMap]):**

```ini
TsunamiBase=0x8018000000
TsunamiSize=0x0008000000
```

**Dispatch Functions:**

```cpp
MEM_STATUS GuestMemory::readRouted(quint64 pa, quint8 width,
                                    quint64& outValue, AccessKind kind)
{
    const PARouteEntry* route = findRoute(pa);
    if (!route) return MEM_STATUS::AccessViolation;

    switch (route->target) {
    case RouteTarget::SafeMemory:
        return m_safeMem->load(offset, width, outValue);
    case RouteTarget::MMIOManager:
        return m_mmio->handleRead(pa, width, outValue);
    }
}
```

**Contract:**
- GuestMemory classifies PA exactly once
- MMIO reads never return from instruction fetch (enforced)
- Route lookup is linear scan (2-3 routes, fast enough)

---

### 4.2 MMIOManager → Device Handlers

**File:** `mmioLib/mmio_Manager.h`, `mmioLib/mmio_Manager.cpp`

MMIOManager holds a sorted list of MMIO regions. Each region maps a PA
range to a device handler through function pointers.

**Region Registration:**

```cpp
struct RegionDescriptor {
    quint64 basePA;
    quint64 sizeBytes;
    quint32 flags;          // WIDTH_32 | WIDTH_64 | alignment
    quint32 deviceUid;      // device identifier for diagnostics
    quint8  hoseId;         // PCI hose (0 for Tsunami primary)
};

struct Handlers {
    void*    ctx;           // opaque device pointer (this)
    ReadFn   read;          // quint64 (*)(void* ctx, quint64 offset, quint8 width)
    WriteFn  write;         // void (*)(void* ctx, quint64 offset, quint64 value, quint8 width)
};

bool registerRegion(const RegionDescriptor& desc, const Handlers& handlers);
void finalize();            // sort regions for binary search
```

**Registered Regions (after TsunamiChipset::registerWithMMIO):**

| Region | Base PA | Size | Device UID | Handler |
|--------|---------|------|------------|---------|
| Pchip0 | 0x8018000000 | 0x200000000 | 0x8018 | TsunamiPchip::mmioRead/Write |
| Cchip | 0x801A0000000 | 0x10000000 | 0x801A | TsunamiCchip::mmioRead/Write |
| Dchip | 0x801B0000000 | 0x10000000 | 0x801B | TsunamiDchip::mmioRead/Write |

**Dispatch:**

```cpp
MEM_STATUS MMIOManager::handleRead(quint64 pa, quint8 width, quint64& value)
{
    const Region* r = findRegion(pa);       // binary search after finalize()
    if (!r) return MEM_STATUS::AccessViolation;

    const quint64 offset = pa - r->basePA;
    value = r->handlers.read(r->handlers.ctx, offset, width);
    return MEM_STATUS::Ok;
}
```

**Contract:**
- Regions must not overlap (checked at registration)
- finalize() must be called before first access
- Handler receives offset relative to region basePA, not absolute PA
- Handler ctx is the device instance pointer (cast from void*)

---

### 4.3 MMIOManager → TsunamiPchip (Internal Dispatch)

**File:** `TsunamiPchip.h`

The Pchip occupies a large PA range and internally dispatches based on
offset sub-ranges within its address space.

**Pchip Address Space Layout:**

| Offset Range | Size | Function |
|-------------|------|----------|
| 0x00000000 - 0x00FFFFFF | 16 MB | CSR Registers (WSBA, WSM, TBA, PCTL) |
| 0x01000000 - 0x017FFFFF | 8 MB | PCI Sparse I/O Space |
| 0x01800000 - 0x01FFFFFF | 8 MB | PCI Dense I/O Space |
| 0x02000000 - 0x027FFFFF | 8 MB | PCI Type 0 Config Space |
| 0x02800000 - 0x02FFFFFF | 8 MB | PCI Type 1 Config Space |
| 0x03000000 - 0x037FFFFF | 8 MB | PCI Sparse Memory Space |
| 0x03800000 - 0x03FFFFFF | 8 MB | PCI Dense Memory Space |

**Internal Dispatch (TsunamiPchip::read):**

```cpp
quint64 TsunamiPchip::read(quint64 offset, quint8 width) const
{
    if (offset < kCSRSize)
        return readCSR(offset);

    if (offset >= kCfgType0Offset && offset < kCfgType0Offset + 0x800000)
        return readPciConfig0(offset - kCfgType0Offset, width);

    if (offset >= kIODenseOffset && offset < kIODenseOffset + 0x800000)
        return readIoPort(port_from_offset, width);

    return 0;   // unmapped sub-range
}
```

---

### 4.4 Pchip → PCI Configuration Space

**File:** `TsunamiPchip.h`

PCI config space accesses arrive as offsets within the Type 0 config
region. The Pchip decodes bus/device/function/register from the offset
and dispatches to registered PCI device handlers via function pointers.

**Type 0 Config Address Decode:**

```
Offset within Type 0 region:
    Bits [15:11] = Device number (0-31, IDSEL select)
    Bits [10:8]  = Function number (0-7)
    Bits [7:0]   = Register offset (4-byte aligned)

Example: Read vendor ID of device 1, function 0
    Offset = (1 << 11) | (0 << 8) | 0x00 = 0x0800
    PA = Pchip_Base + kCfgType0Offset + 0x0800
       = 0x8018000000 + 0x02000000 + 0x0800
       = 0x801A000800
```

**PCI Device Registration (function pointer pattern):**

```cpp
struct PciDeviceEntry {
    void*   ctx;
    quint32 (*configRead)(void* ctx, quint8 reg, quint8 width);
    void    (*configWrite)(void* ctx, quint8 reg, quint32 value, quint8 width);
};

void TsunamiPchip::registerPciDevice(
    quint8 bus, quint8 device, quint8 function,
    void* ctx,
    quint32 (*configRead)(void*, quint8, quint8),
    void (*configWrite)(void*, quint8, quint32, quint8));
```

**Device Map (ES40 configuration):**

| Bus | Device | Function | Context | Identity |
|-----|--------|----------|---------|----------|
| 0 | 1 | 0 | IsaBridge* | ALi M1533 (0x10B9:0x1533) |
| 0 | all others | - | (none) | Returns 0xFFFFFFFF |

**Empty Slot Behavior:**

When the firmware reads config space for an unregistered bus/device/function,
the Pchip returns 0xFFFFFFFF. This is the PCI standard "no device present"
response.

---

### 4.5 Pchip → PCI I/O Space → ISA Bridge

**File:** `TsunamiPchip.h`, `IsaBridge.h`

PCI I/O space accesses arrive as offsets within the Dense I/O region.
The Pchip extracts the port address and dispatches to registered I/O
port handlers via function pointers.

**I/O Port Address Decode:**

```
Offset within Dense I/O region:
    Port address = offset & 0xFFFF

Example: Access COM1 THR (port 0x3F8)
    Offset = kIODenseOffset + 0x3F8
    PA = Pchip_Base + 0x01800000 + 0x3F8
       = 0x8018000000 + 0x018003F8
       = 0x80198003F8
```

**I/O Port Registration (function pointer pattern):**

```cpp
struct IoPortEntry {
    quint16 startPort;
    quint16 endPort;        // exclusive
    void*   ctx;
    quint64 (*ioRead)(void* ctx, quint16 port, quint8 width);
    void    (*ioWrite)(void* ctx, quint16 port, quint64 value, quint8 width);
};
```

**ISA Bridge static registration functions:**

```cpp
static quint64 ioReadStatic(void* ctx, quint16 port, quint8 width) {
    return static_cast<IsaBridge*>(ctx)->ioRead(port, width);
}

static void ioWriteStatic(void* ctx, quint16 port, quint64 value, quint8 width) {
    static_cast<IsaBridge*>(ctx)->ioWrite(port, value, width);
}
```

**I/O Port Routing (two-level dispatch):**

The ISA bridge is registered with the Pchip as the handler for the
entire legacy ISA I/O range (0x000-0xFFF). The Pchip dispatches to
the ISA bridge, which then routes to the specific ISA device.

```
Pchip IoPortEntry.ioRead(ctx=isaBridge, port, width)
    → IsaBridge searches m_isaDevices for port range
        → IsaDeviceEntry.ioRead(ctx=uart, port, width)
            → Uart16550::readRegister(reg)
```

All three hops are direct function pointer calls. No vtable indirection.

**Registered ISA Devices:**

| Port Range | Device | Handler |
|-----------|--------|---------|
| 0x3F8 - 0x3FF | COM1 / OPA0 | Uart16550 |
| 0x2F8 - 0x2FF | COM2 / OPA1 | Uart16550 |
| 0x070 - 0x071 | RTC | (future) |
| 0x060 - 0x064 | Keyboard | (future) |
| all others | (none) | Returns 0xFF (ISA bus float) |

---

### 4.6 ISA Bridge → UART 16550

**File:** `Uart16550.h`

The UART receives I/O port reads and writes from the ISA bridge,
translates them to register operations, and connects to an
IConsoleDevice backend for actual I/O.

**UART Register Map (8 registers, offset from base port):**

| Offset | DLAB=0 Read | DLAB=0 Write | DLAB=1 Read | DLAB=1 Write |
|--------|-------------|--------------|-------------|--------------|
| +0 | RBR (receive) | THR (transmit) | DLL | DLL |
| +1 | IER | IER | DLM | DLM |
| +2 | IIR | FCR | IIR | FCR |
| +3 | LCR | LCR | LCR | LCR |
| +4 | MCR | MCR | MCR | MCR |
| +5 | LSR | (ignored) | LSR | (ignored) |
| +6 | MSR | (ignored) | MSR | (ignored) |
| +7 | SCR | SCR | SCR | SCR |

DLAB is bit 7 of LCR. When set, offsets +0 and +1 access the divisor
latch (baud rate configuration) instead of the data registers.

**Key Register Behaviors:**

**THR (Transmit Holding Register) — write to offset +0, DLAB=0:**
- Sends one byte to IConsoleDevice backend
- Backend pushes byte over TCP to PuTTY
- Always succeeds immediately (TCP is buffered)

**RBR (Receive Buffer Register) — read from offset +0, DLAB=0:**
- Pulls one byte from IConsoleDevice backend (non-blocking)
- Backend reads from TCP receive queue
- Returns 0x00 if no data available

**LSR (Line Status Register) — read from offset +5:**
- Bit 0 (DR): 1 if backend has input queued
- Bit 5 (THRE): always 1 (TX always ready)
- Bit 6 (TEMT): always 1 (transmitter always idle)
- All error bits: always 0

**IIR (Interrupt Identification Register) — read from offset +2:**
- Bit 0: always 1 (no interrupt pending, polled mode)
- Bits 7:6: 0xC0 if FIFOs enabled (from FCR bit 0)

**FCR (FIFO Control Register) — write to offset +2:**
- Bit 0: enable FIFOs (affects IIR bits 7:6)
- Bit 1: reset RX FIFO (self-clearing, no-op in emulation)
- Bit 2: reset TX FIFO (self-clearing, no-op in emulation)

**Firmware Init Sequence (typical SRM UART setup):**

```
1. Write LCR = 0x80           (set DLAB to access divisor)
2. Write DLL = 0x01           (divisor low byte)
3. Write DLM = 0x00           (divisor high byte — 115200 baud)
4. Write LCR = 0x03           (clear DLAB, 8 data bits, 1 stop, no parity)
5. Write FCR = 0x07           (enable FIFOs, reset both)
6. Write MCR = 0x0B           (DTR + RTS + interrupt enable)
7. Write IER = 0x00           (disable all interrupts — polled mode)
8. Read  LSR                  (check status)
9. Write THR = 0x41           (transmit 'A' — first character!)
```

---

### 4.7 UART 16550 → IConsoleDevice (Backend Interface)

**File:** `deviceLib/IConsoleDevice.h`

The UART model does not implement I/O transport. It delegates all
actual byte transfer to an IConsoleDevice backend through a simple
byte-at-a-time interface.

**IConsoleDevice Interface (relevant methods):**

```cpp
class IConsoleDevice {
public:
    virtual int getChar(bool blocking, quint32 timeoutMs) = 0;
    virtual void putChar(quint8 ch) = 0;
    virtual bool hasInput() const = 0;
    virtual bool isConnected() const = 0;
};
```

**UART-to-Backend Wiring:**

| UART Operation | Backend Call | Direction |
|---------------|-------------|-----------|
| THR write | putChar(byte) | UART → Backend → TCP → PuTTY |
| RBR read | getChar(false, 0) | PuTTY → TCP → Backend → UART |
| LSR bit 0 | hasInput() | PuTTY → TCP → Backend → UART |
| MSR bit 7 | isConnected() | Check TCP connection state |

---

### 4.8 IConsoleDevice → SRMConsoleDevice → TCP → PuTTY

**File:** `deviceLib/SRMConsoleDevice.h`

SRMConsoleDevice implements IConsoleDevice using a QTcpServer. It listens
on a configured port and accepts one client connection (PuTTY).

**Transport Architecture:**

```
SRMConsoleDevice
    +-- QTcpServer (listening)
    |       +-- onNewConnection() → accept QTcpSocket
    +-- QTcpSocket (connected client)
    |       +-- onReadyRead() → bytes enqueued to m_rxQueue
    |       +-- write(data) → bytes sent to client
    +-- QQueue<quint8> m_rxQueue (receive buffer)
    |       +-- getChar() dequeues front byte
    |       +-- hasInput() checks !isEmpty()
    +-- QMutex m_mutex (thread safety)
    +-- QWaitCondition m_rxCondition (blocking reads)
```

**Configuration (from INI [Device.OPA0]):**

```ini
[Device.OPA0]
classType=UART
iface=Net
iface_port=5555
application=putty -raw localhost 5555
```

**PuTTY Connection:**
- Mode: RAW (not telnet, not SSH)
- Host: localhost
- Port: 5555 (OPA0) or 5678 (OPA1)
- No protocol negotiation, pure byte stream

**putChar Flow (firmware → PuTTY):**

```
SRM firmware writes THR register
    → Uart16550::writeTHR(byte)
        → IConsoleDevice::putChar(byte)
            → SRMConsoleDevice::putChar(byte)
                → QTcpSocket::write(&byte, 1)
                    → TCP packet to PuTTY
                        → PuTTY renders character
```

**getChar Flow (PuTTY → firmware):**

```
User types key in PuTTY
    → TCP packet to SRMConsoleDevice
        → onReadyRead() slot fires
            → bytes appended to m_rxQueue
                → m_rxCondition.wakeAll()

SRM firmware reads LSR register
    → Uart16550::readLSR()
        → IConsoleDevice::hasInput()
            → m_rxQueue.isEmpty() ? 0 : LSR_DR

SRM firmware reads RBR register
    → Uart16550::readRBR()
        → IConsoleDevice::getChar(false, 0)
            → m_rxQueue.dequeue()
                → byte returned to firmware
```

---

## 5. Coherency, Ordering, and Blocking Model

### 5.1 Memory Ordering

The Alpha EV6 is weakly ordered. A store to RAM followed by a store to
an MMIO register can arrive out of order on real hardware. Firmware uses
`MB` and `WMB` instructions to enforce ordering when needed.

In the emulator with single-threaded per-CPU execution, **ordering is
naturally sequential**. When CPU 0 executes a store to RAM followed by a
store to MMIO, both go through `GuestMemory::writeRouted()` in program
order. There is no reorder buffer or store queue that could deliver them
out of sequence.

**When it becomes relevant:**

| Scenario | Mechanism | Status |
|----------|-----------|--------|
| Multi-CPU shared memory | WriteBufferManager flush on MB | Infrastructure exists |
| Device DMA reads guest RAM | DMACoherencyManager | Infrastructure exists |
| Single CPU polled I/O | Naturally sequential | No action needed |

For the >>> milestone, none of these scenarios apply.

### 5.2 Blocking Guarantees

All static function pointer dispatch handlers execute **synchronously**
on the calling CPU's thread. The full chain from Pchip through ISA bridge
through UART to putChar() runs to completion before returning to the CPU's
instruction execution loop.

This is architecturally correct — on real hardware, an uncacheable MMIO
store stalls the CPU until the chipset acknowledges it.

**Critical constraint: no blocking reads in the dispatch chain.**

The UART's `readRBR()` calls `getChar(false, 0)` — non-blocking, returns
-1 immediately if the TCP receive queue is empty. The firmware always
checks LSR before reading RBR (standard polled UART protocol), so a
blocking call never occurs.

**Blocking scenarios to avoid:**

| Call | Blocking? | Safe? | Why |
|------|-----------|-------|-----|
| `getChar(false, 0)` | No | Yes | Returns immediately |
| `getChar(true, timeout)` | Yes | **DANGEROUS** | Stalls CPU thread |
| `putChar(byte)` | No* | Yes | TCP buffered, returns immediately |
| `hasInput()` | No | Yes | Queue check only |

*putChar can technically block if TCP send buffer is full, but this would
require PuTTY to stop reading — practically impossible during normal use.

### 5.3 Interrupt-Driven I/O (Future)

When interrupt-driven UART mode is enabled (IER bit 0 set), the UART
must signal the Cchip DRIR when TCP data arrives. This crosses threads:

```
Qt event loop thread:
    SRMConsoleDevice::onReadyRead()
        → enqueue bytes to m_rxQueue
        → cchip.assertInterrupt(uartBit)     // atomic fetch_or on DRIR

CPU execution thread:
    AlphaCPU::checkInterrupts()
        → cchip.readDIR(cpuId)               // atomic DRIR & DIM[N]
        → if non-zero, take device interrupt
```

The `std::atomic<quint64>` on DRIR and DIM handles cross-thread safety.
No locks in the interrupt path. The CPU polls at instruction granularity;
the device writes atomically. No blocking anywhere.

---

## 6. ControllersLib Integration Path

### 6.1 Layer Separation

Today's interfaces (TsunamiPchip, IsaBridge, Uart16550) are the
**hardware dispatch layer** — they represent what the Tsunami chipset
physically does when a PA arrives.

The controllersLib (ISP1020_Controller, PciScsiControllerTemplate,
GenericScsiHostAdapter) is the **device model layer** — it represents
what happens inside a PCI device after the chipset delivers an access.

The relationship is hierarchical:

```
GuestMemory → MMIOManager → TsunamiPchip    (hardware dispatch)
                                |
                                v
                          PciDeviceEntry     (function pointer bridge)
                                |
                                v
                          ISP1020_Controller (device model)
                                |
                                v
                          GenericScsiHostAdapter → VirtualScsiDevice
```

### 6.2 Existing ControllersLib Capabilities

| Component | Purpose | Status |
|-----------|---------|--------|
| PciConfigSpace | Vendor/device/class/BAR struct | Complete |
| PciBarDescriptor | BAR base/size/type | Complete |
| PciScsiControllerTemplate | Register bank, mailbox, DMA, IRQ | Complete |
| ISP1020_Controller | QLogic 0x1077:0x1020 identity | Complete (skeleton) |
| PciScsiDeviceShell | Composite: PCI config + MMIO + SCSI | Complete |
| GenericScsiHostAdapter | Command routing to VirtualScsiDevice | Complete |
| PciScsiDmaChannel/Engine | DMA transfer modeling | Complete (skeleton) |
| PciScsiInterruptController | IRQ status/mask management | Complete |
| PciScsiMailbox | Mailbox command queue | Complete |

### 6.3 Connection Point

The controllersLib has its own `PciConfigSpace` struct. The Tsunami Pchip
dispatch uses function pointers. The bridge between them is a pair of
static functions on the controller:

```cpp
// In ISP1020_Controller or a wrapper:
static quint32 pciConfigReadStatic(void* ctx, quint8 reg, quint8 width) {
    auto* ctrl = static_cast<ISP1020_Controller*>(ctx);
    return ctrl->configSpaceRead(reg, width);
}

static void pciConfigWriteStatic(void* ctx, quint8 reg, quint32 value, quint8 width) {
    auto* ctrl = static_cast<ISP1020_Controller*>(ctx);
    ctrl->configSpaceWrite(reg, value, width);
}
```

Registration with Pchip:

```cpp
m_tsunami->pchip().registerPciDevice(
    0, 3, 0,                           // bus, device, function
    isp1020Ptr,                        // context
    ISP1020_Controller::pciConfigReadStatic,
    ISP1020_Controller::pciConfigWriteStatic);
```

### 6.4 MMIO BAR Access Path (Post-Boot)

After PCI enumeration, the firmware writes BAR addresses into the
controller's PCI config space. When the guest OS later accesses that
BAR range, the access must route through MMIOManager to the controller's
register bank.

This requires dynamic MMIO region registration when the firmware writes
a BAR. The Pchip (or a PCI resource manager) detects the BAR write in
pciConfigWrite, extracts the assigned address, and registers a new MMIO
region with MMIOManager pointing to the controller's MMIO read/write
handlers.

This is a post->>> concern. For PCI enumeration, only config space
read/write is needed.

### 6.5 SCSI Command Path (Post-Boot)

The full runtime path from guest BOOT command to disk I/O:

```
Guest OS writes I/O descriptor to HBA mailbox register
    → ISP1020_Controller::decodeMailboxDoorbell(value)
        → ScsiCommand populated from guest CDB
            → GenericScsiHostAdapter::submitCommand(cmd)
                → ScsiControllerKzpba::resolve(target, lun)
                    → VirtualScsiDevice::handleCommand(cmd)
                        → VirtualScsiBackend::read/write
                            → QIODeviceBackend (host file)
```

Completion flows back:

```
VirtualScsiDevice updates cmd.status, cmd.dataTransferred
    → ISP1020_Controller posts completion to guest completion queue
        → ISP1020_Controller::raiseInterrupt()
            → cchip.assertInterrupt(scsiIrqBit)
                → CPU takes device interrupt
                    → Guest OS reads completion queue
```

### 6.6 Timeline

| Phase | What | When |
|-------|------|------|
| PCI config header only | Firmware reads vendor/device ID | Phase 4 (pre->>>) |
| Full MMIO register bank | Firmware configures BARs | Post->>> |
| SCSI command execution | Guest issues BOOT or OS does I/O | Post-boot |
| DMA transfers | Controller moves data to/from RAM | Post-boot |
| Interrupt-driven completion | Controller signals I/O complete | Post-boot |

---

## 7. Initialization Sequence

All wiring occurs in SubsystemCoordinator during construction:

```cpp
// 1. Core subsystems
m_safeMemory     = make_unique<SafeMemory>();
m_mmio           = make_unique<MMIOManager>();
m_guestMemory    = make_unique<GuestMemory>();
m_consoleManager = make_unique<ConsoleManager>();

// 2. Wire GuestMemory to backends, initialize PA routes
m_guestMemory->attachSubsystems(m_safeMemory.get(), m_mmio.get());
m_guestMemory->initDefaultPARoutes();   // creates Route 3 for Tsunami

// 3. Construct chipset (variant inferred from model)
m_tsunami = make_unique<TsunamiChipset>("ES40", 4, memSize);
m_tsunami->registerWithMMIO(m_mmio.get());

// 4. ISA bridge on PCI bus 0, device 1
m_isaBridge = make_unique<IsaBridge>();
m_tsunami->pchip().registerPciDevice(0, 1, 0,
    m_isaBridge.get(),
    IsaBridge::pciConfigReadStatic,
    IsaBridge::pciConfigWriteStatic);

// 5. UART COM1 (OPA0) at I/O 0x3F8
auto* opa0 = m_consoleManager->getDevice("OPA0");
m_uart0 = make_unique<Uart16550>(opa0, 0x3F8, "OPA0");
m_isaBridge->registerIoDevice(0x3F8, 0x400,
    m_uart0.get(),
    Uart16550::ioReadStatic,
    Uart16550::ioWriteStatic);

// 6. UART COM2 (OPA1) at I/O 0x2F8
auto* opa1 = m_consoleManager->getDevice("OPA1");
m_uart1 = make_unique<Uart16550>(opa1, 0x2F8, "OPA1");
m_isaBridge->registerIoDevice(0x2F8, 0x300,
    m_uart1.get(),
    Uart16550::ioReadStatic,
    Uart16550::ioWriteStatic);

// 7. ISA bridge handles all legacy I/O port dispatch
m_tsunami->pchip().registerIoPortRange(0x000, 0x1000,
    m_isaBridge.get(),
    IsaBridge::ioReadStatic,
    IsaBridge::ioWriteStatic);

// 8. Lock MMIO regions (sort for binary search)
m_mmio->finalize();
```

**Dependency Order (must be respected):**

```
SafeMemory
    |
MMIOManager
    |
GuestMemory (attachSubsystems + initDefaultPARoutes)
    |
TsunamiChipset (registerWithMMIO)
    |
IsaBridge (registerPciDevice on Pchip)
    |
Uart16550 (registerIoDevice on IsaBridge)
    |
I/O port range (registerIoPortRange on Pchip)
    |
MMIOManager::finalize()
```

---

## 8. File Inventory

### New Files (Tsunami Chipset Layer)

| File | Location | Purpose |
|------|----------|---------|
| TsunamiVariant.h | chipsetLib/ | Variant enum, model-to-chipset mapping |
| TsunamiChipset.h | chipsetLib/ | Top-level container, MMIO registration |
| TsunamiChipset.cpp | chipsetLib/ | registerWithMMIO implementation |
| TsunamiCchip.h | chipsetLib/ | System config, interrupts, IPI (4 CPU, atomic) |
| TsunamiDchip.h | chipsetLib/ | DRAM config, chip revision |
| TsunamiPchip.h | chipsetLib/ | PCI bridge, config space, I/O space (fn ptr dispatch) |

### New Files (Device Layer)

| File | Location | Purpose |
|------|----------|---------|
| IsaBridge.h | deviceLib/tsunami/ | ALi M1533, PCI config + I/O routing (fn ptr) |
| Uart16550.h | deviceLib/tsunami/ | 16550 UART register model |

### Modified Files

| File | Change |
|------|--------|
| GuestMemory.cpp | initDefaultPARoutes adds Route 3 for Tsunami |
| SubsystemCoordinator.h | Owns TsunamiChipset, IsaBridge, Uart16550 |

### Existing Files (Unchanged)

| File | Role in Chain |
|------|--------------|
| mmioLib/mmio_Manager.h/.cpp | Region dispatch (already complete) |
| deviceLib/IConsoleDevice.h | Backend interface (already defined) |
| deviceLib/SRMConsoleDevice.h | TCP transport (already implemented) |
| deviceLib/ConsoleManager.h/.cpp | Device registry (already implemented) |
| controllersLib/ISP1020_Controller.h | QLogic HBA (connects via fn ptr) |
| controllersLib/PciScsiControllerTemplate.h | SCSI controller base |
| scsiCoreLib/VirtualScsiDevice.h | SCSI target interface |
| scsiCoreLib/ScsiCommand.h | CDB/status/sense contract |

---

## 9. PCI-ISA Device Extension Guide

### 9.1 Adding a New ISA Device

**Step 1: Create device model with static dispatch functions**

```cpp
class RtcDevice {
public:
    quint64 read(quint16 port, quint8 width) { ... }
    void write(quint16 port, quint64 value, quint8 width) { ... }

    static quint64 ioReadStatic(void* ctx, quint16 port, quint8 width) {
        return static_cast<RtcDevice*>(ctx)->read(port, width);
    }
    static void ioWriteStatic(void* ctx, quint16 port, quint64 value, quint8 width) {
        static_cast<RtcDevice*>(ctx)->write(port, value, width);
    }
};
```

**Step 2: Register with ISA bridge**

```cpp
m_rtc = make_unique<RtcDevice>();
m_isaBridge->registerIoDevice(0x070, 0x072,
    m_rtc.get(), RtcDevice::ioReadStatic, RtcDevice::ioWriteStatic);
```

**Step 3: No other changes needed.**

### 9.2 Adding a New PCI Device

**Step 1: Create PCI device with static config space functions**

```cpp
class MyPciDevice {
public:
    quint32 configRead(quint8 reg, quint8 width) { ... }
    void configWrite(quint8 reg, quint32 value, quint8 width) { ... }

    static quint32 configReadStatic(void* ctx, quint8 reg, quint8 width) {
        return static_cast<MyPciDevice*>(ctx)->configRead(reg, width);
    }
    static void configWriteStatic(void* ctx, quint8 reg, quint32 value, quint8 width) {
        static_cast<MyPciDevice*>(ctx)->configWrite(reg, value, width);
    }
};
```

**Step 2: Register with Pchip**

```cpp
m_myDevice = make_unique<MyPciDevice>();
m_tsunami->pchip().registerPciDevice(0, 5, 0,
    m_myDevice.get(),
    MyPciDevice::configReadStatic,
    MyPciDevice::configWriteStatic);
```

**Step 3: Optional — register MMIO BAR or I/O port handlers.**

---

## 10. Diagnostic and Debugging

### 10.1 Trace Points

Each layer logs on unknown/unhandled accesses:

| Layer | Log Message Pattern |
|-------|-------------------|
| GuestMemory | "Unmapped PA 0x..." |
| MMIOManager | AccessViolation return (no region found) |
| Pchip | "read/write unhandled offset 0x..." |
| Pchip | "PCI config write to empty slot..." |
| Pchip | "I/O read unhandled port 0x..." |
| IsaBridge | "I/O read unhandled port 0x..." |
| Uart16550 | "TX 0x41 'A'" (every transmitted byte) |
| Cchip | "read unknown offset 0x..." |
| Dchip | "read unknown offset 0x..." |

### 10.2 First Character Detection

When the first byte reaches PuTTY, the Uart16550 TRACE log will show:

```
Uart16550 OPA0: TX 0x0D '.'
Uart16550 OPA0: TX 0x0A '.'
Uart16550 OPA0: TX 0x41 'A'
Uart16550 OPA0: TX 0x6C 'l'
Uart16550 OPA0: TX 0x70 'p'
Uart16550 OPA0: TX 0x68 'h'
Uart16550 OPA0: TX 0x61 'a'
```

### 10.3 Breakpoints for Debugging

| Breakpoint Location | What It Tells You |
|---------------------|-------------------|
| Uart16550::writeTHR | Firmware is writing to UART (Phase 5 success) |
| IsaBridge::ioRead | Firmware is probing ISA ports (Phase 5 entry) |
| IsaBridge::pciConfigRead | Firmware is enumerating PCI (Phase 4) |
| TsunamiCchip::read | Firmware is probing chipset (Phase 3) |
| TsunamiPchip::readCSR | Firmware is configuring PCI windows |

---

## 11. Summary

The complete byte path from SRM firmware to PuTTY traverses 8 layers,
all connected via function-pointer dispatch (no vtable overhead):

```
CPU instruction (HW_ST / STQ)
  → GuestMemory::writeRouted         (PA router)
    → MMIOManager::handleWrite        (region dispatch, fn ptr)
      → TsunamiPchip::mmioWrite       (chipset decode, fn ptr)
        → Pchip I/O port dispatch     (port lookup, fn ptr)
          → IsaBridge::ioWrite        (ISA routing, fn ptr)
            → Uart16550::ioWrite      (register decode, fn ptr)
              → SRMConsoleDevice::putChar  (TCP transport)
                → PuTTY window        (character rendered)
```

Each layer has a single responsibility. No layer knows about layers
more than one step away. Adding new devices requires implementing
static read/write functions and registering at one point. The firmware
discovers everything through standard PCI enumeration and I/O probing —
no emulator-specific hooks needed.

All dispatch is synchronous, non-blocking, and naturally ordered by
single-threaded CPU execution. Cross-thread concerns (device interrupts,
multi-CPU) are handled by atomic operations on Cchip DRIR and DIM
registers, with no locks in any hot path.