// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"

//
// Smart C-drive cleanup engine. Modeled on the junk-clean module of the
// LightC reference implementation: a fixed table of well-known junk
// categories (temporary directories, system and browser caches, logs,
// dumps, error reports), a parallel scanner with layered safety filters,
// and a delete pass that double-checks protected locations before touching
// anything. Scan paths follow the LightC category table; system-managed
// locations such as Windows.old are surfaced but never deleted here.
//
namespace SmartClean
{
    // Directory levels scanned below each category root
    inline constexpr int MaxScanDepth = 10;

    enum class Category : std::uint8_t
    {
        WindowsTemp, SystemCache, BrowserCache, RecycleBin, WindowsUpdate,
        DeliveryOptimization, DefenderCache, ThumbnailCache, LogFiles,
        MemoryDump, OldWindowsInstallation, AppCache, ThirdPartyCache,
        FontCache, ErrorReports, InstallerTemp, ClipboardCache, ShaderCache,
        Count
    };

    struct CategoryInfo
    {
        std::wstring_view nameId;     // Localization key of the display name
        std::wstring_view descId;     // Localization key of the description
        std::uint8_t risk;            // 1 = safe, 2 = medium, 3 = high impact
        bool systemManaged;           // Reported but never deletable from here
        bool recycleBin;              // Emptied through the shell, not per file
        std::span<const std::wstring_view> paths;    // %ENV% specs, may hold wildcards
        std::span<const std::wstring_view> patterns; // File name wildcards; empty = every file
    };

    // One deletable file discovered by the scan
    struct FileEntry
    {
        std::wstring path;
        ULONGLONG size = 0;
    };

    struct CategoryResult
    {
        std::vector<FileEntry> files;
        ULONGLONG size = 0;  // Logical bytes
        std::size_t count = 0;
    };

    // Shared between the dialog and the worker threads; counters are polled
    // through a dialog timer instead of window messages
    struct RecycleDrive
    {
        std::wstring root;
        ULONGLONG size = 0;
        int items = 0;
    };

    struct ScanState
    {
        std::array<CategoryResult, std::to_underlying(Category::Count)> results;
        std::atomic<int> categoriesDone{ 0 };
        std::atomic<int> filesFound{ 0 };
        std::atomic<bool> running{ false };
        std::atomic<bool> cancelled{ false };
        std::vector<RecycleDrive> recycleDrives; // Drives holding a non-empty bin
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

        // Plain arrays written by the worker before running flips back to
        // false; the dialog reads them only after observing completion
        std::array<int, std::to_underlying(Category::Count)> deletedPerCategory{};
        std::array<ULONGLONG, std::to_underlying(Category::Count)> freedPerCategory{};
    };

    const std::vector<CategoryInfo>& Categories();

    // Scan every category on parallel worker threads; blocks the calling
    // thread until all categories finish (or the scan was cancelled)
    void ScanAll(ScanState& state);

    // Delete the files of the selected categories; recycle bin entries empty
    // the corresponding bin through the shell instead of deleting files.
    // System-managed categories are always skipped.
    void CleanSelected(const ScanState& scan, const std::vector<bool>& selected, CleanState& state);

    // True when the location must never be deleted from this dialog
    bool IsDeleteBlocked(const std::wstring& path);
}
