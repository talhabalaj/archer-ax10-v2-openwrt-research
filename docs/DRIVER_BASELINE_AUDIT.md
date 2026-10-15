# BCM63178 D11 driver baseline audit

Checked 2026-10-15. Read-only inspection of pinned upstream source and previously collected metadata. No router probe, register access, module load, hardware execution, reboot, or public-checkout edit occurred. This report contains original analysis and source references, not proprietary code or microcode bytes.

## Result

None of the inspected upstream drivers supports the observed BCM63178/D11 core 130.1/AC-family PHY 51 combination. **The PHY type is not the central ambiguity:** upstream b43 defines type 0x0b as AC. The blocked layers are explicit revision selection, register/ucode contracts, unsupported AC PHY initialization, and the data path.

b43 and brcmsmac provide useful mac80211/D11 bus and microcode examples. brcmfmac instead requires a working firmware-side control/data protocol and matching firmware image; a cfg80211 interface or Broadcom PCI ID does not establish that contract. Do not begin by adding the AX10 PCI ID to brcmfmac or forcing b43 past its gates.

## Evidence identity and scope

Upstream Linux revision: `0fcb05b72344f40806cbe810de8872f1698bf053`. All Linux links below are pinned to that revision; absence claims cover the inspected files at that revision.

The device tuple comes from the existing reviewed hardware inventory: BCM63178, PCI 14e4:f6ca, D11 major 130/minor 1, PHY type 0x0b, PHY revision 51.0; its logical vendor device ID is 0x4492. This is separate from the BCM43217 2.4GHz radio. The running proprietary driver is 17.10.121.37; its previously acquired `wl.ko` has SHA-256 `8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`. A vendor PCI view does not by itself prove the radio is a conventional standalone PCIe FullMAC device.

The exact vendor GPL archive is the [AX10 V2 archive](https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz), SHA-256 `9670b07a1449dc7e338db9becbb019d154f0486569c1621a40f759843f655a87`. It contains older 17.10.86.5 support source and prebuilt core objects. Its source is comparison evidence, not an exact source reconstruction of the current binary. Vendor paths below are relative to `ax10v2_GPL/bcm675x/bcm963xx_router/`. The older WL base is `bcmdrivers/broadcom/net/wl/impl59/`.

## Exact upstream gates

