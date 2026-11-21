// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "JianYingClean.h"
#include "HelpersInterface.h"
#include "HelpersTasks.h"
#include "SmartClean.h"

namespace JianYingClean
{
    namespace
    {
        // ---------------------------------------------------------------------------
        // Whitelist table. Only the directories listed here are scanned or deleted;
        // everything else in the JianYing/CapCut data root (drafts under Projects,
        // presets, settings, cloud files, the installed apps) is unreachable by
        // construction because the scan never leaves these roots.
        // ---------------------------------------------------------------------------

        constexpr std::wstring_view TypeCache = IDS_JIANYING_TYPE_CACHE;
        constexpr std::wstring_view TypeLog = IDS_JIANYING_TYPE_LOG;
        constexpr std::wstring_view TypeTemp = IDS_JIANYING_TYPE_TEMP;

        constexpr std::array jianyingTargets
        {
            // Logs, reports, and telemetry
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\Log", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\VELog", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\Tracking", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\Crash", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\ExceptionMonitor", TypeLog },

            // Caches
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\EMaterial", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\CEF", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\TTNet", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\bach_private_cache", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\bach_private_cache", TypeCache },

            // Temporaries
            TargetInfo{ L"%LOCALAPPDATA%\\JianyingPro\\User Data\\LVOPTmp", TypeTemp },

            // International CapCut build keeps the same layout
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\Log", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\VELog", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\Tracking", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\Crash", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\ExceptionMonitor", TypeLog },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\EMaterial", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\CEF", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\TTNet", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\bach_private_cache", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\bach_private_cache", TypeCache },
            TargetInfo{ L"%LOCALAPPDATA%\\CapCut\\User Data\\LVOPTmp", TypeTemp },
        };

        // ---------------------------------------------------------------------------
        // Scanning
        // ---------------------------------------------------------------------------

        std::wstring ExpandSpec(const std::wstring_view spec)
        {
            wchar_t expanded[1024]{};
            if (ExpandEnvironmentStringsW(std::wstring(spec).c_str(), expanded,
                static_cast<DWORD>(std::size(expanded))) == 0 || expanded[0] == L'\0')
            {
                return {};
            }
            return expanded;
        }

        void AddScanFile(TargetResult& result, ScanState& state, std::wstring path, const ULONGLONG size)
        {
            result.files.push_back({ std::move(path), size });
            result.size += size;
            result.count++;
            state.filesFound.fetch_add(1, std::memory_order_relaxed);
        }

