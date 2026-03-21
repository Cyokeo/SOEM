## EEPROM
is a standard part of EtherCAT slaves. From the code in ec_config.c, you can see it's used extensively:
1. SII (Slave Information Interface) - The EEPROM contains standardized data that the master reads during configuration:
   - Manufacturer ID (ECT_SII_MANUF)
   - Product ID (ECT_SII_ID)
   - Revision (ECT_SII_REV)
   - Serial number (ECT_SII_SER)
   - Mailbox configuration (ECT_SII_RXMBXADR, ECT_SII_TXMBXADR, ECT_SII_MBXPROTO)
   - PDO mapping information (via ecx_siiPDO)
   - Sync Manager configuration (via ecx_siiSM)
   - FMMU configuration (via ecx_siiFMMU)
2. Functions used: The code uses ecx_readeeprom1 and ecx_readeeprom2 to read EEPROM data, and ecx_eeprom2pdi to transfer control between master and PDI (Process Data Interface).
3. Standardized by EtherCAT spec: Every EtherCAT slave must have an EEPROM with a defined structure that allows the master to auto-configure the slave without manual intervention.
The EEPROM is essential for EtherCAT's plug-and-play capability - it allows the master to discover slave capabilities, I/O mapping, and configuration automatically.

### EtherCAT EEPROM Access Architecture
1. ESC Register Interface (Memory-Mapped)
The master accesses EEPROM through specific ESC (EtherCAT Slave Controller) registers:
| Register | Address | Description |
|----------|---------|-------------|
| ECT_REG_EEPCFG | 0x0500 | EEPROM control configuration (who owns EEPROM) |
| ECT_REG_EEPCTL | 0x0502 | EEPROM command register |
| ECT_REG_EEPSTAT | 0x0502 | EEPROM status register |
| ECT_REG_EEPADR | 0x0504 | EEPROM address |
| ECT_REG_EEPDAT | 0x0508 | EEPROM data (8 bytes) |
2. EEPROM Command Structure
typedef struct {
    uint16 comm;  // Command (READ=0x0100, WRITE=0x0201, RELOAD=0x0300)
    uint16 addr;  // EEPROM word address
    uint16 d2;    // Reserved
} ec_eepromt;
3. Access Protocol
┌─────────────────────────────────────────────────────────┐
│ 1. Request EEPROM ownership to master                     │
│    Write 0x02 then 0x00 to ECT_REG_EEPCFG                │
├─────────────────────────────────────────────────────────┤
│ 2. Wait until EEPROM not busy                            │
│    Poll ECT_REG_EEPSTAT (wait for BUSY flag clear)      │
├─────────────────────────────────────────────────────────┤
│ 3. Issue READ command                                   │
│    Write {CMD_READ, word_address, 0} to ECT_REG_EEPCTL  │
├─────────────────────────────────────────────────────────┤
│ 4. Wait for completion                                  │
│    Poll ECT_REG_EEPSTAT until busy cleared               │
├─────────────────────────────────────────────────────────┤
│ 5. Read data                                            │
│    Read 4 or 8 bytes from ECT_REG_EEPDAT (R64 bit)      │
├─────────────────────────────────────────────────────────┤
│ 6. Release EEPROM to PDI (optional)                    │
│    Write 0x01 to ECT_REG_EEPCFG                         │
└─────────────────────────────────────────────────────────┘
4. Status Flags (ECT_REG_EEPSTAT)
| Bit | Name | Description |
|-----|------|-------------|
| 0x0040 | EC_ESTAT_R64 | Supports 8-byte read (vs 4-byte) |
| 0x2000 | EC_ESTAT_NACK | Negative acknowledge |
| 0x7800 | EC_ESTAT_EMASK | Error mask |
| 0x8000 | EC_ESTAT_BUSY | EEPROM busy |
5. SII (Slave Information Interface) Structure
The EEPROM contains standardized SII sections starting at word address 0x0020 (byte 0x0040):
Word 0x0000-0x0007: Vendor-specific configuration
Word 0x0008: Manufacturer ID
Word 0x000A: Product ID  
Word 0x000C: Revision ID
Word 0x000E: Serial Number
Word 0x0010-0x001F: Reserved
Word 0x0020+: TYPE 0x000A = String Section
Word 0x0020+: TYPE 0x001E = General Section  
Word 0x0020+: TYPE 0x0028 = FMMU Section
Word 0x0020+: TYPE 0x0029 = Sync Manager Section
Word 0x0020+: TYPE 0x0032 = PDO Rx Section
Word 0x0020+: TYPE 0x0033 = PDO Tx Section
6. SOEM Implementation Flow
ecx_config_init()
    └─> ecx_readeeprom1(slave, ECT_SII_MANUF)  // Issue read request
    └─> ecx_readeeprom2(slave)                   // Get manufacturer ID
    └─> ecx_readeeprom1(slave, ECT_SII_ID)      // Issue read request
    └─> ecx_readeeprom2(slave)                   // Get product ID
    └─> ... (parallel reads for all slaves)
    └─> ecx_siifind(slave, ECT_SII_GENERAL)     // Find SII section
    └─> ecx_siigetbyte(slave, address)           // Read cached bytes
    └─> ecx_siiSM()                              // Parse Sync Managers
    └─> ecx_siiFMMU()                            // Parse FMMUs
    └─> ecx_siiPDO()                             // Parse PDO mappings
