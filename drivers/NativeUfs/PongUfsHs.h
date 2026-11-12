// SPDX-License-Identifier: BSD-2-Clause-Patent
// HS-G4 bring-up for the SM8475 UFS link.  Ported from the iOS16 Pong driver
// (xnu/patches/0090..0092), which measured 0.5 MB/s in PWM-G1 and ~16 MiB/s
// after this sequence on the same hardware.
//
// Enabled with boot-arg pong_ufs_hs=1.  The default path in PongUfsTransport
// keeps the ABL-programmed link, so a failed experiment is one boot away from
// the working PWM path.  This code resets the UFS_PHY block (and therefore
// the host controller) and rebuilds the link from the QMP PHY tables.
#ifndef PONG_UFS_HS_H
#define PONG_UFS_HS_H

namespace {

enum : uint32_t {
  kUfsRegAhit = 0x18,
  kUfsRegIs = 0x20,
  kUfsRegHcs = 0x30,
  kUfsRegHce = 0x34,
  kUfsRegUtrlba = 0x50,
  kUfsRegUtrlbau = 0x54,
  kUfsRegUtrlrsr = 0x60,
  kUfsRegUiccmd = 0x90,
  kUfsRegUicarg1 = 0x94,
  kUfsRegUicarg2 = 0x98,
  kUfsRegUicarg3 = 0x9c,
  kUfsRegSys1clk1us = 0xc0,
  kUfsRegCfg0 = 0xd8,
  kUfsRegCfg1 = 0xdc,
  kUfsRegHwVersion = 0xe4,
};
enum : uint32_t {
  kUfsCfg1QuniproSel = 1u << 0,
  kUfsCfg1DevRefClkEn = 1u << 26,
  kUfsCfg0QuniproG4Sel = 1u << 5,
};

// GCC offsets from the GCC base at 0x100000 (gcc-sm8450.c; SM8475 matches).
enum : uint32_t {
  kGccUfsBcr = 0x87000,
  kGccUfsGdscr = 0x87004,
  kGccUfsAxiCbcr = 0x87018,
  kGccUfsAhbCbcr = 0x87020,
  kGccUfsTxSymbolCbcr = 0x87024,
  kGccUfsRxSymbol0Cbcr = 0x87028,
  kGccUfsUniproCbcr = 0x87064,
  kGccUfsIceCbcr = 0x8706c,
  kGccUfsAuxCbcr = 0x870a4,
  kGccUfsRxSymbol1Cbcr = 0x870c0,
  kGccAggreUfsAxiCbcr = 0x870d4,
  kGccUfs0ClkrefEn = 0x9c000,
  kGccUfsAxiRcgr = 0x8702c,
  kGccUfsIceRcgr = 0x87074,
  kGccUfsUniproRcgr = 0x8708c,
  kGccUfsAuxRcgr = 0x870a8,
};
enum : uint32_t {
  kGccRcgUpdate = 1u << 0,
  kGccRcgRootEn = 1u << 1,
  kGccRcgDivMask = 0x1fu,
  kGccRcgSrcMask = 0x7u << 8,
  kGccRcgModeMask = 0x3u << 12,
  kGccRcgHwCtrl = 1u << 20,
  kGccCbcrHwCtrl = 1u << 1,
  kGccCbcrClkOff = 1u << 31,
  kGccGdscPwrOn = 1u << 31,
  kGccRcgDiv300Mhz = 3,
  kGccRcgDiv19p2Mhz = 1,
};

struct PongUfsPhyReg {
  uint16_t off;
  uint16_t val;
};

// Linux sm8475_ufsphy_cfg: QMP v6 offsets, sm8475 SerDes, sm8550 TX/RX/PCS
// and the HS-G4 overlay.
static const PongUfsPhyReg kPongPhySerdes[] = {
  {0x110, 0xd9}, {0x174, 0x16}, {0x03c, 0x11}, {0x09c, 0x00}, {0x120, 0x01}, {0x148, 0x00},
  {0x088, 0x82}, {0x074, 0x18}, {0x078, 0x18}, {0x080, 0xff}, {0x084, 0x0c},
};
static const PongUfsPhyReg kPongPhyTx[] = {
  {0x07c, 0x05}, {0x030, 0x07},
};
static const PongUfsPhyReg kPongPhyRx[] = {
  {0x0d4, 0x0c}, {0x208, 0xc2}, {0x20c, 0xc2}, {0x214, 0x1a}, {0x220, 0x60}, {0x238, 0x9e},
  {0x244, 0x60}, {0x25c, 0x9e}, {0x260, 0x0e}, {0x264, 0x36}, {0x270, 0x02},
};
static const PongUfsPhyReg kPongPhyPcs[] = {
  {0x18c, 0x69}, {0x030, 0x0f}, {0x1f4, 0x43}, {0x1fc, 0x02},
};
static const PongUfsPhyReg kPongPhyG4Serdes[] = {
  {0x140, 0x04}, {0x0f4, 0x0f}, {0x070, 0x14}, {0x028, 0x98}, {0x010, 0x14}, {0x014, 0x18},
  {0x018, 0x18}, {0x020, 0x32}, {0x024, 0x0f},
};
static const PongUfsPhyReg kPongPhyG4Tx[] = {{0x108, 0x4c}};
static const PongUfsPhyReg kPongPhyG4Rx[] = {{0x178, 0x0e}};
static const PongUfsPhyReg kPongPhyG4Pcs[] = {{0x02c, 0x0b}, {0x074, 0x04}, {0x0bc, 0x04}};

enum : uint32_t {
  kPongPhyOffSerdes = 0x000, kPongPhyOffPcs = 0x400, kPongPhyOffTx = 0x1000,
  kPongPhyOffRx = 0x1200, kPongPhyOffTx2 = 0x1800, kPongPhyOffRx2 = 0x1a00,
};

static bool gPongUfsHsActive;
static bool gPongUfsHsTried;

static inline uint32_t pongGccRd(uint32_t off) {
  return read32(gHost.gcc, off);
}
static inline void pongGccWr(uint32_t off, uint32_t v) {
  write32(gHost.gcc, off, v);
}

static uint32_t pongGccRcgRate(uint32_t cmdRcgr, uint32_t *divp, uint32_t *srcp) {
  uint32_t cfg = pongGccRd(cmdRcgr + 4);
  uint32_t div = cfg & kGccRcgDivMask;
  uint32_t src = (cfg >> 8) & 0x7;
  uint32_t parent;
  switch (src) {
    case 0: parent = 19200000; break;
    case 1: parent = 600000000; break;
    case 6: parent = 300000000; break;
    default: parent = 0; break;
  }
  if (divp) *divp = div;
  if (srcp) *srcp = src;
  return parent ? parent * 2 / (div + 1) : 0;
}

static int pongGccRcgSet(uint32_t cmdRcgr, uint32_t div, uint32_t src, const char *name) {
  uint32_t cmd = pongGccRd(cmdRcgr);
  uint32_t cfg = pongGccRd(cmdRcgr + 4);
  if (!(cmd & kGccRcgRootEn)) pongGccWr(cmdRcgr, cmd | kGccRcgRootEn);
  cfg &= ~(kGccRcgDivMask | kGccRcgSrcMask | kGccRcgModeMask);
  cfg |= div | (src << 8) | kGccRcgHwCtrl;
  pongGccWr(cmdRcgr + 4, cfg);
  pongGccWr(cmdRcgr, pongGccRd(cmdRcgr) | kGccRcgUpdate);
  for (int i = 0; i < 100; i++) {
    if (!(pongGccRd(cmdRcgr) & kGccRcgUpdate)) return 0;
    IODelay(1);
  }
  IOLog("PONG_UFS: HS gcc %s rate update timed out cmd=0x%x\n", name, pongGccRd(cmdRcgr));
  return kIOReturnTimeout;
}

static int pongGccBranchOn(uint32_t cbcr, const char *name) {
  uint32_t v = pongGccRd(cbcr);
  if (!(v & 1u)) pongGccWr(cbcr, v | 1u);
  for (int i = 0; i < 200; i++) {
    v = pongGccRd(cbcr);
    if (!(v & kGccCbcrClkOff) || (v & kGccCbcrHwCtrl)) return 0;
    IODelay(1);
  }
  IOLog("PONG_UFS: HS gcc %s did not start cbcr=0x%x\n", name, v);
  return kIOReturnTimeout;
}

static void pongGccPowerDomainOn() {
  uint32_t v = pongGccRd(kGccUfsGdscr);
  if (v & kGccGdscPwrOn) return;
  pongGccWr(kGccUfsGdscr, v & ~1u);
  for (int i = 0; i < 1000; i++) {
    if (pongGccRd(kGccUfsGdscr) & kGccGdscPwrOn) return;
    IODelay(1);
  }
  IOLog("PONG_UFS: HS gcc gdsc stuck at 0x%x\n", pongGccRd(kGccUfsGdscr));
}

static void pongGccPhyReset() {
  uint32_t v = pongGccRd(kGccUfsBcr);
  IOLog("PONG_UFS: HS gcc bcr before 0x%x\n", v);
  pongGccWr(kGccUfsBcr, v | 1u);
  IODelay(200);
  pongGccWr(kGccUfsBcr, pongGccRd(kGccUfsBcr) & ~1u);
  IODelay(1000);
  IOLog("PONG_UFS: HS gcc bcr after 0x%x\n", pongGccRd(kGccUfsBcr));
}

static int pongGccClocksOn() {
  pongGccPowerDomainOn();
  int err = 0;
  err |= pongGccRcgSet(kGccUfsAxiRcgr, kGccRcgDiv300Mhz, 1, "axi");
  err |= pongGccRcgSet(kGccUfsUniproRcgr, kGccRcgDiv300Mhz, 1, "unipro");
  err |= pongGccRcgSet(kGccUfsIceRcgr, kGccRcgDiv300Mhz, 1, "ice");
  err |= pongGccRcgSet(kGccUfsAuxRcgr, kGccRcgDiv19p2Mhz, 0, "aux");
  err |= pongGccBranchOn(kGccUfsAxiCbcr, "axi");
  err |= pongGccBranchOn(kGccUfsAhbCbcr, "ahb");
  err |= pongGccBranchOn(kGccUfsUniproCbcr, "unipro");
  err |= pongGccBranchOn(kGccUfsIceCbcr, "ice");
  err |= pongGccBranchOn(kGccUfsAuxCbcr, "aux");
  err |= pongGccBranchOn(kGccAggreUfsAxiCbcr, "aggre");
  err |= pongGccBranchOn(kGccUfsTxSymbolCbcr, "tx_sym");
  err |= pongGccBranchOn(kGccUfsRxSymbol0Cbcr, "rx_sym0");
  err |= pongGccBranchOn(kGccUfsRxSymbol1Cbcr, "rx_sym1");
  err |= pongGccBranchOn(kGccUfs0ClkrefEn, "qref");
  IODelay(1000);
  IOLog("PONG_UFS: HS gcc gdsc=0x%x bcr=0x%x axi=%u uni=%u ice=%u aux=%u cbcr=%x/%x/%x/%x/%x/%x/%x\n",
        pongGccRd(kGccUfsGdscr), pongGccRd(kGccUfsBcr),
        pongGccRcgRate(kGccUfsAxiRcgr, nullptr, nullptr),
        pongGccRcgRate(kGccUfsUniproRcgr, nullptr, nullptr),
        pongGccRcgRate(kGccUfsIceRcgr, nullptr, nullptr),
        pongGccRcgRate(kGccUfsAuxRcgr, nullptr, nullptr),
        pongGccRd(kGccUfsAxiCbcr), pongGccRd(kGccUfsAhbCbcr),
        pongGccRd(kGccUfsUniproCbcr), pongGccRd(kGccUfsIceCbcr),
        pongGccRd(kGccUfsAuxCbcr), pongGccRd(kGccAggreUfsAxiCbcr),
        pongGccRd(kGccUfs0ClkrefEn));
  return err ? kIOReturnError : 0;
}

static void pongPhyApply(uint32_t off, const PongUfsPhyReg *t, size_t n) {
  for (size_t i = 0; i < n; i++)
    write32(gHost.phy, off + t[i].off, t[i].val);
}
#define PONG_PHY_APPLY(o, t) pongPhyApply((o), (t), sizeof(t) / sizeof((t)[0]))

static int pongPhyWake() {
  volatile uint32_t *pcs = reinterpret_cast<volatile uint32_t *>(gHost.phy + kPongPhyOffPcs);
  IOLog("PONG_UFS: HS phy pre start=0x%x pwrup=0x%x ready=0x%x\n",
        pcs[0x000 / 4], pcs[0x004 / 4], pcs[0x1a8 / 4]);
  // qmp_ufs_com_init(): SW_PWRUP first, tables next, SW_RESET only cleared,
  // SerDes start only (PCS_START is not used by Linux).
  pcs[0x004 / 4] = pcs[0x004 / 4] | 1u;
  __asm__ volatile("dsb sy" ::: "memory");
  PONG_PHY_APPLY(kPongPhyOffSerdes, kPongPhySerdes);
  PONG_PHY_APPLY(kPongPhyOffTx, kPongPhyTx);
  PONG_PHY_APPLY(kPongPhyOffRx, kPongPhyRx);
  PONG_PHY_APPLY(kPongPhyOffTx2, kPongPhyTx);
  PONG_PHY_APPLY(kPongPhyOffRx2, kPongPhyRx);
  PONG_PHY_APPLY(kPongPhyOffPcs, kPongPhyPcs);
  PONG_PHY_APPLY(kPongPhyOffSerdes, kPongPhyG4Serdes);
  PONG_PHY_APPLY(kPongPhyOffTx, kPongPhyG4Tx);
  PONG_PHY_APPLY(kPongPhyOffRx, kPongPhyG4Rx);
  PONG_PHY_APPLY(kPongPhyOffTx2, kPongPhyG4Tx);
  PONG_PHY_APPLY(kPongPhyOffRx2, kPongPhyG4Rx);
  PONG_PHY_APPLY(kPongPhyOffPcs, kPongPhyG4Pcs);
  pcs[0x008 / 4] = pcs[0x008 / 4] & ~1u;
  pcs[0x000 / 4] = (pcs[0x000 / 4] & ~2u) | 1u;
  for (int i = 0; i < 1000 && !(pcs[0x1a8 / 4] & 1); i++) IODelay(100);
  uint32_t ready = pcs[0x1a8 / 4];
  IOLog("PONG_UFS: HS phy post start=0x%x pwrup=0x%x ready=0x%x\n",
        pcs[0x000 / 4], pcs[0x004 / 4], ready);
  return (ready & 1) ? 0 : kIOReturnTimeout;
}

static int pongHbaRestart() {
  write32(gHost.hc, kUfsRegHce, 0);
  for (int i = 0; i < 1000 && (read32(gHost.hc, kUfsRegHce) & 1); i++) IODelay(1);
  write32(gHost.hc, kUfsRegHce, 1);
  for (int i = 0; i < 1000 && !(read32(gHost.hc, kUfsRegHce) & 1); i++) IODelay(1);
  for (int i = 0; i < 1000 && !(read32(gHost.hc, kUfsRegHcs) & 0x8); i++) IODelay(1);
  write32(gHost.hc, kUfsRegUtrlrsr, 0);
  write32(gHost.hc, kUfsRegUtrlba, uint32_t(gHost.phys));
  write32(gHost.hc, kUfsRegUtrlbau, uint32_t(gHost.phys >> 32));
  write32(gHost.hc, kUfsRegUtrlrsr, 1);
  write32(gHost.hc, kUfsRegAhit, 0);
  write32(gHost.hc, kUfsRegIs, read32(gHost.hc, kUfsRegIs));
  bool ok = (read32(gHost.hc, kUfsRegHce) & 1) && (read32(gHost.hc, kUfsRegHcs) & 0x8);
  IOLog("PONG_UFS: HS host restart hce=0x%x hcs=0x%x %s\n",
        read32(gHost.hc, kUfsRegHce), read32(gHost.hc, kUfsRegHcs), ok ? "ok" : "failed");
  return ok ? 0 : kIOReturnTimeout;
}

static int pongUic(uint8_t opcode, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t *out) {
  if (!(read32(gHost.hc, kUfsRegHcs) & 0x8)) {
    IOLog("PONG_UFS: HS UIC not ready hcs=0x%x\n", read32(gHost.hc, kUfsRegHcs));
    return kIOReturnBusy;
  }
  write32(gHost.hc, kUfsRegUicarg1, a1);
  write32(gHost.hc, kUfsRegUicarg2, a2);
  write32(gHost.hc, kUfsRegUicarg3, a3);
  write32(gHost.hc, kUfsRegUiccmd, opcode & 0xff);
  uint64_t deadline;
  clock_interval_to_deadline(100, kMillisecondScale, &deadline);
  while (!(read32(gHost.hc, kUfsRegIs) & 0x400)) {
    if (mach_absolute_time() > deadline) {
      IOLog("PONG_UFS: HS UIC 0x%02x timeout is=0x%x\n", opcode, read32(gHost.hc, kUfsRegIs));
      return kIOReturnTimeout;
    }
    IODelay(1);
  }
  write32(gHost.hc, kUfsRegIs, read32(gHost.hc, kUfsRegIs));
  if (out) *out = read32(gHost.hc, kUfsRegUicarg3);
  return int(read32(gHost.hc, kUfsRegUicarg2) & 0xff);
}

static void pongHcClockInit() {
  uint32_t ver = read32(gHost.hc, kUfsRegHwVersion);
  uint32_t major = (ver >> 28) & 0xf;
  uint32_t axi = pongGccRcgRate(kGccUfsAxiRcgr, nullptr, nullptr);
  if (!axi) axi = 300000000;
  write32(gHost.hc, kUfsRegCfg1, read32(gHost.hc, kUfsRegCfg1) | kUfsCfg1QuniproSel);
  if (major >= 5)
    write32(gHost.hc, kUfsRegCfg0, read32(gHost.hc, kUfsRegCfg0) & ~kUfsCfg0QuniproG4Sel);
  write32(gHost.hc, kUfsRegSys1clk1us, axi / 1000000);
  (void)read32(gHost.hc, kUfsRegSys1clk1us);
  IOLog("PONG_UFS: HS hc ver=0x%x cfg1=0x%x sys1clk=%uMHz\n",
        ver, read32(gHost.hc, kUfsRegCfg1), read32(gHost.hc, kUfsRegSys1clk1us));
}

static void pongDevRefClock(bool on) {
  uint32_t v = read32(gHost.hc, kUfsRegCfg1);
  v = on ? (v | kUfsCfg1DevRefClkEn) : (v & ~kUfsCfg1DevRefClkEn);
  write32(gHost.hc, kUfsRegCfg1, v);
  (void)read32(gHost.hc, kUfsRegCfg1);
  IODelay(10);
  IOLog("PONG_UFS: HS device ref clock %s cfg1=0x%x\n", on ? "on" : "off", v);
}

static void pongDmeClockInit() {
  const uint32_t cycles = 300;
  uint32_t v = 0;
  int r = pongUic(0x01, 0xD002u << 16, 0, 0, &v);
  if (!r) {
    v &= ~(0x3ffu << 16);
    v |= (cycles & 0x3ff) << 16;
    v &= ~(1u << 8);
    r = pongUic(0x02, 0xD002u << 16, 0, v, nullptr);
  }
  IOLog("PONG_UFS: HS dme core clk ctrl=0x%x -> %d\n", v, r);
  v = 0;
  r = pongUic(0x01, 0x9007u << 16, 0, 0, &v);
  if (!r) {
    v = (v & ~0x7fu) | (cycles / 25);
    r = pongUic(0x02, 0x9007u << 16, 0, v, nullptr);
  }
  IOLog("PONG_UFS: HS dme 40ns cycles=0x%x -> %d\n", v, r);
}

static int pongSetHsGear() {
  uint32_t maxhs = 0, tx = 0, rx = 0, pm = 0, txl = 0, rxl = 0, cs = 0xff;
  int e1 = pongUic(0x01, 0x1587u << 16, 0, 0, &maxhs);  // PA_MAXRXHSGEAR
  int e2 = pongUic(0x01, 0x1568u << 16, 0, 0, &tx);     // PA_TXGEAR
  int e3 = pongUic(0x01, 0x1583u << 16, 0, 0, &rx);     // PA_RXGEAR
  int e4 = pongUic(0x01, 0x1571u << 16, 0, 0, &pm);     // PA_PWRMODE
  int e5 = pongUic(0x01, 0x1560u << 16, 0, 0, &txl);    // PA_ACTIVETXDATALANES
  int e6 = pongUic(0x01, 0x1580u << 16, 0, 0, &rxl);    // PA_ACTIVERXDATALANES
  if (e1 || maxhs < 1) {
    IOLog("PONG_UFS: HS no gear maxhs=%u err=%d\n", maxhs, e1);
    return kIOReturnUnsupported;
  }
  uint32_t want = maxhs > 4 ? 4 : maxhs;
  int s_rx = pongUic(0x02, 0x1583u << 16, 0, want, nullptr);
  int s_lane_rx = pongUic(0x02, 0x1580u << 16, 0, 2, nullptr);
  pongUic(0x02, 0x1584u << 16, 0, 1, nullptr);
  int s_tx = pongUic(0x02, 0x1568u << 16, 0, want, nullptr);
  int s_lane_tx = pongUic(0x02, 0x1560u << 16, 0, 2, nullptr);
  pongUic(0x02, 0x1569u << 16, 0, 1, nullptr);
  int s_adapt = pongUic(0x02, 0x15d4u << 16, 0, 1, nullptr);
  int s_pm = -1, series = 1;
  for (series = 1; series <= 2; series++) {
    pongUic(0x02, 0x156au << 16, 0, uint32_t(series), nullptr);
    s_pm = pongUic(0x02, 0x1571u << 16, 0, 0x11u, nullptr);
    if (!s_pm) break;
  }
  for (int i = 0; i < 100; i++) {
    if (!pongUic(0x01, 0x4020u << 16, 0, 0, &cs) && cs == 1) break;
    IODelay(1000);
  }
  uint32_t ntx = 0, nrx = 0, npm = 0, ntxl = 0, nrxl = 0;
  pongUic(0x01, 0x1568u << 16, 0, 0, &ntx);
  pongUic(0x01, 0x1583u << 16, 0, 0, &nrx);
  pongUic(0x01, 0x1571u << 16, 0, 0, &npm);
  pongUic(0x01, 0x1560u << 16, 0, 0, &ntxl);
  pongUic(0x01, 0x1580u << 16, 0, 0, &nrxl);
  IOLog("PONG_UFS: HS pre maxhs=%u(%d) tx=%u(%d) rx=%u(%d) pm=0x%x(%d) lanes=%u(%d)/%u(%d)\n",
        maxhs, e1, tx, e2, rx, e3, pm, e4, txl, e5, rxl, e6);
  IOLog("PONG_UFS: HS set gear=%u rx=%d lanes_rx=%d tx=%d lanes_tx=%d adapt=%d pwr=0x11 -> %d series=%d conn=0x%x\n",
        want, s_rx, s_lane_rx, s_tx, s_lane_tx, s_adapt, s_pm, series > 2 ? 2 : series, cs);
  IOLog("PONG_UFS: HS after tx=%u rx=%u pm=0x%x lanes=%u/%u\n", ntx, nrx, npm, ntxl, nrxl);
  return s_pm ? kIOReturnError : 0;
}

/* Read-only view of the link the boot loader left. PA_PWRMODE bits 7:4 are the
 * mode (1 = fast/HS, 2 = slow/PWM) and bits 3:0 the gear; this never writes. */
static void pongUfsLinkReport() {
  uint32_t maxhs = 0, tx = 0, rx = 0, pm = 0, txl = 0, rxl = 0;
  int e1 = pongUic(0x01, 0x1587u << 16, 0, 0, &maxhs);
  int e2 = pongUic(0x01, 0x1568u << 16, 0, 0, &tx);
  int e3 = pongUic(0x01, 0x1583u << 16, 0, 0, &rx);
  int e4 = pongUic(0x01, 0x1571u << 16, 0, 0, &pm);
  int e5 = pongUic(0x01, 0x1560u << 16, 0, 0, &txl);
  int e6 = pongUic(0x01, 0x1580u << 16, 0, 0, &rxl);
  IOLog("PONG_UFS: link maxhs=G%u txgear=G%u rxgear=G%u pwr=0x%x lanes=%u/%u err=%d%d%d%d%d%d\n",
        maxhs, tx, rx, pm, txl, rxl, e1, e2, e3, e4, e5, e6);
}

static bool pongUfsHsBringUp() {
  if (gPongUfsHsActive) return true;
  if (gPongUfsHsTried) return false;
  gPongUfsHsTried = true;

  gHost.gcc = mapPhys(0x00100000, 0x00200000, gHost.gccMap);
  gHost.phy = mapPhys(0x01d80000, 0x00002000, gHost.phyMap);
  if (!gHost.gcc || !gHost.phy) {
    IOLog("PONG_UFS: HS map failed gcc=%p phy=%p\n", (void *)gHost.gcc, (void *)gHost.phy);
    return false;
  }
  IOLog("PONG_UFS: HS bring-up start\n");
  if (pongGccClocksOn()) { IOLog("PONG_UFS: HS clock layer failed\n"); return false; }
  pongGccPhyReset();
  pongHcClockInit();
  pongDevRefClock(true);
  if (pongPhyWake()) { IOLog("PONG_UFS: HS phy not ready\n"); return false; }
  if (pongHbaRestart()) { IOLog("PONG_UFS: HS host restart failed\n"); return false; }
  int rr = pongUic(0x14, 0, 0, 0, nullptr);  // DME_RESET
  int re = pongUic(0x12, 0, 0, 0, nullptr);  // DME_ENABLE
  if (!re) pongDmeClockInit();
  int rl = pongUic(0x16, 0, 0, 0, nullptr);  // DME_LINKSTARTUP
  IOLog("PONG_UFS: HS uic reset=%d enable=%d link=%d\n", rr, re, rl);
  IOSleep(100);
  for (int i = 0; i < 30; i++) {
    if (!pongSetHsGear()) { gPongUfsHsActive = true; break; }
    IOSleep(100);
  }
  IOLog("PONG_UFS: HS bring-up %s\n", gPongUfsHsActive ? "active" : "FAILED");
  return gPongUfsHsActive;
}

}  // namespace

#endif  // PONG_UFS_HS_H
