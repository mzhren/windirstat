// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "DuplicateClean.h"
#include "AppMigrate.h"

namespace AppMigrate
{
    namespace
    {
        // Default storage locations of the supported applications. Newer WeChat
        // 4.x builds moved to xwechat_files while 3.x kept WeChat Files.
        constexpr std::array wechatPaths
        {
            std::wstring_view(L"%USERPROFILE%\\Documents\\WeChat Files"),
            std::wstring_view(L"%USERPROFILE%\\Documents\\xwechat_files")
        };
        constexpr std::array wpsPaths
        {
            std::wstring_view(L"%APPDATA%\\kingsoft\\office6\\backup"),
            std::wstring_view(L"%LOCALAPPDATA%\\Kingsoft\\WPS Cloud Files"),
            std::wstring_view(L"%USERPROFILE%\\WPSDrive")
        };

        std::wstring ExpandCandidatePath(const std::wstring_view spec)
        {
            wchar_t expanded[1024]{};
            if (ExpandEnvironmentStringsW(std::wstring(spec).c_str(), expanded,
                static_cast<DWORD>(std::size(expanded))) == 0 || expanded[0] == L'\0')
            {
                return {};
            }
            return expanded;
        }

        ULONGLONG SizeFolder(const std::wstring& folder)
        {
            ULONGLONG size = 0;
            std::vector<std::wstring> pending{ folder };
            while (!pending.empty())
            {
                const std::wstring dir = pending.back();
                pending.pop_back();

                WIN32_FIND_DATAW data{};
                HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
                if (find == INVALID_HANDLE_VALUE) continue;
                do
                {
                    if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
                    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        pending.push_back(dir + L"\\" + data.cFileName);
                    }
                    else
                    {
                        size += (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
                    }
                } while (FindNextFileW(find, &data));
                FindClose(find);
            }
            return size;
        }

        bool SameVolume(const std::wstring& left, const std::wstring& right)
        {
            return left.size() >= 2 && right.size() >= 2 &&
                towlower(left[0]) == towlower(right[0]);
        }

        // True when `inner` equals or lives inside the `outer` directory tree
        bool IsInside(const std::wstring& outer, const std::wstring& inner)
        {
            std::wstring lowerOuter = MakeLower(outer);
            std::wstring lowerInner = MakeLower(inner);
            while (lowerOuter.ends_with(L'\\')) lowerOuter.pop_back();
            while (lowerInner.ends_with(L'\\')) lowerInner.pop_back();
            if (lowerInner == lowerOuter) return true;
            return lowerInner.starts_with(lowerOuter + L"\\");
        }

