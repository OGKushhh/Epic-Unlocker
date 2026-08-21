#!/usr/bin/env python3
"""
ScreamAPI Master Generator
==========================
Parses a game's EOSSDK DLL export table and generates everything needed
for ScreamAPI proxy mode in ONE shot — no manual export hunting, no C# generator,
no separate steps.

Generates (in the specified output directory, default = current directory):
  1. LinkerExports64.h  (or LinkerExports32.h)
       -> #pragma comment(linker) forwarders for ALL DLL exports.
       -> Intercepted functions are commented out with // REMOVED:
  2. ScreamAPI.def
       -> Native exports ONLY for intercepted functions.
       -> NO forwarders (those live in the header).
       -> NO LIBRARY line (avoids LNK2001 conflicts with __declspec(dllexport)).

Does NOT copy files anywhere. Prints clear instructions at the end.

Usage:
    python master_generator.py <path_to_eossdk_dll>
    python master_generator.py <path_to_dll> --output <dir>
    python master_generator.py   (opens file picker — Windows only)

Requirements:
    - Python 3.6+
    - No external pip packages needed (pure struct-based PE parser)
"""

import struct
import sys
import os
import argparse

# ═══════════════════════════════════════════════════════════════════════════════
#  INTERCEPTED FUNCTIONS — implemented natively in eos-impl/
#  These get // REMOVED: in LinkerExports header and native entries in .def
# ═══════════════════════════════════════════════════════════════════════════════

INTERCEPTED = {
    # eos_initialize.cpp
    "EOS_Initialize",
    # eos_init.cpp
    "EOS_Platform_Create", "EOS_Platform_Release", "EOS_Platform_Tick",
    # eos_sdk.cpp
    "EOS_Platform_GetAuthInterface", "EOS_Platform_GetAchievementsInterface",
    "EOS_Platform_GetConnectInterface", "EOS_Platform_GetStatsInterface",
    "EOS_Platform_GetEcomInterface", "EOS_Platform_GetUIInterface",
    # eos_auth.cpp
    "EOS_Auth_Login", "EOS_Auth_GetLoggedInAccountByIndex",
    "EOS_Auth_AddNotifyLoginStatusChanged",
    # eos_connect.cpp
    "EOS_Connect_Login", "EOS_Connect_GetLoggedInUserByIndex",
    "EOS_Connect_AddNotifyLoginStatusChanged",
    # eos_achievements.cpp
    "EOS_Achievements_GetPlayerAchievementCount", "EOS_Achievements_UnlockAchievements",
    "EOS_Achievements_GetAchievementDefinitionCount", "EOS_Achievements_QueryDefinitions",
    "EOS_Achievements_CopyAchievementDefinitionV2ByIndex", "EOS_Achievements_DefinitionV2_Release",
    "EOS_Achievements_QueryPlayerAchievements", "EOS_Achievements_CopyPlayerAchievementByIndex",
    "EOS_Achievements_CopyPlayerAchievementByAchievementId", "EOS_Achievements_PlayerAchievement_Release",
    "EOS_Achievements_AddNotifyAchievementsUnlockedV2",
    "EOS_Achievements_CopyAchievementDefinitionByIndex", "EOS_Achievements_Definition_Release",
    "EOS_Achievements_AddNotifyAchievementsUnlocked",
    # eos_ecom_ownership.cpp
    "EOS_Ecom_QueryOwnership", "EOS_Ecom_QueryOwnershipBySandboxIds",
    "EOS_Ecom_QueryOwnershipToken",
    # eos_ecom_entitlements.cpp
    "EOS_Ecom_QueryEntitlements", "EOS_Ecom_GetEntitlementsCount",
    "EOS_Ecom_GetEntitlementsByNameCount", "EOS_Ecom_CopyEntitlementByIndex",
    "EOS_Ecom_CopyEntitlementByNameAndIndex", "EOS_Ecom_CopyEntitlementById",
    "EOS_Ecom_Entitlement_Release", "EOS_Ecom_QueryEntitlementToken",
    "EOS_Ecom_RedeemEntitlements",
    # eos_ecom_items.cpp
    "EOS_Ecom_GetItemReleaseCount",
    # eos_ecom_transactions.cpp
    "EOS_Ecom_Checkout",
    # eos_metrics.cpp
    "EOS_Metrics_BeginPlayerSession", "EOS_Metrics_EndPlayerSession",
    # eos_common.cpp
    "EOS_EpicAccountId_IsValid", "EOS_EResult_ToString",
    # eos_stats.cpp
    "EOS_Stats_IngestStat", "EOS_Stats_QueryStats", "EOS_Stats_GetStatsCount",
    "EOS_Stats_CopyStatByIndex", "EOS_Stats_CopyStatByName", "EOS_Stats_Stat_Release",
}