7. Key Insight
All EEPROM access goes through standard EtherCAT datagrams (FPWR, FPRD) - no special protocols. The master simply:
1. Writes commands to ESC registers via EtherCAT frames
2. Reads responses back via EtherCAT frames
This is why SOEM works with any standard Ethernet NIC - it's just reading/writing memory-mapped registers through normal Ethernet frames with EtherCAT type (0x88A4).

# EtherCAT Packet Format & Parsing
1. Complete Frame Structure
┌──────────────────────────────────────────────────────────────────────────┐
│                      ETHERNET FRAME (Standard Ethernet II)                 │
├──────────────────────────────────────────────────────────────────────────┤
│  Ethernet Header (14 bytes)                                               │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ Destination MAC (6 bytes)      = 0xFFFF.FFFF.FFFF (broadcast)      │  │
│  │ Source MAC (6 bytes)           = Master MAC address                 │  │
│  │ EtherType (2 bytes)           = 0x88A4 (ETH_P_ECAT)                 │  │
│  └────────────────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────────┤
│  EtherCAT Header (10 bytes) + Datagram(s)                               │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ ELENGTH (2 bytes)  = Total EtherCAT frame length (excl. Ethernet)  │  │
│  │ COMMAND (1 byte)   = EC_CMD_XXX (see below)                        │  │
│  │ INDEX (1 byte)    = Frame identifier for TX/RX matching            │  │
│  │ ADP (2 bytes)     = Auto Increment Position / Configured Address   │  │
│  │ ADO (2 bytes)     = Address Offset (ESC register address)           │  │
│  │ DLENGTH (2 bytes) = Data length | EC_DATAGRAMFOLLOWS flag         │  │
│  │                                                                    │  │
│  │ ┌────────────────────────────────────────────────────────────────┐ │  │
│  │ │ DATA (0-1486 bytes)                                            │ │  │
│  │ │ - For Read: empty (slave fills)                               │ │  │
│  │ │ - For Write: data to write                                    │ │  │
│  │ │ - For LRW: process data                                      │ │  │
│  │ └────────────────────────────────────────────────────────────────┘ │  │
│  │ WKC (2 bytes)     = Work Counter (incremented by each slave)       │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Multiple datagrams can be chained in one frame (EC_DATAGRAMFOLLOWS bit) │
└──────────────────────────────────────────────────────────────────────────┘
2. EtherCAT Command Types (EC_CMD_XXX)
| Command | Value | Description |
|---------|-------|-------------|
| EC_CMD_NOP | 0x00 | No operation |
| EC_CMD_APRD | 0x01 | Auto Increment Position Read |
| EC_CMD_APWR | 0x02 | Auto Increment Position Write |
| EC_CMD_APRW | 0x03 | Auto Increment Position Read-Write |
| EC_CMD_FPRD | 0x04 | Configured Address Read |
| EC_CMD_FPWR | 0x05 | Configured Address Write |
| EC_CMD_FPRW | 0x06 | Configured Address Read-Write |
| EC_CMD_BRD | 0x07 | Broadcast Read |
| EC_CMD_BWR | 0x08 | Broadcast Write |
| EC_CMD_BRW | 0x09 | Broadcast Read-Write |
| EC_CMD_LRD | 0x0A | Logical Memory Read |
| EC_CMD_LWR | 0x0B | Logical Memory Write |
| EC_CMD_LRW | 0x0C | Logical Memory Read-Write (Process Data) |
| EC_CMD_ARMW | 0x0D | Auto Increment Read Multiple Write |
| EC_CMD_FRMW | 0x0E | Configured Read Multiple Write |
3. Key Addressing Concepts
┌─────────────────────────────────────────────────────────────┐
│  ADP (Address Position) - Two modes:                         │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ Auto Increment Mode:                                   │  │
│  │   - ADP = 0x0000 = Broadcast to all slaves            │  │
│  │   - ADP = 0xFFFF = Skip slave                         │  │
│  │   - ADP = 0x0001, 0x0002, ... = Address sequentially   │  │
│  │                                                            │  │
│  │ Configured Address Mode:                                 │  │
│  │   - ADP = Configured station address (from ESC)        │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  ADO (Address Offset)                                        │
│    - ESC register offset to access (e.g., 0x0508 = EEPROM) │
└─────────────────────────────────────────────────────────────┘
4. Frame Parsing in SOEM
Frame Transmission Flow:
// Example: FPRD (Read from configured address)
ecx_FPRD(port, ADP, ADO, length, data, timeout)
    │
    ├─> ecx_getindex(port)           // Get unique frame index
    │
    ├─> ecx_setupdatagram(...)
    │   ├─> Fill Ethernet header (already set by ec_setupheader())
    │   ├─> Set EtherCAT header:
    │   │     elength = 0x1000 + 10 + length  // EC_ECATTYPE + header + data
    │   │     command = EC_CMD_FPRD
    │   │     index   = frame_idx
    │   │     ADP     = slave_address
    │   │     ADO     = register_offset
    │   │     dlength = data_length
    │   ├─> Copy data to frame
    │   └─> Zero workcounter (WKC)
    │
    ├─> ecx_outframe(port, idx)       // Send via socket
    │
    └─> ecx_srconfirm(port, idx, timeout)
        │
        └─> ecx_waitinframe(port, idx, timeout)
            │
            └─> ecx_inframe(port, idx)
                │
                ├─> Check if frame already in buffer
                │   └─> Extract WKC from frame end
                │
                └─> Otherwise:
                    ├─> ecx_recvpkt(port)    // Receive from socket
                    ├─> Verify EtherType = 0x88A4
                    ├─> Extract INDEX from EtherCAT header
                    ├─> Match INDEX with expected frame
                    └─> Extract data and WKC