        std::wstring LeafNameImpl(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L'\\');
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }
    }

    std::wstring LeafName(const std::wstring& path)
    {
        return LeafNameImpl(path);
    }

        std::wstring UniqueDestination(const std::wstring& dest, const std::wstring& leaf)
        {
            std::wstring target = dest + L"\\" + leaf;
            for (int i = 2; FolderExists(target); ++i)
            {
                target = dest + L"\\" + leaf + std::format(L" ({})", i);
            }
            return target;
        }

        // Reports per-operation byte progress back to the dialog
        class MoveProgressSink final : public IFileOperationProgressSink
        {
            std::atomic<ULONGLONG>& m_bytesMoved;
            const ULONGLONG m_base;
            std::atomic<ULONG> m_refs = 1;

        public:
            explicit MoveProgressSink(std::atomic<ULONGLONG>& bytesMoved, const ULONGLONG base)
                : m_bytesMoved(bytesMoved), m_base(base) {}

            STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
            {
                if (ppv == nullptr) return E_POINTER;
                if (riid != IID_IUnknown && riid != __uuidof(IFileOperationProgressSink))
                {
                    *ppv = nullptr;
                    return E_NOINTERFACE;
                }
                *ppv = static_cast<IFileOperationProgressSink*>(this);
                AddRef();
                return S_OK;
            }
            STDMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
            STDMETHODIMP_(ULONG) Release() override
            {
                const ULONG refs = --m_refs;
                if (refs == 0) delete this;
                return refs;
            }

            STDMETHODIMP StartOperations() override { return S_OK; }
            STDMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
            STDMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
            STDMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
            STDMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
            STDMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
            STDMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
            STDMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
            STDMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
            STDMETHODIMP PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) override { return S_OK; }
            STDMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
            STDMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
            STDMETHODIMP ResetTimer() override { return S_OK; }
            STDMETHODIMP PauseTimer() override { return S_OK; }
            STDMETHODIMP ResumeTimer() override { return S_OK; }

            // The shell walks the whole selection first and reports progress during the move
            STDMETHODIMP UpdateProgress(UINT total, UINT soFar) override
            {
                m_bytesMoved.store(m_base + soFar, std::memory_order_relaxed);
                return S_OK;
            }
        };

    const PresetInfo& Presets(const Preset preset)
    {
        static const std::vector<PresetInfo> table
        {
            { IDS_MIGRATE_WECHAT_TITLE, IDS_MIGRATE_WECHAT_DESC, IDS_MIGRATE_WECHAT_NOTE, wechatPaths },
            { IDS_MIGRATE_WPS_TITLE, IDS_MIGRATE_WPS_DESC, IDS_MIGRATE_WPS_NOTE, wpsPaths },
        };
        return table[static_cast<std::size_t>(preset)];
    }

    void RunDetect(const Preset preset, DetectState& state)
    {
        state.running.store(true, std::memory_order_release);
        state.cancelled.store(false, std::memory_order_release);
        state.candidates.clear();
        state.sized.store(0, std::memory_order_release);

        const PresetInfo& info = Presets(preset);
        std::vector<std::wstring> found;
        for (const std::wstring_view spec : info.paths)
        {
            const std::wstring path = ExpandCandidatePath(spec);
            if (!path.empty() && FolderExists(path))
            {
                found.push_back(path);
            }
        }

        // Size each folder on its own thread; updates flow through the counter
        std::vector<std::jthread> workers;
        std::atomic<size_t> next{ 0 };
        std::mutex candidatesMutex;
        const unsigned threadCount = std::min<unsigned>(4,
            std::max(1u, std::thread::hardware_concurrency()));
        for (const unsigned i : std::views::iota(0u, threadCount))
        {
            workers.emplace_back([&]
            {
                for (;;)
                {
                    if (state.cancelled.load(std::memory_order_relaxed)) return;
                    const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= found.size()) return;

                    Candidate candidate;
                    candidate.path = found[index];
                    candidate.size = SizeFolder(found[index]);
                    std::scoped_lock lock(candidatesMutex);
                    state.candidates.push_back(std::move(candidate));
                    state.sized.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (std::jthread& worker : workers) worker.join();

        std::ranges::sort(state.candidates, [](const Candidate& a, const Candidate& b)
        {
            return a.size > b.size;
        });
        state.running.store(false, std::memory_order_release);
    }

    bool CreateDirectoryJunction(const std::wstring& linkPath, const std::wstring& targetPath)
    {
        if (linkPath.empty() || targetPath.empty()) return false;
        if (!CreateDirectoryW(linkPath.c_str(), nullptr)) return false;

        const HANDLE handle = CreateFileW(linkPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            RemoveDirectoryW(linkPath.c_str());
            return false;
        }

        struct MountPointHeader
        {
            DWORD ReparseTag;
            WORD ReparseDataLength;
            WORD Reserved;
            WORD SubstituteNameOffset;
            WORD SubstituteNameLength;
            WORD PrintNameOffset;
            WORD PrintNameLength;
        };

        // The substitute name must be an absolute path in the \??\ namespace;
        // a trailing backslash mirrors what mklink /J stores.
        const std::wstring substitute = L"\\??\\" + targetPath + L"\\";
        const std::wstring printName = targetPath;
        const size_t substituteBytes = substitute.size() * sizeof(WCHAR);
        const size_t printBytes = printName.size() * sizeof(WCHAR);

        std::vector<BYTE> buffer(sizeof(MountPointHeader) + substituteBytes + sizeof(WCHAR) + printBytes + sizeof(WCHAR));
        auto* mountPoint = reinterpret_cast<MountPointHeader*>(buffer.data());
        mountPoint->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
        mountPoint->Reserved = 0;
        mountPoint->SubstituteNameOffset = 0;
        mountPoint->SubstituteNameLength = static_cast<WORD>(substituteBytes);
        mountPoint->PrintNameOffset = static_cast<WORD>(substituteBytes + sizeof(WCHAR));
        mountPoint->PrintNameLength = static_cast<WORD>(printBytes);
        mountPoint->ReparseDataLength = static_cast<WORD>(sizeof(MountPointHeader) - offsetof(MountPointHeader, SubstituteNameOffset) +
            substituteBytes + sizeof(WCHAR) + printBytes + sizeof(WCHAR));

        DWORD bytes = 0;
        const bool created = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
            static_cast<DWORD>(buffer.size()), nullptr, 0, &bytes, nullptr) != FALSE;
        CloseHandle(handle);
        if (!created) RemoveDirectoryW(linkPath.c_str());
        return created;
    }

    void RunMigration(const Preset preset, const std::vector<std::wstring>& sources,
        const std::vector<ULONGLONG>& sizes, const std::wstring& dest, MigrateState& state)
    {
        state.running.store(true, std::memory_order_release);
        state.cancelled.store(false, std::memory_order_release);
        state.filesTotal.store(static_cast<int>(sources.size()), std::memory_order_release);
        state.currentIndex.store(-1, std::memory_order_release);
        state.done.store(0, std::memory_order_release);
        state.failed.store(0, std::memory_order_release);
        state.locked.store(0, std::memory_order_release);
        state.bytesMoved.store(0, std::memory_order_relaxed);
        state.bytesTotal.store(0, std::memory_order_release);
        state.junctionFailed.clear();
        state.failedPaths.clear();

        ULONGLONG total = 0;
        for (const ULONGLONG size : sizes) total += size;
        state.bytesTotal.store(total, std::memory_order_release);

        // Initialize COM once for all the shell operations on this worker thread
        const ComApartmentScope com;
        if (!com)
        {
            state.failed.store(static_cast<int>(sources.size()), std::memory_order_relaxed);
            state.running.store(false, std::memory_order_release);
            return;
        }

        for (const int index : std::views::iota(0, static_cast<int>(sources.size())))
        {
            if (state.cancelled.load(std::memory_order_relaxed)) break;
            state.currentIndex.store(index, std::memory_order_relaxed);
            const std::wstring& source = sources[static_cast<std::size_t>(index)];
            const std::wstring leaf = LeafName(source);

            // The whole point is freeing the source drive, and nesting breaks junctions
            if (SameVolume(source, dest) || IsInside(dest, source) || IsInside(source, dest))
            {
                state.failed.fetch_add(1, std::memory_order_relaxed);
                state.failedPaths.push_back(source);
                continue;
            }

            const std::wstring destPath = UniqueDestination(dest, leaf);
            HRESULT moveResult = E_FAIL;
            bool moved = false;
            {
                CComPtr<IFileOperation> fileOperation;
                CComPtr<IShellItem> destShellItem;
                CComPtr<IShellItem> sourceShellItem;
                const auto flags = FOFX_SHOWELEVATIONPROMPT | FOF_NOCONFIRMATION |
                    (COptions::ShowMicrosoftProgress ? 0 : FOF_NO_UI);
                if (FAILED(::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&fileOperation))) ||
                    FAILED(fileOperation->SetOwnerWindow(GetMainWindowHandle())) ||
                    FAILED(fileOperation->SetOperationFlags(flags)) ||
                    FAILED(SHCreateItemFromParsingName(destPath.c_str(), nullptr, IID_PPV_ARGS(&destShellItem))) ||
                    FAILED(SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(&sourceShellItem))) ||
                    FAILED(fileOperation->MoveItem(sourceShellItem, destShellItem, nullptr, nullptr)))
                {
                    moveResult = E_FAIL;
                }
                else
                {
                    const ULONGLONG base = state.bytesMoved.load(std::memory_order_relaxed);
                    DWORD progressCookie = 0;
                    CComPtr<IFileOperationProgressSink> progress;
                    progress.Attach(new MoveProgressSink(state.bytesMoved, base));
                    fileOperation->Advise(progress, &progressCookie);
                    moveResult = PerformCancellableOperations(fileOperation, flags,
                        [this_ = &state] { return this_->cancelled.load(std::memory_order_relaxed); });
                    if (progressCookie != 0) fileOperation->Unadvise(progressCookie);
                    moved = moveResult == S_OK;
                }
            }

            if (!moved)
            {
                state.failed.fetch_add(1, std::memory_order_relaxed);
                state.failedPaths.push_back(source);
                continue;
            }

            // Locked files leave the source folder behind; do not junction an incomplete move
            if (FolderExists(source))
            {
                state.locked.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            if (CreateDirectoryJunction(source, destPath))
            {
                state.done.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                state.junctionFailed.push_back(source + L" -> " + destPath);
            }
        }

        state.currentIndex.store(-1, std::memory_order_release);
        state.running.store(false, std::memory_order_release);
    }
}
