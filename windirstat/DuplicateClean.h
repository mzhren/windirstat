// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"

class CItem;

//
// Duplicate file cleanup engine. Walks the in-memory scan tree, groups files
// by size and content hash (reusing the model's own hashing), and removes the
// copies the user selects through the Recycle Bin so removal stays reversible.
// One copy per group is always kept on the dialog side.
//
namespace DuplicateClean
{
    struct FileEntry
    {
        std::wstring path;
        ULONGLONG size = 0;
        std::wstring modified;
    };

    struct DupGroup
    {
        std::vector<FileEntry> files;  // Sorted oldest first; the default keeper
        ULONGLONG size = 0;            // Per-file logical size
        ULONGLONG wasted = 0;          // size * (count - 1)
    };

    // Shared between the dialog and the worker threads; polled by timer
    struct ScanState
    {
        std::vector<DupGroup> groups;
        std::atomic<int> filesCollected{ 0 };
        std::atomic<int> candidates{ 0 };
        std::atomic<int> hashed{ 0 };
        std::atomic<bool> running{ false };
        std::atomic<bool> cancelled{ false };
    };

    void RunScan(CItem* root, ScanState& state);

    struct CleanState
    {
        std::atomic<int> filesTotal{ 0 };
        std::atomic<int> processed{ 0 };
        std::atomic<int> removed{ 0 };
        std::atomic<int> failed{ 0 };
        std::atomic<ULONGLONG> bytesFreed{ 0 };
        std::atomic<bool> running{ false };
        std::atomic<bool> cancelled{ false };
        std::vector<std::wstring> removedPaths;  // Worker-written; read after done
    };

    void RunClean(const std::vector<std::wstring>& paths, const std::vector<ULONGLONG>& sizes,
        CleanState& state);
}
