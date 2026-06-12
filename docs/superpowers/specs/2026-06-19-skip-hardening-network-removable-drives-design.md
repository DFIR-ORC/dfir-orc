# Skip file hardening on network/removable drives

**Date:** 2026-06-19
**Component:** `src/OrcLib/Utils/WinApi.cpp`
**Status:** Approved design

## Problem

DFIR-ORC hardens every output file it creates. `CreateFileApi` dispatches write-access
creations with no explicit security descriptor to `CreateFileSafeApi`, which creates the file
with a restrictive *protected* DACL built from local SIDs (current user + `BA` + `SY`) and then
calls `SetSecurityInfo(OWNER | GROUP | PROTECTED_DACL)`.

On a **network share** (`DRIVE_REMOTE`) — and similarly on some **removable** media
(`DRIVE_REMOVABLE`) — the remote server / device cannot map those local SIDs to our SMB/session
identity. Writing a *protected* DACL containing only unmappable SIDs revokes our own access to
the just-created file: the file is orphaned at **creation time**. This surfaces as 0-byte or
"file not found" output and aborted collections when DFIR-ORC runs from such a drive.

## Goal

Let DFIR-ORC run from a network or removable drive by skipping file hardening on those drives,
with the **strict minimum** of changes.

## Non-goals (explicitly out of scope)

- No `SecurityHardening` enum and no `DaclOnly` degradation value.
- No read-back-server-SID confidentiality path.
- No `RemoveFileIfExists` "poisoned-path" defensive tweak.
- Behavior on local fixed disks is unchanged.

## Design

The fix lives entirely in `src/OrcLib/Utils/WinApi.cpp` and is roughly 15 lines.

### Where the gate goes

In `CreateFileApi`, **before** it dispatches to `CreateFileSafeApi` — not inside it. The file is
orphaned by the protected security descriptor at the moment of creation, so skipping any
post-creation step would not help; the whole hardened path must be bypassed. Gating here also
reuses the existing `if (!needsHardening)` plain-`CreateFileW` branch, which keeps the change
minimal.

### Drive detection helper

Add a file-local helper in the anonymous namespace:

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

If the drive type cannot be determined, the helper returns `false` so hardening is kept
(fail-closed for local disks).

### Gate in `CreateFileApi`

Relax `needsHardening` from `const bool` to a mutable `bool`, then add, immediately after it is
computed:

```cpp
if (needsHardening && IsReducedHardeningDrive(lpFileName))
{
    Log::Warn(L"Skipping file hardening on network/removable drive (path: {})", lpFileName);
    needsHardening = false;
}
```

The existing `if (!needsHardening)` branch then takes the plain `CreateFileW` path using the
caller's own arguments. `CREATE_ALWAYS` is handled natively by `CreateFileW` on that path, so no
extra `RemoveFileIfExists` is needed.

## Verification

1. Incremental build of the `dfir-orc-x64-llm-Debug` preset compiles clean.
2. `DFIR-ORC-Debug.exe capsule add ...\Debug\DFIR-ORC_x64.exe /force`.
3. Run `DFIR-ORC-Debug.exe /key=SystemInfo /overwrite /debug` from a **fresh subdir** on a remote
   share (`Z:`). Expect:
   - a valid, non-empty `..._Little.7z` with the 7z magic `37 7A BC AF 27 1C` and a computed SHA1,
   - the new `Log::Warn` ("Skipping file hardening on network/removable drive") for each output file,
   - the process exits cleanly.

Use a fresh directory / unique archive name to avoid pre-existing server-side orphans from earlier
protected-DACL runs.
