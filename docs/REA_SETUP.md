# REA native analysis setup

## Scope and current status

Offline analysis tooling for a Broadcom ARMv7 Linux WiFi driver. No target driver
is executed, and no router firmware, settings, or flash contents are changed.
Engine preparation is complete. The first two native function queries are documented in [REA_INITIAL_FINDINGS.md](REA_INITIAL_FINDINGS.md); driver reconstruction and hardware validation remain pending. Tool archives,
executables, raw diagnostics, and host configuration are excluded from the public
research repository.

## Versions and official source

- REA CLI: `rea-agents@6.5.0`, invoked with `npx -y rea-agents@6.5.0`.
- Node.js: `26.11.1`.
- Ghidra: `12.1.4 PUBLIC`, build `20260921`, revision
  `8b6bbb857accdfa20dc5b2f5dea471178c2e9fbc`.
- Existing full JDK: OpenJDK `21.0.12.1`, ARM64, supplied by Homebrew.
- Native compiler: Apple clang `21.0.0`; GNU Bison `2.3`; system `make`.

The Ghidra archive is
[`ghidra_12.1.4_PUBLIC_20260921.zip`](https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip)
from the [official NSA release](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build).
Its downloaded size and SHA-256 were checked against official release metadata:

```text
569649598 bytes
ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db
```

An additional JDK download was canceled after the existing matching JDK was found.
No new Java installation or global configuration was needed.

## macOS ARM64 decompiler recipe

The release archive contains decompiler source but no macOS native decompiler.
The release Makefile defaults to macOS x86-64; command-line overrides select
ARM64 without editing source or using Rosetta:

```sh
cd "$GHIDRA_INSTALL_DIR/Ghidra/Features/Decompiler/src/decompile/cpp"
make -j 4 ghidra_opt ARCH_TYPE="-arch arm64" OSDIR=mac_arm_64
decompiler_dir="$GHIDRA_INSTALL_DIR/Ghidra/Features/Decompiler/os/mac_arm_64"
mkdir -p "$decompiler_dir"
test ! -e "$decompiler_dir/decompile" && test ! -L "$decompiler_dir/decompile" &&
  install -m 755 ghidra_opt "$decompiler_dir/decompile"
```

If a host compiler rejects the inherited deployment-target flag, the documented
fallback is to repeat `make` with `ADDITIONAL_FLAGS="-w"`. The installed executable
must be Mach-O ARM64; directory naming alone is insufficient.

Use caller-selected absolute installation and JDK paths only in the child process:

```sh
env PATH="/absolute/path/to/node26/bin:$PATH" \
  GHIDRA_INSTALL_DIR=/absolute/path/to/ghidra_12.1.4_PUBLIC \
  JAVA_HOME=/absolute/path/to/full-arm64-jdk21 \
  npx -y rea-agents@6.5.0 doctor --provider ghidra --json
```

REA requires a full 64-bit JDK with `javac`, Ghidra 12.1.x, and a native decompiler
whose architecture matches both the host and the JVM. Ghidra 12.1.4 requires JDK
21 or newer. Doctor readiness does not prove an MCP connection or successful
driver import, auto-analysis, or decompilation.

The provider-scoped doctor exited successfully with `healthy: true`: Node,
Ghidra configuration/platform/architecture/version/headless launcher, native
decompiler, and Java checks all passed. The compiled decompiler was observed as
Mach-O ARM64. The wider `environment_healthy` result remains false because other
optional providers and client registrations are not configured; they are outside
this CLI workflow. A shell-selected older Node failed its prerequisite check
until the child invocation selected the existing Node 26 executable directory.

No MCP registration, client approval settings, Gatekeeper quarantine flags,
global environment, Hopper installation, or host security protections are changed.
The native build uses only the bundled decompiler source; it is not a full Ghidra
source build.

References: [REA installation guidance](https://github.com/morluto/rea/blob/main/docs/installation.md#macos-arm64-native-decompiler),
[Ghidra release Makefile](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/Decompiler/src/decompile/cpp/Makefile),
[Ghidra getting started](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/GhidraDocs/GettingStarted.md).
