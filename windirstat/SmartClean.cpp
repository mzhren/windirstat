// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "SmartClean.h"
#include "HelpersInterface.h"

namespace SmartClean
{
    namespace
    {
        // ---------------------------------------------------------------------------
        // Category table. Paths mirror the LightC junk-clean scanner; wildcards are
        // expanded before scanning. Installer patch caches (Windows\Installer) are
        // deliberately absent: deleting them can break uninstall/repair.
        // ---------------------------------------------------------------------------

        constexpr std::array windowsTempPaths
        {
            std::wstring_view(L"%TEMP%"), std::wstring_view(L"%TMP%"), std::wstring_view(L"%SystemRoot%\\Temp")
        };
        constexpr std::array systemCachePaths
        {
            std::wstring_view(L"%SystemRoot%\\Prefetch"), std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Windows\\Caches")
        };
        constexpr std::array browserCachePaths
        {
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Code Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\GPUCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Profile *\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Profile *\\Code Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\ShaderCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Code Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\GPUCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Profile *\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Profile *\\Code Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\ShaderCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles\\*\\cache2"),
            std::wstring_view(L"%APPDATA%\\Mozilla\\Firefox\\Profiles\\*\\cache2"),
            std::wstring_view(L"%LOCALAPPDATA%\\BraveSoftware\\Brave-Browser\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\BraveSoftware\\Brave-Browser\\User Data\\Default\\Code Cache"),
            std::wstring_view(L"%APPDATA%\\Opera Software\\Opera Stable\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Tencent\\QQBrowser\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Tencent\\QQBrowser\\User Data\\Default\\Code Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\360Chrome\\Chrome\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\360Chrome\\Chrome\\User Data\\Default\\Code Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\360se6\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\UCBrowser\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Vivaldi\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\YandexBrowser\\User Data\\Default\\Cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\CentBrowser\\User Data\\Default\\Cache")
        };
        constexpr std::array windowsUpdatePaths
        {
            std::wstring_view(L"%SystemRoot%\\SoftwareDistribution\\Download")
        };
        constexpr std::array deliveryOptimizationPaths
        {
            std::wstring_view(L"%SystemRoot%\\SoftwareDistribution\\DeliveryOptimization"),
            std::wstring_view(L"%SystemRoot%\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache")
        };
        constexpr std::array defenderCachePaths
        {
            std::wstring_view(L"%ProgramData%\\Microsoft\\Windows Defender\\LocalCopy"),
            std::wstring_view(L"%ProgramData%\\Microsoft\\Windows Defender\\Support"),
            std::wstring_view(L"%ProgramData%\\Microsoft\\Windows Defender\\Scans\\History\\Service")
        };
        constexpr std::array thumbnailCachePaths
        {
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer"),
            std::wstring_view(L"%SystemDrive%\\Users\\*\\AppData\\Local\\Microsoft\\Windows\\Explorer")
        };
        constexpr std::array thumbnailCachePatterns
        {
            std::wstring_view(L"thumbcache_*.db"), std::wstring_view(L"iconcache_*.db")
        };
        constexpr std::array logFilesPaths
        {
            std::wstring_view(L"%SystemRoot%\\Logs"), std::wstring_view(L"%LOCALAPPDATA%\\CrashDumps")
        };
        constexpr std::array logFilesPatterns
        {
            std::wstring_view(L"*.log"), std::wstring_view(L"*.etl"), std::wstring_view(L"*.evtx")
        };
        constexpr std::array memoryDumpPaths
        {
            std::wstring_view(L"%SystemRoot%\\Minidump"), std::wstring_view(L"%SystemRoot%\\MEMORY.DMP")
        };
        constexpr std::array memoryDumpPatterns
        {
            std::wstring_view(L"*.dmp"), std::wstring_view(L"MEMORY.DMP")
        };
        constexpr std::array oldWindowsPaths
        {
            std::wstring_view(L"%SystemDrive%\\Windows.old"),
            std::wstring_view(L"%SystemDrive%\\$Windows.~BT"),
            std::wstring_view(L"%SystemDrive%\\$Windows.~WS")
        };
        constexpr std::array appCachePaths
        {
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Windows\\WebCache")
        };
        constexpr std::array thirdPartyCachePaths
        {
            std::wstring_view(L"%APPDATA%\\discord\\Cache"),
            std::wstring_view(L"%APPDATA%\\discord\\Code Cache"),
            std::wstring_view(L"%APPDATA%\\discord\\GPUCache"),
            std::wstring_view(L"%APPDATA%\\discord\\ShaderCache"),
            std::wstring_view(L"%APPDATA%\\Slack\\Cache"),
            std::wstring_view(L"%APPDATA%\\Microsoft\\Teams\\Cache"),
            std::wstring_view(L"%APPDATA%\\Microsoft\\Teams\\GPUCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Steam\\htmlcache"),
            std::wstring_view(L"%LOCALAPPDATA%\\EpicGamesLauncher\\Saved\\webcache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Netease\\CloudMusic\\Cache"),
            std::wstring_view(L"%APPDATA%\\Tencent\\QQMusic\\Cache")
        };
        constexpr std::array fontCachePaths
        {
            std::wstring_view(L"%SystemRoot%\\ServiceProfiles\\LocalService\\AppData\\Local\\FontCache")
        };
        constexpr std::array errorReportPaths
        {
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Windows\\WER"),
            std::wstring_view(L"%ProgramData%\\Microsoft\\Windows\\WER")
        };
        constexpr std::array installerTempPaths
        {
            std::wstring_view(L"%LOCALAPPDATA%\\Downloaded Installations")
        };
        constexpr std::array clipboardCachePaths
        {
            std::wstring_view(L"%LOCALAPPDATA%\\Microsoft\\Windows\\Clipboard")
        };
        constexpr std::array shaderCachePaths
        {
            std::wstring_view(L"%SystemRoot%\\System32\\d3d_cache"),
            std::wstring_view(L"%LOCALAPPDATA%\\D3DSCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\AMD\\DxCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\NVIDIA\\DXCache"),
            std::wstring_view(L"%LOCALAPPDATA%\\Intel\\ShaderCache")
        };