| Driver | Positive source evidence | Consequence for BCM63178 |
| --- | --- | --- |
| b43 | [BCMA core table, main.c:117-128](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/main.c#L117) lists D11 revisions17,21,22,23,24,28,29,30,40,42; no 130. [b43.h:419-429](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/b43.h#L419) defines AC = 0x0b. [main.c:4537-4549](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/main.c#L4537) rejects AC revision > 1. | No bind entry and explicit PHY 51 rejection. Changing only either gate is insufficient. |
| b43 AC implementation | [Kconfig:151-158](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/Kconfig#L151) marks it BROKEN and warns of crashes. [phy_ac.c:16-88](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/phy_ac.c#L16) has allocation/access helpers and placeholder power operations, with no init/channel callback. | Existing AC code is not a usable PHY 51 implementation. A new PHY bring-up/calibration/channel implementation is required. |
| brcmsmac | [mac80211_if.c:96-103](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmsmac/mac80211_if.c#L96) binds D11 rev 17,23,24. [main.c:2231-2257](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmsmac/main.c#L2231) selects N/LCN microcode for those cores. | Useful host-MAC architecture, no AC 51/core 130 implementation. |
| brcmfmac PCIe | [pcie.c:86-114](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c#L86) maps chip revisions to firmware files; [pcie.c:2735-2765](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c#L2735) binds known PCI IDs. Neither table supplies63178/f6ca. | No existing target match or matching firmware setup. Generic D11 helpers are not evidence of a complete63178 device implementation. |

## Linux interface versus location of MAC work

b43's [ieee80211_ops and allocation, main.c:5188 and5585](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/main.c#L5188) and brcmsmac's [mac80211_if.c:963-983](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmsmac/mac80211_if.c#L963) expose frame/data-path callbacks to mac80211. The reusable boundary is kernel WLAN management plus explicit hardware callbacks; it does not remove hardware DMA, PHY, firmware, timing or capability work.

brcmfmac uses [cfg80211_ops, cfg80211.c:6003-6040](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c#L6003). Its [firmware interface, fwil.c:96-126](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil.c#L96) sends command/query messages through a protocol object. [proto.c:20-47](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmfmac/proto.c#L20) chooses BCDC or MSGBUF; PCIe explicitly selects MSGBUF at [pcie.c:2514-2524](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c#L2514).

The vendor GPL integration similarly supplies `cfg80211_ops` and allocates a wiphy (`bcmdrivers/broadcom/net/wl/impl59/sys/src/wl/sys/wl_cfg80211.c:11854,12031`). Calling that interface FullMAC does **not** prove the module is compatible with brcmfmac's firmware protocol. Existing current-binary symbols include host-resident `wlc_attach`, `wlc_bmac_attach`, `phy_ac_attach`, `wlc_ucode_download`, and `wlc_ucode_write`; previous two-function REA observations also show many host protocol attachments. The defensible distinction is: cfg80211-facing vendor host driver, versus mac80211-facing drivers, versus a firmware-driven brcmfmac transport. No63178 MSGBUF firmware contract has been established.

## Reusable bus and register boundaries

- [b43 bus.h:13-54](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/bus.h#L13) separates core enable/disable,16/32-bit reads/writes and block access. [bus.c:30-118](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/bus.c#L30) implements that over BCMA. This is an adaptable interface pattern, not proof the AX integrated radio can use the existing BCMA host path unchanged.
- [brcmsmac d11.h:107-163](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h#L107) describes an older fixed register structure: MAC control 0x120 and object address/data 0x160/0x164. [main.c:2212-2227](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmsmac/main.c#L2212) uses those object registers to write microcode.
- The current vendor symbol inventory contains `d11regs_select_offsets_tbl` and distinct shared/object-memory helpers (`wlc_bmac_write_shm`, `write_shmx`, `write_shm1`, `write_objmem16`). **Inference:** recover the core 130-specific offset-selection and memory-space contracts before reusing fixed old offsets. Symbol names alone do not recover those contracts.
- Vendor source-available backplane boundaries include `main/src/shared/siutils.c:3365` core selection, `:3752` core register access, `:3837` reset; `aiutils.c:892` register access and `:1087` reset. These are under the older WL path above. They are useful comparative reset/windowing source, not a verified current-core reset recipe.

## Three distinct firmware formats

1. **b43 extracted firmware.** [b43.h:632-655](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/b43.h#L632) defines an 8-byte header: type byte, format version byte, padding, big-endian 32-bit length. Ucode/PCM lengths are bytes; initval length is a record count. Initvals encode a big-endian 16-bit offset/width flag followed by16/32-bit data. [main.c:2262-2279](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/main.c#L2262) checks format version 1; [initval decoding at2763-2813](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/main.c#L2763) bounds register offsets below 0x1000. [Ucode upload at2616-2690](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/main.c#L2616) stops/initializes the PSM, writes big-endian words through SHM, starts it and waits for a firmware response. Firmware selection has no core 130 entry.
2. **brcmsmac bin-plus-index.** [mac80211_if.c:76-94](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmsmac/mac80211_if.c#L76) uses a binary and separate header file containing little-endian offset/length/index triplets. [Bounds checking at1678-1716](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmsmac/mac80211_if.c#L1678) validates indexed regions. [main.c:249-253,644-664](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmsmac/main.c#L249) represents initvals as little-endian address16/size16/value32 records ending with address 0xffff. Its microcode uploader consumes little-endian 32-bit words.
3. **brcmfmac PCIe ARM firmware.** [pcie.c:1693-1783](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c#L1693) halts an ARM core, copies firmware to device RAM, supplies NVRAM, releases the core and waits for firmware to publish a shared-RAM pointer. It then initializes firmware-described shared/ring state. That is not the same artifact or handshake as a D11 PSM microcode array embedded in `wl.ko`.

An extractor can repackage known words, but repackaging does not establish matching register maps, PSM banks, start sequence, ABI or firmware licensing. In particular, do not rename a vendor core 130 array to `ucode42.fw` or a brcmfmac firmware filename.

## Missing blocks for a working core 130 mac80211 path

The source comparison identifies these unresolved implementation blocks: integrated-core discovery and resource/interrupt ownership; power/clock/reset/windowing; revision-specific register offsets and object-memory selectors; complete microcode selection/upload/start/readiness contract; AC-PHY 51 radio initialization/calibration/channel/power control; DMA descriptors and TX/RX status/header interpretation; interrupt acknowledgement/recovery; keys, beacons, rate control and aggregation; HE capability reporting and feature-specific behavior. Existing [b43 DMA code](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/dma.c#L353) illustrates kernel DMA mapping/allocation patterns but does not establish core 130 descriptor compatibility. Board calibration/regulatory inputs remain private and separately required.

The smaller BCM43217/b43 experiment remains independent: old supported core 30/NPHY 17 code does not provide the missing AC 51 implementation.

## First component boundary

The [offline C components](../driver/README.md) now implement the initial
microcode selector and restricted raw initialization-record validation/replay.
They do not implement the full register map or microcode upload sequence. The
following requirements describe the wider boundary for that work.

Build an **offline core 130 register-and-microcode manifest validator** before a hardware-binding module. It should accept the exact current ELF as a caller-provided private input and:

- Verify artifact hash/ELF identity and retain the complete observed major/minor core/PHY tuple.
- Resolve the offset-table selection and microcode-selection references from bounded analysis. Record which facts are proven, which depend on unresolved pointers/relocations, and which are unknown.
- Check symbol/section bounds, word width/endianness, array lengths, initval termination, selected memory bank, and relationships among companion arrays. Never infer a core 130 format merely from its name or length.
- Produce a deterministic public-safe manifest of identities, sizes, hashes, register-role evidence and unresolved requirements. Keep original arrays, raw decompiler output and individual-device inputs private.
- Exercise any proposed upload-sequence generator against a fake register sink, with deterministic operation order and failure cases. This supplies testable component work without touching hardware or pretending a driver exists.

The concrete next REA entries are `d11regs_select_offsets_tbl`, `wlc_ucode_download` and `wlc_ucode_write`, followed by the relevant object-memory helpers. A later minimal kernel component can adopt the small read/write/reset interface pattern only after mapping and startup contracts are established; initially it should expose no TX path. Hardware binding/loading still requires the separately established boot/recovery and resource-ownership evidence. No such kernel component was implemented or tested by this audit.
