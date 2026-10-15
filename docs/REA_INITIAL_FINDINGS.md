# Initial REA native-driver findings

REA 6.5.0 successfully opened the current ARM32 relocatable `wl.ko` through
Ghidra 12.1.4, found the two selected function names, returned nonempty
pseudocode for each, and closed its native session. The target SHA-256 matches
the module inventory, and its original bytes remained unchanged.

The [sanitized Evidence summary](../findings/REA_EVIDENCE_SUMMARY.json) identifies
the exact target, tool versions, operations, Evidence IDs, and query scope.
Raw records and decompiler output remain private; this summary is not a full
portable REA Evidence export.

## `wl_cfg80211_attach`

The returned pseudocode includes the named kernel interfaces `wiphy_new_nm`,
`wiphy_register`, `wiphy_apply_custom_regulatory` and `wiphy_free`, alongside
vendor band, event-handler and rfkill initialization names.

This is static evidence of a cfg80211-facing initialization boundary in the
shipped module. It is a useful entry point for comparing the published older
cfg80211 integration source with the current binary. It does not establish
modern-kernel ABI compatibility or successful runtime registration.

Evidence: `ev_8eeff897f45c82e3abb8597a67d033248e4468d7cc6dc3ded7c705541ab9c2bc`.

## `wlc_attach`

The much larger returned pseudocode includes low-level MAC initialization
names and many subsystem attachment names, including HT, VHT, HE, aggregation,
key-management, scanning and TWT. These are selected names visible in the
pseudocode, not a complete resolved-call graph or proof that every branch runs
on the AX10 board.

**Inference:** the missing core spans substantial MAC/PHY and protocol state;
the task is not confined to a small PCI-ID or cfg80211 wrapper patch. The next
useful boundary is the low-level MAC/PHY attach/reset/DMA sequence and its
relation to the source-available HND/OS/backplane interfaces.

Evidence: `ev_d30632c25057d25aa2675b4054046652815edaaf6a4a605e3c532265732674aa`.

## Limits and next probes

- Only two named functions were queried. No whole-driver reconstruction or
  current-kernel port is complete.
- Pseudocode is not original source. Structure types, fields and indirect call
  targets remain uncertain. The output includes overlapping-global warnings;
  relocatable import/relocation and type context need corroboration.
- Compare the source-available integration/HND paths first; use resolved call
  and register/DMA evidence for the remaining closed behavior.
- The 2.4 GHz BCMA/b43 route and board PCIe/Ethernet work remain separate from
  reverse-engineering the AX radio core.
- No target driver was executed on the analysis host, patched or loaded into
  another kernel. No radio operation, boot or firmware image was validated.
