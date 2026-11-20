// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

//
// Application data migration engine. Known app data folders (WeChat chat
// files, WPS cloud cache and backups) are detected on their default C:
// locations, sized, and then moved to a folder on another drive through the
// shell while a directory junction is left at the original path so the
// applications keep working unchanged.
//
namespace AppMigrate
{
#pragma message("=== M_MID after namespace open ===")
    enum class Preset : std::uint8_t
    {
        WeChat,
        Wps,
        Count
    };

#pragma message("=== M_MID2 after enum ===")
    struct PresetInfo
    {
        std::wstring_view titleId;   // Dialog caption and toolbar tooltip
        std::wstring_view descId;    // Header description
        std::wstring_view noteId;    // Bottom guidance note
        std::span<const std::wstring_view> paths;  // %ENV% candidate folders
    };

    const PresetInfo& Presets(Preset preset);

    struct Candidate
    {
        std::wstring path;
        ULONGLONG size = 0;          // Recursive logical size
    };

    struct DetectState
    {
        std::vector<Candidate> candidates;
        std::atomic<int> sized{ 0 };
        std::atomic<bool> running{ false };
        std::atomic<bool> cancelled{ false };
    };

    // Expand the preset paths, keep the ones that exist and size them
    void RunDetect(Preset preset, DetectState& state);

    // Directory junction helpers shared with the file info pane
    bool CreateDirectoryJunction(const std::wstring& linkPath, const std::wstring& targetPath);

    // Last path segment for progress display
    std::wstring LeafName(const std::wstring& path);

    struct MigrateState
    {
        std::atomic<int> filesTotal{ 0 };        // Candidates being migrated
        std::atomic<int> currentIndex{ -1 };     // Candidate the worker is on
        std::atomic<int> done{ 0 };
        std::atomic<int> failed{ 0 };
        std::atomic<int> locked{ 0 };            // Moved with files left behind
        std::atomic<ULONGLONG> bytesMoved{ 0 };
        std::atomic<ULONGLONG> bytesTotal{ 0 };
        std::atomic<bool> running{ false };
        std::atomic<bool> cancelled{ false };
        std::vector<std::wstring> junctionFailed;  // Moved but junction failed
        std::vector<std::wstring> failedPaths;     // Not moved at all
    };

#pragma message("=== M_END before RunMigration ===")
    // Moves each source folder under the destination and leaves a junction
    void RunMigration(Preset preset, const std::vector<std::wstring>& sources,
        const std::vector<ULONGLONG>& sizes, const std::wstring& dest, MigrateState& state);
}