# Legacy functions from SDK v1.1.0 — always present in LegacyExports header
LEGACY_UNDECORATED = ["EOS_AccountId_FromString", "EOS_AccountId_IsValid", "EOS_AccountId_ToString"]


# ═══════════════════════════════════════════════════════════════════════════════
#  PURE PYTHON PE EXPORT PARSER — zero dependencies
# ═══════════════════════════════════════════════════════════════════════════════

def parse_pe_exports(filepath):
    """
    Parse a PE file's export table. Returns (export_names: list[str], machine: int).
    machine: 0x14c = i386 (32-bit), 0x8664 = AMD64 (64-bit)
    """
    with open(filepath, 'rb') as f:
        data = f.read()

    # ── DOS Header ──
    if data[0:2] != b'MZ':
        raise ValueError("Not a valid PE file (missing MZ signature)")
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]

    # ── PE Signature ──
    if data[e_lfanew:e_lfanew + 4] != b'PE\x00\x00':
        raise ValueError("Not a valid PE file (missing PE\x00\x00 signature)")

    # ── COFF Header (20 bytes) ──
    coff = e_lfanew + 4
    machine = struct.unpack_from('<H', data, coff)[0]
    num_sections = struct.unpack_from('<H', data, coff + 2)[0]
    opt_header_size = struct.unpack_from('<H', data, coff + 16)[0]

    # ── Optional Header ──
    opt = coff + 20
    opt_magic = struct.unpack_from('<H', data, opt)[0]
    is_pe32plus = (opt_magic == 0x20B)

    # DataDirectory offset within optional header
    dd_offset = opt + (112 if is_pe32plus else 96)

    # Export Directory = DataDirectory[0]
    export_rva = struct.unpack_from('<I', data, dd_offset)[0]
    export_size = struct.unpack_from('<I', data, dd_offset + 4)[0]

    if export_rva == 0:
        print("WARNING: DLL has no export table!", file=sys.stderr)
        return [], machine

    # ── Section Headers (RVA -> file offset mapping) ──
    sec_start = opt + opt_header_size
    sections = []
    for i in range(num_sections):
        s = sec_start + i * 40
        vsize = struct.unpack_from('<I', data, s + 8)[0]
        vaddr = struct.unpack_from('<I', data, s + 12)[0]
        rsize = struct.unpack_from('<I', data, s + 16)[0]
        roff  = struct.unpack_from('<I', data, s + 20)[0]
        sections.append((vaddr, vsize, roff, rsize))

    def rva_to_offset(rva):
        for va, vs, ro, rs in sections:
            if va <= rva < va + max(vs, rs):
                return ro + (rva - va)
        return None

    # ── Parse Export Directory Table ──
    ed = rva_to_offset(export_rva)
    if ed is None:
        raise ValueError(f"Cannot resolve export directory RVA 0x{export_rva:X}")

    num_names      = struct.unpack_from('<I', data, ed + 24)[0]
    name_ptr_rva   = struct.unpack_from('<I', data, ed + 32)[0]

    if num_names == 0 or name_ptr_rva == 0:
        return [], machine

    # ── Read export names ──
    nptr = rva_to_offset(name_ptr_rva)
    names = []
    for i in range(num_names):
        nrva = struct.unpack_from('<I', data, nptr + i * 4)[0]
        noff = rva_to_offset(nrva)
        if noff is not None:
            end = data.index(b'\x00', noff)
            names.append(data[noff:end].decode('ascii', errors='replace'))

    return sorted(names), machine


# ═══════════════════════════════════════════════════════════════════════════════
#  NAME UTILITIES
# ═══════════════════════════════════════════════════════════════════════════════

def undecorate(name):
    """Strip stdcall decoration (_prefix@suffix) -> undecorated name."""
    if name.startswith('_'):
        # _FuncName@N -> FuncName
        base = name[1:]
        at = base.rfind('@')
        if at > 0 and base[at+1:].isdigit():
            return base[:at]
        return base
    return name


def is_eos_export(name):
    """Check if an export name looks like an EOS SDK function."""
    u = undecorate(name)
    return u.startswith("EOS_") or u.startswith("_EOS_")


def arch_name(machine):
    if machine == 0x8664:
        return "64"
    elif machine == 0x14C:
        return "32"
    else:
        return f"unknown(0x{machine:X})"


def dll_basename(is_64):
    return f"EOSSDK-Win{'64' if is_64 else '32'}-Shipping"


def forward_dll_name(is_64):
    return dll_basename(is_64) + "_o"


def linker_header_name(is_64):
    return f"LinkerExports{arch_name(0x8664 if is_64 else 0x14C)}.h"


# ═══════════════════════════════════════════════════════════════════════════════
#  FILE PICKER (Windows-only, no external deps)
# ═══════════════════════════════════════════════════════════════════════════════