        // ---------------------------------------------------------------------------
        // Safety tables (LightC safety_constants / enhanced_delete). Every delete
        // passes through these gates; the rebuildable exception re-admits a narrow
        // set of system cache directories that the scanner legitimately targets.
        // ---------------------------------------------------------------------------

        constexpr std::array protectedPathPrefixes
        {
            std::wstring_view(L"\\windows\\system32"), std::wstring_view(L"\\windows\\syswow64"),
            std::wstring_view(L"\\windows\\winsxs"), std::wstring_view(L"\\windows\\servicing"),
            std::wstring_view(L"\\windows\\assembly"), std::wstring_view(L"\\windows\\boot"),
            std::wstring_view(L"\\windows\\fonts"), std::wstring_view(L"\\windows\\inf"),
            std::wstring_view(L"\\windows\\microsoft.net"), std::wstring_view(L"\\windows\\security"),
            std::wstring_view(L"\\program files"), std::wstring_view(L"\\users\\default"),
            std::wstring_view(L"\\users\\public\\desktop"),
            std::wstring_view(L"\\programdata\\microsoft\\windows"),
            std::wstring_view(L"\\programdata\\microsoft\\windows defender"),
            std::wstring_view(L"\\recovery"), std::wstring_view(L"\\windows.old"),
            std::wstring_view(L"\\$windows.~bt"), std::wstring_view(L"\\$windows.~ws")
        };
        constexpr std::array rebuildableCachePaths
        {
            std::wstring_view(L"\\windows\\system32\\d3d_cache\\"),
            std::wstring_view(L"\\programdata\\microsoft\\windows defender\\localcopy\\"),
            std::wstring_view(L"\\programdata\\microsoft\\windows defender\\support\\"),
            std::wstring_view(L"\\programdata\\microsoft\\windows defender\\scans\\history\\service\\"),
            std::wstring_view(L"\\programdata\\microsoft\\windows\\wer\\")
        };
        constexpr std::array protectedFileNames
        {
            std::wstring_view(L"ntoskrnl.exe"), std::wstring_view(L"hal.dll"), std::wstring_view(L"ntdll.dll"),
            std::wstring_view(L"kernel32.dll"), std::wstring_view(L"kernelbase.dll"), std::wstring_view(L"user32.dll"),
            std::wstring_view(L"gdi32.dll"), std::wstring_view(L"advapi32.dll"), std::wstring_view(L"shell32.dll"),
            std::wstring_view(L"ole32.dll"), std::wstring_view(L"bootmgr"), std::wstring_view(L"bcd"),
            std::wstring_view(L"ntldr"), std::wstring_view(L"boot.ini"), std::wstring_view(L"pagefile.sys"),
            std::wstring_view(L"hiberfil.sys"), std::wstring_view(L"swapfile.sys"), std::wstring_view(L"desktop.ini"),
            std::wstring_view(L"ntuser.dat"), std::wstring_view(L"usrclass.dat"),
            std::wstring_view(L"config.data"), std::wstring_view(L"accinfo.dat"), std::wstring_view(L"msg.db"),
            std::wstring_view(L"micromsg.db"), std::wstring_view(L"contact.db"), std::wstring_view(L"emotion.db"),
            std::wstring_view(L"favorite.db"), std::wstring_view(L"publicmsg.db")
        };
        constexpr std::array protectedExtensionsInWindows
        {
            std::wstring_view(L"sys"), std::wstring_view(L"dll"), std::wstring_view(L"exe"),
            std::wstring_view(L"drv"), std::wstring_view(L"ocx"), std::wstring_view(L"cpl"),
            std::wstring_view(L"msi"), std::wstring_view(L"msp"), std::wstring_view(L"msu"),
            std::wstring_view(L"cat"), std::wstring_view(L"mum"), std::wstring_view(L"manifest")
        };

