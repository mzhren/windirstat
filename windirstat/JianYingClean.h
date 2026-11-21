// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"

//
// JianYing (剪映/CapCut) cache and log cleanup engine. A whitelist of the
// well-known cache, log, and temporary directories that JianYing Pro and its
// international CapCut build maintain under %LOCALAPPDATA%; every file found
// below them is removable while drafts (Projects), presets, settings, cloud
// files, and the applications themselves are never touched. Modeled on the
// SmartClean scan/clean flow: parallel per-target scanning with atomic
// progress and a sequential delete pass behind safety gates.
//
namespace JianYingClean
{
    // Directory levels scanned below each target root
    inline constexpr int MaxScanDepth = 16;

    struct TargetInfo
    {
        std::wstring_view path;    // %ENV% spec, no wildcards
        std::wstring_view typeId;  // Localization key of the type label
    };

    const std::vector<TargetInfo>& Targets();

    // One deletable file discovered by the scan
    struct FileEntry
    {
        std::wstring path;
        ULONGLONG size = 0;
    };

    // An existing whitelist directory with its collected files
    struct TargetResult
    {
        std::wstring path;              // Expanded existing directory
        std::size_t type = 0;           // Index into Targets()
        std::vector<FileEntry> files;
        ULONGLONG size = 0;             // Logical bytes
        std::size_t count = 0;
    };

    // Shared between the dialog and the worker threads; counters are polled
    // through a dialog timer instead of window messages
    struct ScanState
    {
        std::vector<TargetResult> results;   // Existing targets only
        std::atomic<int> filesFound{ 0 };
        std::atomic<bool> running{ false };
        std::atomic<bool> cancelled{ false };
    };

    struct CleanState
    {
        std::atomic<int> filesTotal{ 0 };
        std::atomic<int> filesProcessed{ 0 };
        std::atomic<int> filesDeleted{ 0 };
        std::atomic<int> filesReboot{ 0 };
        std::atomic<int> filesFailed{ 0 };
        std::atomic<ULONGLONG> bytesFreed{ 0 };
        std::atomic<bool> running{ false };
        std::atomic<bool> cancelled{ false };

        // Plain vectors written by the worker before running flips back to
        // false; the dialog reads them only after observing completion
        std::vector<int> deletedPerTarget;
        std::vector<ULONGLONG> freedPerTarget;
    };

    // Expand the whitelist specs, keep the directories that exist, and size
    // them on parallel worker threads; blocks the calling thread until every
    // target finished (or the scan was cancelled)
    void RunScan(ScanState& state);

    // Delete the files of the selected targets. The whitelist roots and
    // everything outside them are never removed; emptied directories are
    // collapsed afterwards while the roots themselves are kept.
    void RunClean(const ScanState& scan, const std::vector<bool>& selected, CleanState& state);

    // True when JianyingPro.exe or CapCut.exe is currently running
    bool IsAppRunning();
}