def pick_file():
    """Open a native Windows file picker dialog. Returns path or None."""
    try:
        import tkinter as tk
        from tkinter import filedialog
        root = tk.Tk()
        root.withdraw()
        path = filedialog.askopenfilename(
            title="Select the game's EOSSDK DLL (e.g. EOSSDK-Win64-Shipping.dll)",
            filetypes=[
                ("EOS SDK DLL", "EOSSDK-*.dll"),
                ("DLL files", "*.dll"),
                ("All files", "*.*"),
            ],
        )
        root.destroy()
        return path if path else None
    except Exception as e:
        print(f"File picker failed: {e}", file=sys.stderr)
        return None


# ═══════════════════════════════════════════════════════════════════════════════
#  GENERATORS
# ═══════════════════════════════════════════════════════════════════════════════

def generate_linker_exports_header(exports, is_64, fwd_dll):
    """Generate LinkerExports{64,32}.h content."""
    lines = ["#pragma once", ""]

    for name in exports:
        u = undecorate(name)
        if u in INTERCEPTED:
            lines.append(
                f'// REMOVED: #pragma comment(linker, "/export:{name}={fwd_dll}.{name}")'
                f'  (intercepted by eos-impl/ wrapper)'
            )
        else:
            lines.append(
                f'#pragma comment(linker, "/export:{name}={fwd_dll}.{name}")'
            )

    lines.append("")
    return "\n".join(lines)


def generate_def_file(exports, is_64):
    """Generate ScreamAPI.def content — native exports for intercepted functions only.
    No LIBRARY line, no forwarders.
    """
    # Collect intercepted exports, using UNDECORATED names for 64-bit
    # and DECORATED names for 32-bit (to match the DLL's export table exactly).
    native = []
    for name in exports:
        u = undecorate(name)
        if u in INTERCEPTED:
            # For 64-bit: use undecorated name (no _prefix, no @suffix)
            # For 32-bit: also use undecorated name — the .def file linker
            #   will apply the correct stdcall decoration automatically
            native.append(u)

    # Deduplicate (multiple decorated names could map to same undecorated)
    native = sorted(set(native))

    # Check for intercepted functions NOT found in the DLL at all
    missing = INTERCEPTED - {undecorate(n) for n in exports}
    if missing:
        native.extend(sorted(missing))

    lines = [
        "; ScreamAPI.def — Auto-generated by master_generator.py",
        f"; Source DLL: {('64-bit' if is_64 else '32-bit')} EOSSDK",
        f"; Total DLL exports: {len(exports)}",
        f"; Intercepted (native): {len(native)}",
        "; Forwarded (via LinkerExports header): omitted here",
        ";",
        "; This file lists ONLY the functions implemented natively in eos-impl/.",
        "; All other exports are forwarded via #pragma comment(linker) in",
        "; LinkerExports{64 if is_64 else 32}.h (which the .def takes precedence over",
        "; for these symbols, forcing them to resolve to the C++ wrappers).",
        ";",
        "; DO NOT add a LIBRARY line — it causes LNK2001 when combined with",
        "; __declspec(dllexport) from EOS_DECLARE_FUNC + EOS_BUILD_DLL.",
        ";",
        "EXPORTS",
    ]

    for name in native:
        lines.append(f"    {name}")

    lines.append("")
    return "\n".join(lines), native, missing if missing else set()


# ═══════════════════════════════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="ScreamAPI Master Generator — generate proxy files from any EOSSDK DLL",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python master_generator.py C:\\Games\\Mojave\\EOSSDK-Win64-Shipping.dll
  python master_generator.py EOSSDK-Win64-Shipping_o.dll --output generated
  python master_generator.py          (opens file picker)