        // Scan-side protected substrings plus the re-admitted rebuildable caches
        constexpr std::array scanProtectedSubstrings
        {
            std::wstring_view(L"system32"), std::wstring_view(L"syswow64"), std::wstring_view(L"winsxs"),
            std::wstring_view(L"assembly"), std::wstring_view(L"\\windows\\servicing"),
            std::wstring_view(L"\\windows\\installer"), std::wstring_view(L"\\windows\\logs\\cbs"),
            std::wstring_view(L"\\program files"), std::wstring_view(L"\\programdata\\microsoft\\windows defender"),
            std::wstring_view(L"\\users\\default"), std::wstring_view(L"\\appdata\\local\\packages\\")
        };
        constexpr std::array protectedExtensionsUnderWindows
        {
            std::wstring_view(L"sys"), std::wstring_view(L"dll"), std::wstring_view(L"exe"),
            std::wstring_view(L"msi"), std::wstring_view(L"cat"), std::wstring_view(L"mum"),
            std::wstring_view(L"manifest")
        };

        // True when `lower` contains the anchored `anchor` at a segment boundary.
        // Anchors that start with a backslash always begin at a boundary by
        // construction, so only bare anchors need the preceding-separator test.
        bool ContainsSegment(const std::wstring_view lower, const std::wstring_view anchor)
        {
            for (size_t pos = lower.find(anchor); pos != std::wstring_view::npos;
                pos = lower.find(anchor, pos + 1))
            {
                if (pos == 0 || anchor.starts_with(L'\\') || lower[pos - 1] == L'\\') return true;
            }
            return false;
        }

        // Case-insensitive wildcard match over the file name with * and ?
        bool WildcardMatch(const std::wstring_view text, const std::wstring_view pattern)
        {
            size_t t = 0, p = 0, star = std::wstring_view::npos, mark = 0;
            while (t < text.size())
            {
                if (p < pattern.size() && (towlower(pattern[p]) == towlower(text[t]) || pattern[p] == L'?'))
                {
                    ++t; ++p;
                }
                else if (p < pattern.size() && pattern[p] == L'*')
                {
                    star = p++; mark = t;
                }
                else if (star != std::wstring_view::npos)
                {
                    p = star + 1; t = ++mark;
                }
                else return false;
            }
            while (p < pattern.size() && pattern[p] == L'*') ++p;
            return p == pattern.size();
        }