        void WalkDirectory(const std::wstring& dir, TargetResult& result, ScanState& state, const int depth)
        {
            if (state.cancelled.load(std::memory_order_relaxed) || depth > MaxScanDepth) return;

            WIN32_FIND_DATAW data{};
            HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
            if (find == INVALID_HANDLE_VALUE) return;
            do
            {
                if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
                const std::wstring full = dir + L"\\" + data.cFileName;
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    // Reparse points are never followed so junctions cannot
                    // pull outside content into the whitelist
                    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    {
                        WalkDirectory(full, result, state, depth + 1);
                    }
                }
                else
                {
                    AddScanFile(result, state, full,
                        (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
                }
            } while (!state.cancelled.load(std::memory_order_relaxed) && FindNextFileW(find, &data));
            FindClose(find);
        }

        void ScanTarget(const std::size_t index, ScanState& state)
        {
            TargetResult& result = state.results[index];
            WalkDirectory(result.path, result, state, 0);
        }

        // ---------------------------------------------------------------------------
        // Deleting
        // ---------------------------------------------------------------------------

        // True when the path sits inside one of the expanded whitelist roots;
        // the last defense against anything slipping past the scan
        bool IsWhitelisted(const std::wstring& path, const std::vector<std::wstring>& roots)
        {
            const std::wstring lower = MakeLower(path);
            return std::ranges::any_of(roots, [&lower](const std::wstring& root)
            {
                return lower.size() > root.size() + 1 &&
                    lower.compare(0, root.size(), root) == 0 &&
                    lower[root.size()] == L'\\';
            });
        }

        enum class Outcome { Deleted, Reboot, Failed, Missing };

        Outcome DeleteOne(const FileEntry& file, const std::vector<std::wstring>& roots)
        {
            if (!IsWhitelisted(file.path, roots)) return Outcome::Failed;
            if (SmartClean::IsDeleteBlocked(file.path)) return Outcome::Failed;

            const DWORD attributes = GetFileAttributesW(file.path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) return Outcome::Missing;

            BOOL deleted = DeleteFileW(file.path.c_str());
            if (!deleted && GetLastError() == ERROR_ACCESS_DENIED &&
                attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN))
            {
                // Read-only or hidden flags block deletion; clear and retry
                SetFileAttributesW(file.path.c_str(), FILE_ATTRIBUTE_NORMAL);
                deleted = DeleteFileW(file.path.c_str());
            }

            if (!deleted)
            {
                const DWORD error = GetLastError();
                if ((error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) &&
                    MoveFileExW(file.path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
                {
                    return Outcome::Reboot;
                }
                return Outcome::Failed;
            }
            return Outcome::Deleted;
        }

        // Collapse emptied directories bottom-up; the whitelist roots are
        // skipped so JianYing finds its folder layout intact on next launch
        void RemoveEmptyParents(std::vector<std::wstring> dirs, const std::vector<std::wstring>& roots)
        {
            std::ranges::sort(dirs, std::ranges::greater{});
            std::unordered_set<std::wstring> skip;
            for (const std::wstring& root : roots) skip.emplace(MakeLower(root));

            std::unordered_set<std::wstring> seen;
            for (const std::wstring& dir : dirs)
            {
                if (!seen.emplace(MakeLower(dir)).second) continue;
                if (skip.contains(MakeLower(dir))) continue;
                RemoveDirectoryW(dir.c_str());
            }
        }

        bool ProcessRunning(const std::wstring_view image)
        {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return false;

            PROCESSENTRY32W entry{ .dwSize = sizeof(entry) };
            bool found = false;
            if (Process32FirstW(snapshot, &entry))
            {
                do
                {
                    if (_wcsicmp(entry.szExeFile, std::wstring(image).c_str()) == 0)
                    {
                        found = true;
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
            return found;
        }
    }

    const std::vector<TargetInfo>& Targets()
    {
        static const std::vector<TargetInfo> table(jianyingTargets.begin(), jianyingTargets.end());
        return table;
    }

    void RunScan(ScanState& state)
    {
        state.running.store(true, std::memory_order_release);
        state.cancelled.store(false, std::memory_order_release);
        state.filesFound.store(0, std::memory_order_release);
        state.results.clear();

        // Keep only the whitelist directories that exist on this machine
        const std::vector<TargetInfo>& targets = Targets();
        for (const std::size_t i : std::views::iota(std::size_t{ 0 }, targets.size()))
        {
            const std::wstring expanded = ExpandSpec(targets[i].path);
            if (expanded.empty() || !FolderExists(expanded)) continue;
            state.results.push_back({ expanded, i, {}, 0, 0 });
        }

        // One worker per target; each writes only into its own result slot
        std::vector<std::jthread> workers;
        workers.reserve(state.results.size());
        for (const std::size_t i : std::views::iota(std::size_t{ 0 }, state.results.size()))
        {
            workers.emplace_back([i, &state] { ScanTarget(i, state); });
        }
        for (std::jthread& worker : workers) worker.join();

        state.running.store(false, std::memory_order_release);
    }

    void RunClean(const ScanState& scan, const std::vector<bool>& selected, CleanState& state)
    {
        state.running.store(true, std::memory_order_release);
        state.cancelled.store(false, std::memory_order_release);
        state.filesProcessed.store(0, std::memory_order_relaxed);
        state.filesDeleted.store(0, std::memory_order_relaxed);
        state.filesReboot.store(0, std::memory_order_relaxed);
        state.filesFailed.store(0, std::memory_order_relaxed);
        state.bytesFreed.store(0, std::memory_order_relaxed);
        state.deletedPerTarget.assign(scan.results.size(), 0);
        state.freedPerTarget.assign(scan.results.size(), 0);

        // The expanded roots of the selected targets bound every delete
        std::vector<std::wstring> roots;
        std::vector<std::size_t> order;
        int total = 0;
        for (const std::size_t i : std::views::iota(std::size_t{ 0 }, scan.results.size()))
        {
            if (!selected[i]) continue;
            roots.push_back(scan.results[i].path);
            order.push_back(i);
            total += static_cast<int>(scan.results[i].count);
        }
        state.filesTotal.store(total, std::memory_order_release);

        std::vector<std::wstring> parentDirs;
        for (const std::size_t i : order)
        {
            if (state.cancelled.load(std::memory_order_relaxed)) break;

            for (const FileEntry& file : scan.results[i].files)
            {
                if (state.cancelled.load(std::memory_order_relaxed)) break;
                state.filesProcessed.fetch_add(1, std::memory_order_relaxed);
                switch (DeleteOne(file, roots))
                {
                case Outcome::Deleted:
                    state.filesDeleted.fetch_add(1, std::memory_order_relaxed);
                    state.bytesFreed.fetch_add(file.size, std::memory_order_relaxed);
                    state.deletedPerTarget[i] += 1;
                    state.freedPerTarget[i] += file.size;
                    if (const size_t slash = file.path.find_last_of(L'\\'); slash != std::wstring::npos)
                    {
                        parentDirs.push_back(file.path.substr(0, slash));
                    }
                    break;
                case Outcome::Reboot:
                    state.filesReboot.fetch_add(1, std::memory_order_relaxed);
                    break;
                case Outcome::Failed:
                    state.filesFailed.fetch_add(1, std::memory_order_relaxed);
                    break;
                case Outcome::Missing:
                    break;
                }
            }
        }

        if (!state.cancelled.load(std::memory_order_relaxed) && !parentDirs.empty())
        {
            RemoveEmptyParents(std::move(parentDirs), roots);
        }

        state.running.store(false, std::memory_order_release);
    }

    bool IsAppRunning()
    {
        return ProcessRunning(L"JianyingPro.exe") || ProcessRunning(L"CapCut.exe");
    }
}