""",
    )
    parser.add_argument(
        "dll",
        nargs="?",
        default=None,
        help="Path to the game's EOSSDK DLL (original or renamed _o version)",
    )
    parser.add_argument(
        "--output", "-o",
        default=".",
        help="Output directory for generated files (default: current directory)",
    )
    args = parser.parse_args()

    # ── Get DLL path ──
    dll_path = args.dll
    if not dll_path:
        print("No DLL path provided. Opening file picker...")
        dll_path = pick_file()
        if not dll_path:
            print("No file selected. Exiting.")
            sys.exit(1)

    dll_path = os.path.abspath(dll_path)
    if not os.path.isfile(dll_path):
        print(f"ERROR: File not found: {dll_path}", file=sys.stderr)
        sys.exit(1)

    # ── Parse DLL ──
    print(f"\n{'='*60}")
    print(f"  ScreamAPI Master Generator")
    print(f"{'='*60}")
    print(f"\n  DLL: {dll_path}")
    print(f"  Parsing export table...", end=" ", flush=True)

    try:
        exports, machine = parse_pe_exports(dll_path)
    except Exception as e:
        print(f"FAILED\n  ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    if not exports:
        print("FAILED")
        print(f"  ERROR: No exports found in the DLL. Is this really an EOS SDK DLL?",
              file=sys.stderr)
        sys.exit(1)

    is_64 = (machine == 0x8664)
    is_32 = (machine == 0x14C)
    arch = arch_name(machine)

    print(f"OK")
    print(f"  Architecture:  {arch}-bit")
    print(f"  Total exports: {len(exports)}")

    # Filter to only EOS_ exports (skip C++ mangled, internal, etc.)
    eos_exports = [n for n in exports if is_eos_export(n)]
    print(f"  EOS exports:   {len(eos_exports)}")

    # Count intercepted
    intercepted_in_dll = [n for n in eos_exports if undecorate(n) in INTERCEPTED]
    print(f"  Intercepted:   {len(intercepted_in_dll)}")
    print(f"  Forwarded:     {len(eos_exports) - len(intercepted_in_dll)}")

    # ── Determine output paths ──
    out_dir = os.path.abspath(args.output)
    os.makedirs(out_dir, exist_ok=True)

    hdr_name = linker_header_name(is_64)
    def_name = "ScreamAPI.def"
    hdr_path = os.path.join(out_dir, hdr_name)
    def_path = os.path.join(out_dir, def_name)

    fwd = forward_dll_name(is_64)

    # ── Generate LinkerExports header ──
    print(f"\n  Generating {hdr_name}...", end=" ", flush=True)
    header_content = generate_linker_exports_header(eos_exports, is_64, fwd)
    with open(hdr_path, "w", encoding="utf-8") as f:
        f.write(header_content)
    print("OK")

    # ── Generate ScreamAPI.def ──
    print(f"  Generating {def_name}...", end=" ", flush=True)
    def_content, native_list, missing_intercepted = generate_def_file(eos_exports, is_64)
    with open(def_path, "w", encoding="utf-8") as f:
        f.write(def_content)
    print("OK")

    # ── Warnings ──
    if missing_intercepted:
        print(f"\n  WARNING: {len(missing_intercepted)} intercepted functions not found in DLL:")
        for m in sorted(missing_intercepted):
            print(f"    - {m}")
        print(f"  (Added to .def anyway — will be no-ops if the game never calls them)")

    # Warn if no EOS exports found at all
    if not eos_exports:
        print(f"\n  WARNING: No EOS_ exports found in DLL!", file=sys.stderr)
        print(f"  Are you sure this is an EOS SDK DLL?", file=sys.stderr)
        sys.exit(1)

    # ── Summary ──
    print(f"\n{'='*60}")
    print(f"  FILES GENERATED")
    print(f"{'='*60}")
    print()
    print(f"  {hdr_path}")
    print(f"    -> {len(eos_exports)} EOS exports ({len(intercepted_in_dll)} REMOVED, {len(eos_exports) - len(intercepted_in_dll)} forwarded)")
    print()
    print(f"  {def_path}")
    print(f"    -> {len(native_list)} native exports (intercepted by eos-impl/)")
    print()

    # ── Instructions ──
    screamapi_src = os.path.join("ScreamAPI", "src")
    linker_dir = os.path.join(screamapi_src, "LinkerExports")

    target_header = os.path.join(linker_dir, hdr_name)
    target_def = os.path.join(screamapi_src, def_name)

    print(f"{'='*60}")
    print(f"  WHAT TO DO NOW")
    print(f"{'='*60}")
    print()
    print(f"  1. Copy the generated header to the ScreamAPI project:")
    print()
    print(f"     FROM:  {hdr_path}")
    print(f"     TO:    {target_header}")
    print()
    print(f"  2. Copy the generated .def to the ScreamAPI project:")
    print()
    print(f"     FROM:  {def_path}")
    print(f"     TO:    {target_def}")
    print()
    print(f"  3. If the game uses a 32-bit DLL and you need LegacyExports, make sure")
    print(f"     {os.path.join(linker_dir, 'LegacyExports' + arch + '.h')}")
    print(f"     still exists (it's always the same 3 legacy functions — don't regenerate it).")
    print()
    print(f"  4. Build ScreamAPI in Release | x64 (or Release | Win32 for 32-bit games).")
    print()
    print(f"  5. Deploy to the game:")
    print(f"     - Rename the game's original DLL to {dll_basename(is_64)}_o.dll")
    print(f"     - Copy your built DLL as {dll_basename(is_64)}.dll")
    print(f"       (output is at: build\\{arch}\\Release\\{dll_basename(is_64)}.dll)")
    print()
    print(f"  NOTE: If this is a NEW SDK version with functions ScreamAPI doesn't")
    print(f"  intercept yet, they'll be automatically forwarded — no code changes needed.")
    print(f"  Only the functions in the INTERCEPTED set have custom behavior.")
    print()


if __name__ == "__main__":
    main()