Frame Matching Logic:
// In ecx_inframe() - nicdrv.c:378-467
int ecx_inframe(ecx_portt *port, uint8 idx, int stacknumber)
{
    // 1. Check if frame already received
    if (rxbufstat[idx] == EC_BUF_RCVD) {
        // Extract WKC from frame end
        rval = rxbuf[idx][frame_length] + (rxbuf[idx][frame_length + 1] << 8);
    }
    
    // 2. Otherwise, receive from socket
    if (ecx_recvpkt(port, stacknumber)) {
        // Verify EtherCAT frame
        if (ehp->etype == htons(ETH_P_ECAT)) {  // 0x88A4
            
            // 3. Match INDEX
            idxf = ecp->index;  // INDEX from received frame
            
            if (idxf == idx) {
                // Found our frame - copy to buffer
                memcpy(rxbuf, &tempbuf[ETH_HEADERSIZE], ...);
                rval = extract_WKC();
                rxbufstat[idx] = EC_BUF_COMPLETE;
            } else {
                // Out-of-order frame - store for later
                if (rxbufstat[idxf] == EC_BUF_TX) {
                    rxbuf[idxf] = tempbuf;
                    rxbufstat[idxf] = EC_BUF_RCVD;
                }
            }
        }
    }
}
5. Work Counter (WKC) Mechanism
The work counter is critical for EtherCAT reliability:
┌─────────────────────────────────────────────────────────────┐
│  Work Counter (WKC)                                         │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ - 16-bit field at end of each datagram                │  │
│  │ - Incremented by each slave that processes command   │  │
│  │                                                            │  │
│  │ Example for broadcast write:                             │  │
│  │   Send BWR to all slaves → WKC = 3 (3 slaves responded) │  │
│  │   If WKC = 0 → No slaves received command!              │  │
│  │   If WKC = 2 but we have 3 slaves → One slave failed   │  │
│  └────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
6. Multiple Datagrams in Single Frame
// Chain multiple datagrams using ecx_adddatagram()
ecx_setupdatagram(port, frame, CMD1, idx, ADP1, ADO1, len1, data1);
ecx_adddatagram(port, frame, CMD2, idx, TRUE, ADP2, ADO2, len2, data2); // more=TRUE
ecx_adddatagram(port, frame, CMD3, idx, FALSE, ADP3, ADO3, len3, data3); // more=FALSE
The EC_DATAGRAMFOLLOWS flag (bit 15 of dlength) indicates more datagrams follow.
7. Real-World Example: Reading EEPROM
// Reading slave manufacturer ID from EEPROM
// Register: ECT_REG_EEPDAT = 0x0508
// 1. Setup read command
ec_eepromt ed;
ed.comm = htoes(EC_ECMD_READ);  // 0x0100
ed.addr = htoes(0x0008);        // Word address (SII_MANUF)
ed.d2 = 0x0000;
// 2. Write command to EEPROM control register
ecx_FPWR(port, slave_config_adr, ECT_REG_EEPCTL, sizeof(ed), &ed, timeout);
// 3. Wait for EEPROM to process
//    (Poll ECT_REG_EEPSTAT until BUSY bit clears)
// 4. Read data from EEPROM data register
uint64 eedat;
ecx_FPRD(port, slave_config_adr, ECT_REG_EEPDAT, sizeof(eedat), &eedat, timeout);
// 5. Extract manufacturer ID
uint32 man = etohl(eedat);
8. Frame Size Constraints
EC_MAXECATFRAME = 1518 bytes  // Maximum Ethernet frame
ETH_HEADERSIZE = 14 bytes      // Ethernet header
EC_HEADERSIZE = 10 bytes       // EtherCAT header
EC_WKCSIZE = 2 bytes           // Work counter
EC_MAXLRWDATA = 1486 bytes     // Maximum LRW data
// For LRW (process data):
// 1486 = 1518 - 14 - 2 - 10 - 2 - 4
//       (Eth hdr) (ELEN) (EC hdr) (WKC) (FCS)
9. Parsing Summary Diagram
Received Ethernet Frame (RAW socket)
        │
        ▼
┌───────────────────────────┐
│ Check EtherType = 0x88A4 │ ──── Not EtherCAT → Ignore
└─────────────┬─────────────┘
              │
              ▼
┌───────────────────────────┐
│ Strip Ethernet Header     │  (14 bytes)
└─────────────┬─────────────┘
              │
              ▼
┌───────────────────────────┐
│ Parse EtherCAT Header     │
│ • Extract ELENGTH        │
│ • Extract COMMAND        │
│ • Extract INDEX (match!) │
│ • Extract ADP, ADO       │
│ • Extract DLENGTH        │
└─────────────┬─────────────┘
              │
              ▼
┌───────────────────────────┐
│ Extract DATA              │  (Based on DLENGTH)
└─────────────┬─────────────┘
              │
              ▼
┌───────────────────────────┐
│ Extract WKC              │  (2 bytes at end)
└─────────────┬─────────────┘
              │
              ▼
    Return to Application
    + Work Counter (WKC)
This is the complete EtherCAT packet format and parsing mechanism used by SOEM!

# netrwork related
NIC driver port layer