        bool MatchesPattern(const std::wstring& name, const std::span<const std::wstring_view> patterns)
        {
            if (patterns.empty()) return true;
            const std::wstring lower = MakeLower(name);
            return std::ranges::any_of(patterns, [&lower](const std::wstring_view pattern)
            {
                return WildcardMatch(lower, pattern);
            });
        }

        std::wstring_view FileExtensionOf(const std::wstring_view path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            const size_t dot = path.find_last_of(L'.');
            return dot != std::wstring_view::npos && (slash == std::wstring_view::npos || dot > slash) ?
                path.substr(dot + 1) : std::wstring_view();
        }

        // ---------------------------------------------------------------------------
        // Path expansion: environment variables first, then wildcard segments
        // ---------------------------------------------------------------------------

        void ExpandGlobRecursive(const std::wstring& path, std::vector<std::wstring>& out)
        {
            const size_t wildcard = path.find_first_of(L"*?");
            if (wildcard == std::wstring::npos)
            {
                if (const DWORD attrs = GetFileAttributesW(path.c_str()); attrs != INVALID_FILE_ATTRIBUTES)
                {
                    out.push_back(path);
                }
                return;
            }

            // Split into fixed prefix, wildcard segment, and literal remainder
            const size_t segStart = path.rfind(L'\\', wildcard);
            const size_t segEnd = path.find(L'\\', wildcard);
            const std::wstring prefix = path.substr(0, segStart);
            const std::wstring pattern = path.substr(segStart + 1,
                segEnd == std::wstring::npos ? std::wstring::npos : segEnd - segStart - 1);
            const std::wstring rest = segEnd == std::wstring::npos ? L"" : path.substr(segEnd);

            WIN32_FIND_DATAW data{};
            HANDLE find = FindFirstFileW((prefix + L"\\" + pattern).c_str(), &data);
            if (find == INVALID_HANDLE_VALUE) return;
            do
            {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                        wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0)
                    {
                        ExpandGlobRecursive(prefix + L"\\" + data.cFileName + rest, out);
                    }
                }
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }

        std::vector<std::wstring> ExpandPathSpec(const std::wstring_view spec)
        {
            wchar_t expanded[1024]{};
            if (ExpandEnvironmentStringsW(std::wstring(spec).c_str(), expanded,
                static_cast<DWORD>(std::size(expanded))) == 0 || expanded[0] == L'\0')
            {
                return {};
            }

            std::vector<std::wstring> results;
            ExpandGlobRecursive(expanded, results);

            // De-duplicate paths that differ only in case or trailing separators
            std::unordered_set<std::wstring> seen;
            std::vector<std::wstring> unique;
            for (auto& path : results)
            {
                while (path.ends_with(L'\\') && path.size() > 3) path.pop_back();
                if (seen.emplace(MakeLower(path)).second) unique.push_back(std::move(path));
            }
            return unique;
        }

        // ---------------------------------------------------------------------------
        // Scan-side protection checks
        // ---------------------------------------------------------------------------

        bool IsScanBlocked(const std::wstring& path)
        {
            const std::wstring lower = MakeLower(path);

            // Re-admitted rebuildable caches bypass every protection rule
            if (std::ranges::any_of(rebuildableCachePaths, [&lower](const std::wstring_view anchor)
            {
                return ContainsSegment(lower, anchor);
            }))
            {
                return false;
            }

            if (std::ranges::any_of(scanProtectedSubstrings, [&lower](const std::wstring_view needle)
            {
                return lower.find(needle) != std::wstring::npos;
            }))
            {
                return true;
            }

            if (lower.find(L"\\windows\\") != std::wstring::npos)
            {
                const std::wstring_view extension = FileExtensionOf(lower);
                if (const bool blocked = std::ranges::any_of(protectedExtensionsUnderWindows,
                    [&extension](const std::wstring_view ext) { return extension == ext; }); blocked)
                {
                    return true;
                }
            }
            return false;
        }

        // ---------------------------------------------------------------------------
        // Scanning
        // ---------------------------------------------------------------------------

