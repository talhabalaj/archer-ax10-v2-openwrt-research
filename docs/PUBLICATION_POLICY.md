# Publication boundary

Public artifacts are explicitly selected research Markdown, original inspection
scripts, a license, ignore rules, and selected metadata JSON. The private working
directory is not added to Git or copied recursively.

Excluded inputs include:

- Passwords, session/encryption tokens and authentication headers.
- SSH private keys, host private keys, keychain/credential-store exports and
  agent/client configuration.
- Router backups, exported configuration, user/ISP identifiers, MAC/serial
  identifiers, NVRAM, OTP and calibration data.
- Raw router command output, device trees, complete logs and raw REA diagnostics.
- Firmware/driver binaries, vendor archives, extracted filesystems and toolchain
  archives. Their independently available source URLs and hashes may be listed.

The hardware inventory retains model/revision, chip/PCI/core identities,
kernel/driver version and the exposed MTD geometry. These identify a hardware
family, not a person or an individual router.

The public source archive inspector emits only an allowlist of vendor filenames,
sizes and hashes. The public module summary emits only portable metadata and
counts. The latter can read arbitrary caller-selected binaries; the caller is
responsible for selecting only artifacts they are authorized to analyze.

Before publication, inspect the exact staged file list, search for secret/key
formats and private local paths, compare against known local secrets without
printing them, and independently review the selected files. These checks reduce
risk; they are not a mathematical guarantee about arbitrary future additions.
