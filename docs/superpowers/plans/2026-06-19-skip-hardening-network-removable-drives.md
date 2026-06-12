# Skip File Hardening on Network/Removable Drives Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let DFIR-ORC run from a network or removable drive by skipping output-file hardening on those drives, where a protected DACL of local SIDs would otherwise orphan the file at creation.

**Architecture:** Add one file-local helper in `WinApi.cpp` that classifies a path's drive type via `GetVolumePathNameW` + `GetDriveTypeW`, and gate `CreateFileApi` on it so remote/removable paths fall through to the existing plain-`CreateFileW` branch instead of `CreateFileSafeApi`. The gate sits before dispatch because the file is orphaned by the protected security descriptor at creation time, so the whole hardened path must be bypassed.

**Tech Stack:** C++17 (v141_xp toolset), Win32 (`GetVolumePathNameW`, `GetDriveTypeW`), `Orc::Log`.

## Global Constraints

- C++17 only (XP-capable `v141_xp` toolset).
- SPDX license header already present in the file — do not alter it.
- Strict minimum changes: no `SecurityHardening` enum, no `DaclOnly` value, no read-back-server-SID path, no `RemoveFileIfExists` poisoned-path tweak.
- Local fixed-disk behavior must remain unchanged (still fully hardened); if drive type is undeterminable, keep hardening (fail-closed).
- All edits confined to `src/OrcLib/Utils/WinApi.cpp`.
- Formatting per `.clang-format` (LLVM-based, 4-space indent, 120 cols). No unicode characters in source.
- Commits are performed by the USER, not by tools (git writes fail on the share).

---

### Task 1: Skip hardening on network/removable drives

**Files:**
- Modify: `src/OrcLib/Utils/WinApi.cpp` — add helper in the anonymous namespace (near `UpdateSecurityInfo`, before the closing `}  // namespace` at ~line 514); modify `CreateFileApi` (~line 701-742).

**Interfaces:**
- Consumes: `Orc::Log::Warn`, `GetVolumePathNameW`, `GetDriveTypeW` (all already available in this translation unit).
- Produces: file-local `bool IsReducedHardeningDrive(const wchar_t* lpFileName) noexcept` (anonymous namespace — not exported).

- [ ] **Step 1: Add the drive-type helper**

In the anonymous namespace in `src/OrcLib/Utils/WinApi.cpp` (immediately after the `UpdateSecurityInfo` function, which ends near line 463), add:

```cpp
// Network/removable drives reject a protected DACL of local SIDs (the share/device
// governs access), which would orphan the file at creation. Skip hardening there.
bool IsReducedHardeningDrive(const wchar_t* lpFileName) noexcept
{
    std::wstring volume(wcslen(lpFileName) + 1, L'\0');  // GetVolumePathNameW needs >= input length
    if (!GetVolumePathNameW(lpFileName, volume.data(), static_cast<DWORD>(volume.size())))
    {
        return false;  // cannot determine drive type: keep hardening (fail-closed)
    }

    const UINT type = GetDriveTypeW(volume.data());
    return type == DRIVE_REMOTE || type == DRIVE_REMOVABLE;
}
```

- [ ] **Step 2: Gate `CreateFileApi` on the helper**

In `CreateFileApi`, change the `needsHardening` declaration from `const bool` to a mutable `bool`, then insert the gate immediately after it is computed (before the existing `if (!needsHardening)` block).

Replace:

```cpp
    const bool needsHardening = HasWriteAccess(dwDesiredAccess) && !HasExplicitSecurityDescriptor(lpSecurityAttributes)
        && ReplaceNullSecurityAttributes;

    if (!needsHardening)
```

with:

```cpp
    bool needsHardening = HasWriteAccess(dwDesiredAccess) && !HasExplicitSecurityDescriptor(lpSecurityAttributes)
        && ReplaceNullSecurityAttributes;

    if (needsHardening && IsReducedHardeningDrive(lpFileName))
    {
        Log::Warn(L"Skipping file hardening on network/removable drive (path: {})", lpFileName);
        needsHardening = false;
    }

    if (!needsHardening)
```

The remainder of `CreateFileApi` is unchanged: when `needsHardening` is now false, the existing branch creates the file with a plain `CreateFileW` using the caller's own arguments (`CREATE_ALWAYS` handled natively by `CreateFileW`); otherwise it still calls `CreateFileSafeApi`.

- [ ] **Step 3: Build and verify it compiles clean**

Run:
```
cmake --build --preset dfir-orc-x64-llm-Debug
```
Expected: build succeeds with no new warnings or errors in `WinApi.cpp`.

- [ ] **Step 4: Embed the new binary**

Run:
```
Z:\orc103\dfir-orc\build-llm\DFIR-ORC-Debug.exe capsule add Z:\orc103\dfir-orc\build-llm\dfir-orc-x64\Debug\DFIR-ORC_x64.exe /force
```
Expected: capsule updated (a benign `MoveFileExW .new->exe [0x11 NOT_SAME_DEVICE]` log on the share is OK — a copy fallback still updates it in place).

- [ ] **Step 5: Run from a fresh subdir on the remote share and verify output**

Create a fresh empty subdirectory on `Z:` (avoids pre-existing server-side orphans), then run:
```
Z:\orc103\dfir-orc\build-llm\DFIR-ORC-Debug.exe /key=SystemInfo /overwrite /debug
```
from that subdir. Expected:
- The new warning `Skipping file hardening on network/removable drive (path: ...)` appears for each output file.
- A valid, non-empty `..._Little.7z` is produced (7z magic `37 7A BC AF 27 1C`, SHA1 computed OK) instead of a 0-byte / NOT_FOUND file.
- The process exits cleanly.

- [ ] **Step 6: Commit (performed by the USER)**

Suggested message:
```
OrcLib: Utils: WinApi: skip file hardening on network/removable drives
```
Files: `src/OrcLib/Utils/WinApi.cpp`, plus the spec and plan docs under `docs/superpowers/`.