        void AddScanFile(CategoryResult& result, ScanState& state, std::wstring path, const ULONGLONG size)
        {
            result.files.push_back({ std::move(path), size });
            result.size += size;
            result.count++;
            state.filesFound.fetch_add(1, std::memory_order_relaxed);
        }

        void WalkDirectory(const std::wstring& dir, const CategoryInfo& info, CategoryResult& result,
            ScanState& state, const int depth)
        {
            if (state.cancelled.load(std::memory_order_relaxed) || depth > MaxScanDepth) return;
            if (IsScanBlocked(dir)) return;

            WIN32_FIND_DATAW data{};
            HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
            if (find == INVALID_HANDLE_VALUE) return;
            do
            {
                if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
                const std::wstring full = dir + L"\\" + data.cFileName;
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    {
                        WalkDirectory(full, info, result, state, depth + 1);
                    }
                }
                else if (MatchesPattern(data.cFileName, info.patterns) && !IsScanBlocked(full))
                {
                    AddScanFile(result, state, full,
                        (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
                }
            } while (!state.cancelled.load(std::memory_order_relaxed) && FindNextFileW(find, &data));
            FindClose(find);
        }

        void ScanRecycleBins(CategoryResult& result, ScanState& state)
        {
            const DWORD drives = GetLogicalDrives();
            SHQUERYRBINFO info{ .cbSize = sizeof(info) };
            for (wchar_t letter = L'A'; letter <= L'Z'; ++letter)
            {
                if (!(drives & (1u << (letter - L'A')))) continue;
                const std::wstring root = std::format(L"{}:\\", letter);
                if (FAILED(SHQueryRecycleBinW(root.c_str(), &info))) continue;
                if (info.i64NumItems > 0)
                {
                    result.count += static_cast<std::size_t>(info.i64NumItems);
                    result.size += static_cast<ULONGLONG>(info.i64Size);
                    state.recycleDrives.push_back({ root, static_cast<ULONGLONG>(info.i64Size),
                        static_cast<int>(info.i64NumItems) });
                }
            }
        }

        void ScanCategory(const int index, ScanState& state)
        {
            auto& result = state.results[static_cast<std::size_t>(index)];
            const CategoryInfo& info = Categories()[static_cast<std::size_t>(index)];

            if (info.recycleBin)
            {
                ScanRecycleBins(result, state);
            }
            else
            {
                for (const std::wstring_view spec : info.paths)
                {
                    for (const std::wstring& root : ExpandPathSpec(spec))
                    {
                        const DWORD attrs = GetFileAttributesW(root.c_str());
                        if (attrs == INVALID_FILE_ATTRIBUTES) continue;
                        if (attrs & FILE_ATTRIBUTE_DIRECTORY)
                        {
                            WalkDirectory(root, info, result, state, 0);
                        }
                        else if (!IsScanBlocked(root))
                        {
                            // Root specs may name single files, e.g. MEMORY.DMP
                            const size_t slash = root.find_last_of(L'\\');
                            const std::wstring name = root.substr(slash + 1);
                            if (!MatchesPattern(name, info.patterns)) continue;
                            WIN32_FIND_DATAW data{};
                            HANDLE find = FindFirstFileW(root.c_str(), &data);
                            if (find != INVALID_HANDLE_VALUE)
                            {
                                AddScanFile(result, state, root,
                                    (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
                                FindClose(find);
                            }
                        }
                        if (state.cancelled.load(std::memory_order_relaxed)) break;
                    }
                    if (state.cancelled.load(std::memory_order_relaxed)) break;
                }
            }
            state.categoriesDone.fetch_add(1, std::memory_order_relaxed);
        }

        // ---------------------------------------------------------------------------
        // Deleting
        // ---------------------------------------------------------------------------

        DWORD ClusterSize(const wchar_t drive, std::unordered_map<wchar_t, DWORD>& cache)
        {
            if (const auto found = cache.find(drive); found != cache.end()) return found->second;
            DWORD sectors = 0, bytes = 0, freeClusters = 0, totalClusters = 0;
            const std::wstring root = std::format(L"{}:\\", drive);
            DWORD cluster = 4096;
            if (GetDiskFreeSpaceW(root.c_str(), &sectors, &bytes, &freeClusters, &totalClusters))
            {
                cluster = sectors * bytes;
            }
            cache.emplace(drive, cluster);
            return cluster;
        }

        ULONGLONG PhysicalSize(const ULONGLONG logical, const DWORD cluster)
        {
            return cluster == 0 ? logical : ((logical + cluster - 1) / cluster) * cluster;
        }

        bool MarkForRebootDelete(const std::wstring& path)
        {
            // Requires administrator rights; without them the caller reports failure
            return MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
        }

        enum class Outcome { Deleted, Reboot, Failed, Missing };

        Outcome DeleteOne(const FileEntry& file, CleanState& state,
            std::unordered_map<wchar_t, DWORD>& clusters, std::vector<std::wstring>& parentDirs,
            ULONGLONG& freed)
        {
            const std::wstring lower = MakeLower(file.path);
            if (IsDeleteBlocked(file.path)) return Outcome::Failed;

            const DWORD attributes = GetFileAttributesW(file.path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) return Outcome::Missing;

            BOOL deleted = DeleteFileW(file.path.c_str());
            if (!deleted && GetLastError() == ERROR_ACCESS_DENIED &&
                attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            {
                // Read-only or hidden system flags block deletion; clear and retry
                SetFileAttributesW(file.path.c_str(), FILE_ATTRIBUTE_NORMAL);
                deleted = DeleteFileW(file.path.c_str());
            }

            if (!deleted)
            {
                const DWORD error = GetLastError();
                if ((error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) &&
                    MarkForRebootDelete(file.path))
                {
                    return Outcome::Reboot;
                }
                return Outcome::Failed;
            }

            freed = PhysicalSize(file.size, ClusterSize(file.path.front(), clusters));

            // Remember the parent so emptied directories can be removed afterwards
            if (const size_t slash = file.path.find_last_of(L'\\'); slash != std::wstring::npos &&
                slash > 2)
            {
                parentDirs.push_back(file.path.substr(0, slash));
            }
            return Outcome::Deleted;
        }

        void RemoveEmptyParents(std::vector<std::wstring> dirs)
        {
            // Longest first so nested emptied chains collapse bottom-up; each call
            // only succeeds when the directory is genuinely empty
            std::ranges::sort(dirs, std::ranges::greater{});
            std::unordered_set<std::wstring> seen;
            for (const std::wstring& dir : dirs)
            {
                if (!seen.emplace(MakeLower(dir)).second) continue;
                RemoveDirectoryW(dir.c_str());
            }
        }

        void EmptyRecycleBins(const ScanState& scan, CleanState& state)
        {
            // Empties each drive's bin through the shell; per-drive aggregates
            // from the scan are only credited when the bin was emptied
            constexpr std::size_t index = std::to_underlying(Category::RecycleBin);
            for (const RecycleDrive& drive : scan.recycleDrives)
            {
                const HRESULT emptied = SHEmptyRecycleBinW(nullptr, drive.root.c_str(),
                    SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
                if (emptied != S_OK && emptied != E_INVALIDARG) continue;
                state.filesDeleted.fetch_add(drive.items, std::memory_order_relaxed);
                state.filesProcessed.fetch_add(drive.items, std::memory_order_relaxed);
                state.bytesFreed.fetch_add(drive.size, std::memory_order_relaxed);
                state.deletedPerCategory[index] += drive.items;
                state.freedPerCategory[index] += drive.size;
            }
        }

    }

    const std::vector<CategoryInfo>& Categories()
    {
        static const std::vector<CategoryInfo> table
        {
            { IDS_SMARTCLEAN_CAT_WTEMP, IDS_SMARTCLEAN_CAT_WTEMP_DESC, 1, false, false, windowsTempPaths, {} },
            { IDS_SMARTCLEAN_CAT_SYSCACHE, IDS_SMARTCLEAN_CAT_SYSCACHE_DESC, 3, false, false, systemCachePaths, {} },
            { IDS_SMARTCLEAN_CAT_BROWSERCACHE, IDS_SMARTCLEAN_CAT_BROWSERCACHE_DESC, 2, false, false, browserCachePaths, {} },
            { IDS_SMARTCLEAN_CAT_RECYCLEBIN, IDS_SMARTCLEAN_CAT_RECYCLEBIN_DESC, 3, false, true, {}, {} },
            { IDS_SMARTCLEAN_CAT_WINUPDATE, IDS_SMARTCLEAN_CAT_WINUPDATE_DESC, 2, false, false, windowsUpdatePaths, {} },
            { IDS_SMARTCLEAN_CAT_DOPT, IDS_SMARTCLEAN_CAT_DOPT_DESC, 2, false, false, deliveryOptimizationPaths, {} },
            { IDS_SMARTCLEAN_CAT_DEFENDER, IDS_SMARTCLEAN_CAT_DEFENDER_DESC, 2, false, false, defenderCachePaths, {} },
            { IDS_SMARTCLEAN_CAT_THUMBS, IDS_SMARTCLEAN_CAT_THUMBS_DESC, 1, false, false, thumbnailCachePaths, thumbnailCachePatterns },
            { IDS_SMARTCLEAN_CAT_LOGS, IDS_SMARTCLEAN_CAT_LOGS_DESC, 2, false, false, logFilesPaths, logFilesPatterns },
            { IDS_SMARTCLEAN_CAT_DUMP, IDS_SMARTCLEAN_CAT_DUMP_DESC, 3, false, false, memoryDumpPaths, memoryDumpPatterns },
            { IDS_SMARTCLEAN_CAT_OLDWIN, IDS_SMARTCLEAN_CAT_OLDWIN_DESC, 3, true, false, oldWindowsPaths, {} },
            { IDS_SMARTCLEAN_CAT_APPCACHE, IDS_SMARTCLEAN_CAT_APPCACHE_DESC, 3, false, false, appCachePaths, {} },
            { IDS_SMARTCLEAN_CAT_3RDPARTY, IDS_SMARTCLEAN_CAT_3RDPARTY_DESC, 2, false, false, thirdPartyCachePaths, {} },
            { IDS_SMARTCLEAN_CAT_FONTS, IDS_SMARTCLEAN_CAT_FONTS_DESC, 1, false, false, fontCachePaths, {} },
            { IDS_SMARTCLEAN_CAT_WER, IDS_SMARTCLEAN_CAT_WER_DESC, 2, false, false, errorReportPaths, {} },
            { IDS_SMARTCLEAN_CAT_INSTALLER, IDS_SMARTCLEAN_CAT_INSTALLER_DESC, 2, false, false, installerTempPaths, {} },
            { IDS_SMARTCLEAN_CAT_CLIPBOARD, IDS_SMARTCLEAN_CAT_CLIPBOARD_DESC, 1, false, false, clipboardCachePaths, {} },
            { IDS_SMARTCLEAN_CAT_SHADER, IDS_SMARTCLEAN_CAT_SHADER_DESC, 1, false, false, shaderCachePaths, {} },
        };
        return table;
    }

    void ScanAll(ScanState& state)
    {
        state.running.store(true, std::memory_order_release);
        state.cancelled.store(false, std::memory_order_release);
        state.filesFound.store(0, std::memory_order_release);
        state.categoriesDone.store(0, std::memory_order_release);
        state.recycleDrives.clear();
        for (auto& result : state.results) result = {};

        std::vector<std::jthread> workers;
        workers.reserve(std::to_underlying(Category::Count));
        for (const int index : std::views::iota(0, static_cast<int>(std::to_underlying(Category::Count))))
        {
            workers.emplace_back([index, &state] { ScanCategory(index, state); });
        }
        for (std::jthread& worker : workers) worker.join();

        state.running.store(false, std::memory_order_release);
    }

    void CleanSelected(const ScanState& scan, const std::vector<bool>& selected, CleanState& state)
    {
        state.running.store(true, std::memory_order_release);
        state.cancelled.store(false, std::memory_order_release);
        state.filesProcessed.store(0, std::memory_order_release);
        state.filesDeleted.store(0, std::memory_order_release);
        state.filesReboot.store(0, std::memory_order_release);
        state.filesFailed.store(0, std::memory_order_release);
        state.bytesFreed.store(0, std::memory_order_release);

        // Count the files that are actually deletable for honest progress
        int total = 0;
        for (const int index : std::views::iota(0, static_cast<int>(std::to_underlying(Category::Count))))
        {
            const CategoryInfo& info = Categories()[static_cast<std::size_t>(index)];
            if (!selected[static_cast<std::size_t>(index)] || info.systemManaged) continue;
            if (!info.recycleBin) total += static_cast<int>(scan.results[static_cast<std::size_t>(index)].count);
        }
        state.filesTotal.store(total, std::memory_order_release);

        std::unordered_map<wchar_t, DWORD> clusters;
        std::vector<std::wstring> parentDirs;

        for (const int index : std::views::iota(0, static_cast<int>(std::to_underlying(Category::Count))))
        {
            if (state.cancelled.load(std::memory_order_relaxed)) break;

            const std::size_t i = static_cast<std::size_t>(index);
            const CategoryInfo& info = Categories()[i];
            if (!selected[i] || info.systemManaged || info.recycleBin) continue;

            for (const FileEntry& file : scan.results[i].files)
            {
                if (state.cancelled.load(std::memory_order_relaxed)) break;
                state.filesProcessed.fetch_add(1, std::memory_order_relaxed);
                ULONGLONG freed = 0;
                switch (DeleteOne(file, state, clusters, parentDirs, freed))
                {
                case Outcome::Deleted:
                    state.filesDeleted.fetch_add(1, std::memory_order_relaxed);
                    state.bytesFreed.fetch_add(freed, std::memory_order_relaxed);
                    state.deletedPerCategory[i] += 1;
                    state.freedPerCategory[i] += freed;
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

        if (!state.cancelled.load(std::memory_order_relaxed) &&
            selected[std::to_underlying(Category::RecycleBin)])
        {
            EmptyRecycleBins(scan, state);
        }

        if (!parentDirs.empty()) RemoveEmptyParents(std::move(parentDirs));

        state.running.store(false, std::memory_order_release);
    }

    bool IsDeleteBlocked(const std::wstring& path)
    {
        const std::wstring lower = MakeLower(path);

        // Never touch drive roots
        if (lower.size() <= 3 && lower.ends_with(L"\\")) return true;

        // Re-admitted rebuildable caches bypass every protection rule
        if (std::ranges::any_of(rebuildableCachePaths, [&lower](const std::wstring_view anchor)
        {
            return ContainsSegment(lower, anchor);
        }))
        {
            return false;
        }

        // Anchored prefixes after the volume root, e.g. "\Windows\System32";
        // the next character must start a new segment to avoid matching
        // sibling directories that merely share a longer name
        if (lower.size() >= 2)
        {
            const std::wstring_view afterDrive = std::wstring_view(lower).substr(2);
            for (const std::wstring_view prefix : protectedPathPrefixes)
            {
                if (!afterDrive.starts_with(prefix)) continue;
                if (afterDrive.size() == prefix.size() || afterDrive[prefix.size()] == L'\\')
                {
                    return true;
                }
            }
        }

        // Critical file names anywhere on the volume
        const size_t slash = lower.find_last_of(L'\\');
        const std::wstring_view name = slash == std::wstring::npos ?
            std::wstring_view(lower) : std::wstring_view(lower).substr(slash + 1);
        if (std::ranges::any_of(protectedFileNames,
            [&name](const std::wstring_view candidate) { return name == candidate; }))
        {
            return true;
        }

        // Executable and system-critical extensions anywhere under \Windows\
        if (lower.find(L"\\windows\\") != std::wstring::npos)
        {
            const std::wstring_view extension = FileExtensionOf(lower);
            if (std::ranges::any_of(protectedExtensionsInWindows,
                [&extension](const std::wstring_view ext) { return extension == ext; }))
            {
                return true;
            }
        }
        return false;
    }
}
