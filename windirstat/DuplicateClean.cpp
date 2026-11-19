// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "DuplicateClean.h"
#include "SmartClean.h"
#include "Item.h"

namespace DuplicateClean
{
    namespace
    {
        // Walk the in-memory scan tree collecting real local files. The
        // <Hardlinks> pseudo tree is skipped: those entries reference the same
        // physical content as their originals and would show as false duplicates.
        void CollectFiles(CItem* item, std::vector<CItem*>& files)
        {
            if (item->IsTypeOrFlag(IT_HLINKS, IT_HLINKS_SET)) return;
            if (item->IsTypeOrFlag(IT_FILE))
            {
                if (item->GetSizeLogical() > 0 && item->SupportsFilesystemApis() &&
                    !item->GetPathLong().empty())
                {
                    files.push_back(item);
                }
                return;
            }
            for (CItem* child : item->GetChildren()) CollectFiles(child, files);
        }

        std::wstring FormatModified(const FILETIME& modified)
        {
            SYSTEMTIME utc{}, local{};
            if (!FileTimeToSystemTime(&modified, &utc) ||
                !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
            {
                return {};
            }
            return std::format(L"{:04d}-{:02d}-{:02d} {:02d}:{:02d}",
                local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute);
        }

        void MakeEntry(CItem* file, FileEntry& entry)
        {
            entry.path = file->GetPath();
            entry.size = file->GetSizeLogical();
            entry.modified = FormatModified(file->GetLastChange());
        }

        ULONGLONG ModifiedKey(const CItem* file)
        {
            const FILETIME modified = file->GetLastChange();
            return (static_cast<ULONGLONG>(modified.dwHighDateTime) << 32) | modified.dwLowDateTime;
        }
    }

    void RunScan(CItem* root, ScanState& state)
    {
        state.running.store(true, std::memory_order_release);
        state.cancelled.store(false, std::memory_order_release);
        state.filesCollected.store(0, std::memory_order_release);
        state.candidates.store(0, std::memory_order_release);
        state.hashed.store(0, std::memory_order_release);
        state.groups.clear();

        // 1. Collect real local files from the in-memory scan tree
        std::vector<CItem*> files;
        CollectFiles(root, files);
        state.filesCollected.store(static_cast<int>(files.size()), std::memory_order_release);

        // 2. Only files sharing their size with at least one other file need hashing
        std::unordered_map<ULONGLONG, std::vector<CItem*>> bySize;
        for (CItem* file : files) bySize[file->GetSizeLogical()].push_back(file);
        std::vector<CItem*> candidates;
        for (auto& [size, group] : bySize)
        {
            if (group.size() >= 2) candidates.insert(candidates.end(), group.begin(), group.end());
        }
        std::ranges::sort(candidates, [](const CItem* a, const CItem* b)
        {
            return a->GetSizeLogical() > b->GetSizeLogical();
        });
        state.candidates.store(static_cast<int>(candidates.size()), std::memory_order_release);

        // 3. Hash candidates in parallel; a dummy queue never suspends the workers
        BlockingQueue<CItem*> queue;
        std::atomic<size_t> next{ 0 };
        std::mutex hashMutex;
        std::map<std::pair<ULONGLONG, std::vector<BYTE>>, std::vector<CItem*>> hashed;

        const unsigned threadCount = std::min<unsigned>(8,
            std::max(1u, std::thread::hardware_concurrency()));
        std::vector<std::jthread> workers;
        workers.reserve(threadCount);
        for (const unsigned i : std::views::iota(0u, threadCount))
        {
            workers.emplace_back([&]
            {
                for (;;)
                {
                    if (state.cancelled.load(std::memory_order_relaxed)) return;
                    const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= candidates.size()) return;

                    CItem* file = candidates[index];
                    try
                    {
                        auto hash = file->GetFileHash(std::numeric_limits<ULONGLONG>::max(),
                            &queue, false);
                        if (!hash.empty())
                        {
                            std::scoped_lock lock(hashMutex);
                            hashed[{file->GetSizeLogical(), std::move(hash)}].push_back(file);
                        }
                    }
                    catch (...)
                    {
                        // Unreadable content simply keeps the file out of any group
                    }
                    state.hashed.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (std::jthread& worker : workers) worker.join();

        if (state.cancelled.load(std::memory_order_relaxed))
        {
            state.running.store(false, std::memory_order_release);
            return;
        }

        // 4. Same content and size with at least two copies makes a group
        for (auto& [key, items] : hashed)
        {
            if (items.size() < 2) continue;

            // Oldest copy first: it is the default keeper in the dialog
            std::ranges::sort(items, [](const CItem* a, const CItem* b)
            {
                const ULONGLONG left = ModifiedKey(a);
                const ULONGLONG right = ModifiedKey(b);
                if (left != right) return left < right;
                return a->GetPath() < b->GetPath();
            });

            DupGroup group;
            group.size = key.first;
            for (CItem* file : items)
            {
                FileEntry entry;
                MakeEntry(file, entry);
                group.files.push_back(std::move(entry));
            }
            group.wasted = group.size * (group.files.size() - 1);
            state.groups.push_back(std::move(group));
        }

        std::ranges::sort(state.groups, [](const DupGroup& a, const DupGroup& b)
        {
            if (a.wasted != b.wasted) return a.wasted > b.wasted;
            return a.files.size() > b.files.size();
        });

        state.running.store(false, std::memory_order_release);
    }

    void RunClean(const std::vector<std::wstring>& paths, const std::vector<ULONGLONG>& sizes,
        CleanState& state)
    {
        state.running.store(true, std::memory_order_release);
        state.cancelled.store(false, std::memory_order_release);
        state.filesTotal.store(static_cast<int>(paths.size()), std::memory_order_release);
        state.processed.store(0, std::memory_order_release);
        state.removed.store(0, std::memory_order_release);
        state.failed.store(0, std::memory_order_release);
        state.bytesFreed.store(0, std::memory_order_release);
        state.removedPaths.clear();

        for (const int index : std::views::iota(0, static_cast<int>(paths.size())))
        {
            if (state.cancelled.load(std::memory_order_relaxed)) break;
            state.processed.fetch_add(1, std::memory_order_relaxed);
            const std::wstring& path = paths[static_cast<std::size_t>(index)];

            // System-critical locations never leave through this engine
            if (SmartClean::IsDeleteBlocked(path))
            {
                state.failed.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            // Move to the Recycle Bin so the removal can be undone
            std::wstring list = path;
            list.push_back(L'\0'); list.push_back(L'\0');
            SHFILEOPSTRUCTW op{};
            op.hwnd = GetMainWindowHandle();
            op.wFunc = FO_DELETE;
            op.pFrom = list.c_str();
            op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
            if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted)
            {
                state.removed.fetch_add(1, std::memory_order_relaxed);
                state.bytesFreed.fetch_add(sizes[static_cast<std::size_t>(index)],
                    std::memory_order_relaxed);
                state.removedPaths.push_back(path);
            }
            else
            {
                state.failed.fetch_add(1, std::memory_order_relaxed);
            }
        }

        state.running.store(false, std::memory_order_release);
    }
}
