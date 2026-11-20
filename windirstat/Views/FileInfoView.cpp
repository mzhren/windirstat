// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "FileInfoView.h"
#include "AppMigrate.h"

namespace
{
    constexpr COLORREF BlendColors(const COLORREF a, const COLORREF b, const double t)
    {
        const auto mix = [t](const COLORREF x, const COLORREF y, const int shift)
        {
            return static_cast<int>((x >> shift & 0xFF) * (1 - t) + (y >> shift & 0xFF) * t);
        };
        return RGB(mix(a, b, 0), mix(a, b, 8), mix(a, b, 16));
    }

    // True when the lowercase path contains the segment at a boundary.
    bool ContainsSegment(const std::wstring& lowercasePath, const std::wstring& segment)
    {
        return lowercasePath.find(segment + L"\\") != std::wstring::npos ||
            lowercasePath.ends_with(segment);
    }

    std::wstring GetEnvironmentFolder(const wchar_t* variable)
    {
        wchar_t buffer[MAX_PATH]{};
        if (GetEnvironmentVariableW(variable, buffer, MAX_PATH) == 0) return {};
        std::wstring folder(buffer);
        return MakeLower(TrimString(folder, L'\\', true));
    }
}

/////////////////////////////////////////////////////////////////////////////

void CFileInfoView::ShowPane(const bool show)
{
    m_show = show;
    Invalidate();
}

std::wstring CFileInfoView::TypeDescription(const CItem* item) const
{
    std::wstring suffix;
    if (item->IsTypeOrFlag(ITRP_SYMLINK)) suffix = Localization::Lookup(IDS_INFO_TYPE_SYMLINK);
    else if (item->IsTypeOrFlag(ITRP_JUNCTION)) suffix = Localization::Lookup(IDS_INFO_TYPE_JUNCTION);
    else if (item->IsTypeOrFlag(ITRP_MOUNT)) suffix = Localization::Lookup(IDS_INFO_TYPE_MOUNT);
    else if (item->IsTypeOrFlag(ITRP_CLOUD)) suffix = Localization::Lookup(IDS_INFO_TYPE_CLOUD);

    std::wstring type;
    if (item->IsTypeOrFlag(IT_MYCOMPUTER)) type = Localization::Lookup(IDS_INFO_TYPE_MYCOMPUTER);
    else if (item->IsTypeOrFlag(IT_DRIVE)) type = Localization::Lookup(IDS_INFO_TYPE_DRIVE);
    else if (item->IsTypeOrFlag(IT_DIRECTORY)) type = Localization::Lookup(IDS_INFO_TYPE_FOLDER);
    else if (item->IsTypeOrFlag(IT_FILE))
    {
        type = Localization::Lookup(IDS_INFO_TYPE_FILE);
        if (const std::wstring ext = item->GetExtension(); !ext.empty()) type += L" (" + ext + L")";
    }
    else if (item->IsTypeOrFlag(IT_FREESPACE)) type = Localization::Lookup(IDS_INFO_TYPE_FREESPACE);
    else if (item->IsTypeOrFlag(IT_UNKNOWN)) type = Localization::Lookup(IDS_INFO_TYPE_UNKNOWN);
    else if (item->IsTypeOrFlag(IT_HLINKS, IT_HLINKS_SET, IT_HLINKS_IDX, IT_HLINKS_FILE))
    {
        type = Localization::Lookup(IDS_INFO_TYPE_HARDLINKS);
    }
    else return suffix;

    return suffix.empty() ? type : type + L" (" + suffix + L")";
}

bool CFileInfoView::IsItemDeletable(const CItem* item) const
{
    if (item == nullptr) return false;
    // Mirror the selection rules of the cleanup delete commands (ID_CLEANUP_DELETE*).
    if (item->IsTypeOrFlag(ITF_REMOTE)) return !item->IsScanRoot();
    return !item->IsRootItem() && !item->IsMtpRoot() && !item->IsWslRoot() &&
        item->HasShellIdentity() && item->IsTypeOrFlag(IT_DIRECTORY, IT_FILE);
}

CFileInfoView::Protection CFileInfoView::ClassifyProtection(const CItem* item, const DWORD attributes) const
{
    if (!item->SupportsFilesystemApis() || item->IsTypeOrFlag(ITF_MTP, ITF_REMOTE)) return Protection::None;
    if (!item->IsTypeOrFlag(IT_DRIVE, IT_DIRECTORY, IT_FILE)) return Protection::None;

    const std::wstring path = MakeLower(item->GetPath());
    const std::wstring name = MakeLower(item->GetName());
    if (path.empty()) return Protection::None;

    // Files managed entirely by Windows at a volume root
    if (name == L"hiberfil.sys") return Protection::Hiberfil;
    if (name == L"pagefile.sys" || name == L"swapfile.sys") return Protection::Pagefile;

    // Protected system folders
    if (path.find(L"\\$recycle.bin") != std::wstring::npos) return Protection::RecycleBin;
    if (path.find(L"\\system volume information") != std::wstring::npos) return Protection::RestorePoints;

    // The Windows directory and its component store
    std::wstring windowsDirectory(MAX_PATH, L'\0');
    windowsDirectory.resize(GetWindowsDirectoryW(windowsDirectory.data(), MAX_PATH));
    const std::wstring windowsLower = MakeLower(TrimString(windowsDirectory, L'\\', true));
    if (!windowsLower.empty() && (path == windowsLower || path.starts_with(windowsLower + L"\\")))
    {
        if (ContainsSegment(path, L"\\winsxs")) return Protection::ComponentStore;
        if (item->IsTypeOrFlag(IT_FILE) && item->HasExtension(L".sys")) return Protection::SystemDriver;
        return Protection::SystemFolder;
    }

    // Installed programs
    for (const wchar_t* variable : { L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432" })
    {
        const std::wstring programs = GetEnvironmentFolder(variable);
        if (programs.empty()) continue;
        if (path == programs || path.starts_with(programs + L"\\")) return Protection::InstalledProgram;
    }

    (void)attributes;
    return Protection::None;
}

std::wstring CFileInfoView::MigratableHint(const CItem* item) const
{
    if (!item->IsTypeOrFlag(IT_DIRECTORY) || !item->SupportsFilesystemApis() ||
        item->IsScanRoot() || item->IsRootItem())
    {
        return {};
    }

    const std::wstring path = MakeLower(item->GetPath());
    if (ContainsSegment(path, L"\\wechat files") || ContainsSegment(path, L"\\xwechat_files"))
    {
        return Localization::Lookup(IDS_INFO_MIGRATE_WECHAT);
    }
    if (ContainsSegment(path, L"\\tencent files"))
    {
        return Localization::Lookup(IDS_INFO_MIGRATE_QQ);
    }
    if (path.find(L"\\appdata\\roaming\\kingsoft") != std::wstring::npos ||
        path.find(L"\\appdata\\local\\kingsoft") != std::wstring::npos)
    {
        return Localization::Lookup(IDS_INFO_MIGRATE_WPS);
    }
    return {};
}

void CFileInfoView::RefreshInfo()
{
    m_item = nullptr;
    m_selectionCount = 0;
    m_title.clear();
    m_subtitle.clear();
    m_rows.clear();
    m_notes.clear();
    m_buttons.clear();
    m_deletableRowIndex = -1;
    m_deletableVerdict = -1;

    const auto model = CWinDirStatModel::Get();
    if (!model->HasRootItem()) return;

    const auto items = model->GetAllSelected();
    m_selectionCount = static_cast<int>(items.size());
    if (items.empty()) return;

    m_item = items.front();
    m_title = m_item->GetName();
    m_subtitle = TypeDescription(m_item);

    // Path and, for files, the containing folder
    m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_PATH), m_item->GetPath());
    if (m_item->IsTypeOrFlag(IT_FILE))
    {
        m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_DIRECTORY), m_item->GetFolderPath());
    }

    // Sizes and proportion, mirroring the file tree columns
    if (m_item->HasSizePhysical())
    {
        m_rows.emplace_back(Localization::Lookup(IDS_COL_SIZE_PHYSICAL), FormatBytes(m_item->GetSizePhysical()));
    }
    m_rows.emplace_back(Localization::Lookup(IDS_COL_SIZE_LOGICAL), FormatBytes(m_item->GetSizeLogical()));
    m_rows.emplace_back(Localization::Lookup(IDS_COL_PERCENTAGE),
        FormatDouble((COptions::UseAbsolutePercentages ? m_item->GetAbsoluteFraction() : m_item->GetFraction()) * 100) + L"%");

    // Contents of containers
    if (!m_item->IsTypeOrFlag(IT_FILE, IT_FREESPACE, IT_UNKNOWN, IT_HLINKS, IT_HLINKS_SET, IT_HLINKS_IDX))
    {
        m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_CONTENTS), std::format(L"{} {}, {} {}, {} {}",
            Localization::Lookup(IDS_COL_ITEMS), FormatCount(m_item->GetItemsCount()),
            Localization::Lookup(IDS_COL_FILES), FormatCount(m_item->GetFilesCount()),
            Localization::Lookup(IDS_COL_FOLDERS), FormatCount(m_item->GetFoldersCount())));
    }

    // Owner
    std::wstring owner;
    if (m_item->IsTypeOrFlag(IT_FILE, IT_DIRECTORY))
    {
        owner = m_item->GetOwner();
        if (!owner.empty())
        {
            m_rows.emplace_back(Localization::Lookup(IDS_COL_OWNER), owner);
        }
    }

    // Modification time comes from the model; creation and access times are
    // fetched from disk, which also refreshes the current attributes
    if (!m_item->IsTypeOrFlag(IT_FREESPACE, IT_UNKNOWN, IT_HLINKS, IT_HLINKS_SET, IT_HLINKS_IDX))
    {
        m_rows.emplace_back(Localization::Lookup(IDS_COL_LAST_CHANGE),
            FormatFileTime(m_item->GetLastChange(), true));
    }

    DWORD attributes = m_item->GetAttributes();
    if (m_item->SupportsFilesystemApis() && model->IsScanSettled())
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(m_item->GetPathLong().c_str(), GetFileExInfoStandard, &data))
        {
            m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_CREATED),
                FormatFileTime(data.ftCreationTime, true));
            m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_ACCESSED),
                FormatFileTime(data.ftLastAccessTime, true));
            attributes = data.dwFileAttributes;
        }
    }
    m_rows.emplace_back(Localization::Lookup(IDS_COL_ATTRIBUTES), FormatAttributes(attributes));

    // Protection classification decides whether deletion or guidance is shown
    const Protection protection = ClassifyProtection(m_item, attributes);

    if (protection != Protection::None)
    {
        const std::wstring reason = protection == Protection::InstalledProgram ?
            Localization::Lookup(IDS_INFO_DELETABLE_NO_PROGRAM) :
            protection == Protection::Hiberfil || protection == Protection::Pagefile ||
            protection == Protection::RecycleBin || protection == Protection::RestorePoints ?
            Localization::Lookup(IDS_INFO_DELETABLE_NO_MANAGED) :
            Localization::Lookup(IDS_INFO_DELETABLE_NO_SYSTEM);
        m_deletableRowIndex = static_cast<int>(m_rows.size());
        m_deletableVerdict = 0;
        m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_DELETABLE), reason);

        switch (protection)
        {
        case Protection::SystemFolder:
            m_notes.emplace_back(Localization::Lookup(IDS_INFO_PROT_SYSTEMFOLDER));
            m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_DISKCLEANUP), .command = ID_CLEANUP_DISK_CLEANUP, .enabled = true });
            m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_STORAGESENSE), .command = ID_CLEANUP_STORAGE_SENSE, .enabled = true });
            break;
        case Protection::ComponentStore:
            m_notes.emplace_back(Localization::Lookup(IDS_INFO_PROT_WINSXS));
            m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_DISM), .command = ID_CLEANUP_DISM_ANALYZE, .enabled = true });
            break;
        case Protection::InstalledProgram:
            m_notes.emplace_back(Localization::Lookup(IDS_INFO_PROT_PROGRAM));
            m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_UNINSTALL), .command = ID_CLEANUP_REMOVE_PROGRAMS, .enabled = true });
            break;
        case Protection::SystemDriver:
            m_notes.emplace_back(Localization::Lookup(IDS_INFO_PROT_DRIVER));
            break;
        case Protection::Hiberfil:
            m_notes.emplace_back(Localization::Lookup(IDS_INFO_PROT_HIBERFIL));
            m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_HIBERNATE), .command = ID_CLEANUP_HIBERNATE, .enabled = true });
            break;
        case Protection::Pagefile:
            m_notes.emplace_back(Localization::Lookup(IDS_INFO_PROT_PAGEFILE));
            m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_SYSADVANCED), .command = CMD_SYSADVANCED, .enabled = true });
            break;
        case Protection::RecycleBin:
            m_notes.emplace_back(Localization::Lookup(IDS_INFO_PROT_RECYCLEBIN));
            m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_EMPTYBIN), .command = ID_CLEANUP_EMPTY_BIN, .enabled = true });
            break;
        case Protection::RestorePoints:
            m_notes.emplace_back(Localization::Lookup(IDS_INFO_PROT_RESTORE));
            m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_SYSPROTECT), .command = CMD_SYSPROTECT, .enabled = true });
            break;
        default:
            break;
        }

        // System-owned items require taking ownership before anything can touch them
        const std::wstring ownerLower = MakeLower(owner);
        if (ownerLower.find(L"trustedinstaller") != std::wstring::npos ||
            ownerLower.find(L"nt authority\\system") != std::wstring::npos ||
            ownerLower.ends_with(L"\\system"))
        {
            m_notes.emplace_back(Localization::Format(IDS_INFO_PROT_OWNER, owner));
        }
        return;
    }

    // Cleanup actions and their availability for ordinary items
    ActionButton recycle;
    recycle.text = Localization::Lookup(IDS_INFO_BTN_RECYCLEBIN);
    recycle.command = ID_CLEANUP_DELETE_BIN;
    ActionButton permanent;
    permanent.text = Localization::Lookup(IDS_INFO_BTN_PERMANENT);
    permanent.command = ID_CLEANUP_DELETE;

    if (!IsItemDeletable(m_item))
    {
        const std::wstring reason = m_item->IsTypeOrFlag(ITF_REMOTE) ?
            Localization::Lookup(IDS_INFO_DELETABLE_NO_REMOTE_ROOT) :
            !m_item->IsTypeOrFlag(IT_DIRECTORY, IT_FILE) ?
            Localization::Lookup(IDS_INFO_DELETABLE_NO_VIRTUAL) :
            Localization::Lookup(IDS_INFO_DELETABLE_NO_PROTECTED);
        m_deletableRowIndex = static_cast<int>(m_rows.size());
        m_deletableVerdict = 0;
        m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_DELETABLE), reason);
        return;
    }

    const bool recycleBinOk = m_item->SupportsFilesystemApis() && !m_item->IsRootItem() &&
        IsLocalDrive(m_item->GetPath());
    m_deletableRowIndex = static_cast<int>(m_rows.size());
    m_deletableVerdict = 1;
    m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_DELETABLE),
        Localization::Lookup(IDS_INFO_DELETABLE_YES));
    m_notes.emplace_back(recycleBinOk ?
        Localization::Lookup(IDS_INFO_RECYCLE_YES) : Localization::Lookup(IDS_INFO_RECYCLE_NO));
    if (attributes & FILE_ATTRIBUTE_READONLY)
    {
        m_notes.emplace_back(Localization::Lookup(IDS_INFO_READONLY));
    }
    if (attributes & FILE_ATTRIBUTE_SYSTEM)
    {
        m_notes.emplace_back(Localization::Lookup(IDS_INFO_ATTR_SYSTEM));
    }

    recycle.enabled = recycleBinOk;
    recycle.primary = true;
    permanent.enabled = true;
    permanent.primary = true;
    if (recycle.enabled) m_buttons.push_back(recycle);
    m_buttons.push_back(permanent);

    // Known relocatable app data (WeChat, QQ, WPS) gets migration guidance
    if (const std::wstring hint = MigratableHint(m_item); !hint.empty())
    {
        m_notes.emplace_back(hint);
        m_buttons.push_back({ .text = Localization::Lookup(IDS_INFO_BTN_MIGRATE), .command = CMD_MIGRATE, .enabled = true });
    }
}

void CFileInfoView::OnMigrate()
{
    const CItem* item = m_item;
    if (item == nullptr || !item->IsTypeOrFlag(IT_DIRECTORY) ||
        !item->SupportsFilesystemApis() || item->IsScanRoot())
    {
        return;
    }

    std::wstring source = item->GetPath();
    TrimString(source, L'\\', true);
    const std::wstring itemName = item->GetName();

    const auto destination = CDialog::PickFolder();
    if (!destination) return;
    std::wstring dest = *destination;
    TrimString(dest, L'\\', true);

    // The destination must be on another volume and outside the source tree
    wchar_t sourceVolume[MAX_PATH]{}, destVolume[MAX_PATH]{};
    GetVolumePathNameW(source.c_str(), sourceVolume, MAX_PATH);
    GetVolumePathNameW(dest.c_str(), destVolume, MAX_PATH);
    const std::wstring sourceVolumeLower = MakeLower(sourceVolume);
    const std::wstring destVolumeLower = MakeLower(destVolume);
    const std::wstring sourceLower = MakeLower(source);
    const std::wstring destLower = MakeLower(dest);
    if (destLower.starts_with(sourceLower + L"\\") || destLower == sourceLower ||
        sourceLower.starts_with(destLower + L"\\") || sourceVolumeLower == destVolumeLower)
    {
        MessageBoxW(GetMainWindowHandle(), Localization::Lookup(IDS_INFO_MIGRATE_BADDEST).c_str(),
            GetAppTitle().c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring destPath = dest + L"\\" + itemName;
    if (GetFileAttributesW(destPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        MessageBoxW(GetMainWindowHandle(), Localization::Lookup(IDS_INFO_MIGRATE_DESTEXISTS).c_str(),
            GetAppTitle().c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    ULARGE_INTEGER freeBytesAvailable{};
    if (GetDiskFreeSpaceExW(dest.c_str(), &freeBytesAvailable, nullptr, nullptr) &&
        freeBytesAvailable.QuadPart < item->GetSizeLogical())
    {
        MessageBoxW(GetMainWindowHandle(), Localization::Format(IDS_INFO_MIGRATE_NOSPACE,
            FormatBytes(item->GetSizeLogical()), FormatBytes(freeBytesAvailable.QuadPart)).c_str(),
            GetAppTitle().c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring confirmText = Localization::Format(IDS_INFO_MIGRATE_CONFIRM,
        source, FormatBytes(item->GetSizeLogical()), destPath);
    if (MessageBoxW(GetMainWindowHandle(), confirmText.c_str(), GetAppTitle().c_str(),
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    // Move the folder with the shell progress dialog, as the Move To command does
    bool moved = false;
    HRESULT moveResult = E_FAIL;
    CProgressDlg(0, CProgressDlg::Flags::None, GetMainWindow(), [&](const CProgressDlg* pdlg)
    {
        // Initialize an STA for the shell file operation on this worker thread.
        const ComApartmentScope com;
        if (!com) return;

        CComPtr<IFileOperation> fileOperation;
        CComPtr<IShellItem> destShellItem;
        CComPtr<IShellItem> sourceShellItem;
        const auto flags = FOFX_SHOWELEVATIONPROMPT | FOF_NOCONFIRMATION | (COptions::ShowMicrosoftProgress ? 0 : FOF_NO_UI);
        if (FAILED(::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&fileOperation))) ||
            FAILED(fileOperation->SetOwnerWindow(*pdlg)) ||
            FAILED(fileOperation->SetOperationFlags(flags)) ||
            FAILED(SHCreateItemFromParsingName(dest.c_str(), nullptr, IID_PPV_ARGS(&destShellItem))) ||
            FAILED(SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(&sourceShellItem))) ||
            FAILED(fileOperation->MoveItem(sourceShellItem, destShellItem, nullptr, nullptr)))
        {
            return;
        }

        moveResult = PerformCancellableOperations(fileOperation, flags, [pdlg] { return pdlg->IsCancelled(); });
        moved = moveResult == S_OK;
    }).ShowModal();

    if (!moved)
    {
        MessageBoxW(GetMainWindowHandle(), Localization::Format(IDS_INFO_MIGRATE_FAILED,
            TranslateError(moveResult)).c_str(), GetAppTitle().c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    // Locked files leave the source folder behind; do not junction an incomplete move
    if (FolderExists(source))
    {
        MessageBoxW(GetMainWindowHandle(), Localization::Lookup(IDS_INFO_MIGRATE_LOCKED).c_str(),
            GetAppTitle().c_str(), MB_OK | MB_ICONWARNING);
    }
    else if (AppMigrate::CreateDirectoryJunction(source, destPath))
    {
        MessageBoxW(GetMainWindowHandle(), Localization::Format(IDS_INFO_MIGRATE_DONE, destPath).c_str(),
            GetAppTitle().c_str(), MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        MessageBoxW(GetMainWindowHandle(), Localization::Format(IDS_INFO_MIGRATE_JUNCTIONFAIL,
            source, destPath).c_str(), GetAppTitle().c_str(), MB_OK | MB_ICONWARNING);
    }

    // Refresh the scan so the moved data and the junction show up correctly
    std::vector<CItem*> refresh;
    if (item->GetParent() != nullptr) refresh.push_back(item->GetParent());
    if (const auto destItem = CWinDirStatModel::Get()->GetRootItem()->FindItemByPath(dest); destItem != nullptr)
    {
        refresh.push_back(destItem);
    }
    if (!refresh.empty())
    {
        CWinDirStatModel::Get()->RefreshItem(refresh);
    }
}

void CFileInfoView::OnDraw(CDC* pdc)
{
    RefreshInfo();

    CRect rcClient = GetClientRect();
    const COLORREF windowColor = DarkMode::Color(DarkMode::ColorRole::Window);
    const COLORREF textColor = DarkMode::Color(DarkMode::ColorRole::WindowText);
    const COLORREF labelColor = DarkMode::Color(DarkMode::ColorRole::DisabledText);
    const COLORREF frameColor = DarkMode::Color(DarkMode::ColorRole::Frame);

    pdc->FillSolidRect(rcClient, windowColor);

    const ScopedBkMode bkMode(pdc, TRANSPARENT);
    const HFONT font = GetAppFont(m_hWnd);
    const CFont titleFont(font, FW_SEMIBOLD);
    const GdiObjectSelection fontSelection(pdc, font);

    // Derive the row height from the actual font so CJK text never overflows its slot
    const std::optional<TEXTMETRICW> metrics = pdc->GetTextMetrics();
    const int fontLine = metrics ?
        static_cast<int>(metrics->tmHeight + metrics->tmExternalLeading) : ScaleForDpi(16);
    const int rowHeight = std::max(ScaleForDpi(18), fontLine + ScaleForDpi(4));

    constexpr int margin = 8;
    const int pad = ScaleForDpi(margin);
    const int gap = ScaleForDpi(2);
    const int lineGap = ScaleForDpi(6);
    const int buttonHeight = rowHeight + ScaleForDpi(10);
    const int buttonGap = ScaleForDpi(8);
    const int iconTextGap = ScaleForDpi(6);
    const int noteWidth = rcClient.Width() - pad * 2;

    // Measure how many lines each guidance note occupies when wrapped
    std::vector<int> noteLines;
    noteLines.reserve(m_notes.size());
    for (const std::wstring& note : m_notes)
    {
        CRect rcMeasure(0, 0, noteWidth, 0);
        const int height = pdc->DrawText(note, rcMeasure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        noteLines.push_back(std::max(1, (height + rowHeight - 1) / rowHeight));
    }

    int y = rcClient.top + pad;

    if (m_item == nullptr)
    {
        // Nothing is selected: explain what this pane shows.
        pdc->SetTextColor(labelColor);
        CRect rcText(rcClient.left + pad, rcClient.Center().y - rowHeight,
            rcClient.right - pad, rcClient.Center().y + rowHeight * 2);
        pdc->DrawText(Localization::Lookup(IDS_INFO_SELECT_HINT), rcText,
            DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        return;
    }

    // Title: icon plus name
    {
        const GdiObjectSelection titleSelection(pdc, titleFont);
        pdc->SetTextColor(textColor);
        int textLeft = rcClient.left + pad;
        if (const HICON icon = m_item->GetIcon(); icon != nullptr)
        {
            const int iconSize = GetSystemMetrics(SM_CXSMICON);
            ::DrawIconEx(pdc->Handle(), textLeft, y + (rowHeight - iconSize) / 2,
                icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            textLeft += iconSize + iconTextGap;
        }
        CRect rcTitle(textLeft, y, rcClient.right - pad, y + rowHeight);
        pdc->DrawText(m_title, rcTitle, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    }
    y += rowHeight + gap;

    // Type subtitle and multiple-selection notice
    pdc->SetTextColor(labelColor);
    CRect rcSubtitle(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
    pdc->DrawText(m_subtitle, rcSubtitle, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    y += rowHeight + gap;

    if (m_selectionCount > 1)
    {
        CRect rcMulti(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
        pdc->DrawText(Localization::Format(IDS_INFO_MULTI, FormatCount(m_selectionCount)),
            rcMulti, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
        y += rowHeight + gap;
    }

    // Measure the wrapped button rows to reserve the bottom strip
    int buttonRows = m_buttons.empty() ? 0 : 1;
    int measureX = rcClient.left + pad;
    for (const ActionButton& button : m_buttons)
    {
        const int width = pdc->GetTextExtent(button.text).cx + ScaleForDpi(24);
        if (measureX + width > rcClient.right - pad && measureX > rcClient.left + pad)
        {
            buttonRows++;
            measureX = rcClient.left + pad;
        }
        measureX += width + buttonGap;
    }

    const int noteTotalLines = std::accumulate(noteLines.begin(), noteLines.end(), 0);
    const int notesHeight = noteTotalLines * rowHeight + static_cast<int>(m_notes.size()) * gap;
    const int buttonsHeight = buttonRows * (buttonHeight + gap);
    const int bottomStrip = lineGap + rowHeight + gap + notesHeight + buttonsHeight + rowHeight + pad;
    const int rowsBottom = rcClient.bottom - bottomStrip;

    // Verdict colors for the prominent deletability row
    const bool darkMode = DarkMode::IsDarkModeActive();
    const COLORREF verdictColor = m_deletableVerdict == 0 ?
        (darkMode ? RGB(255, 106, 106) : RGB(196, 30, 30)) :
        (darkMode ? RGB(105, 216, 157) : RGB(0, 128, 64));

    // Separator above the detail rows
    if (y < rowsBottom)
    {
        pdc->FillSolidRect(rcClient.left + pad, y, rcClient.Width() - pad * 2, 1, frameColor);
        y += lineGap;
    }

    // Detail rows: label column on the left, values on the right
    int labelWidth = 0;
    for (const auto& [label, value] : m_rows)
    {
        labelWidth = std::max(labelWidth, static_cast<int>(pdc->GetTextExtent(label).cx));
    }
    labelWidth += ScaleForDpi(10);

    int rowIndex = 0;
    for (const auto& [label, value] : m_rows)
    {
        if (y + rowHeight > rowsBottom) break;

        if (rowIndex == m_deletableRowIndex)
        {
            // Highlight band behind the verdict row so it stands out
            const CRect rcBand(rcClient.left + pad - ScaleForDpi(4), y - ScaleForDpi(2),
                rcClient.right - pad + ScaleForDpi(4), y + rowHeight + ScaleForDpi(2));
            pdc->FillSolidRect(rcBand, BlendColors(windowColor, verdictColor, 0.14));
        }

        pdc->SetTextColor(labelColor);
        CRect rcLabel(rcClient.left + pad, y, rcClient.left + pad + labelWidth, y + rowHeight);
        pdc->DrawText(label, rcLabel, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);

        if (rowIndex == m_deletableRowIndex)
        {
            // Prominent bold verdict in green or red
            const GdiObjectSelection boldSelection(pdc, titleFont);
            pdc->SetTextColor(verdictColor);
            CRect rcValue(rcLabel.right, y, rcClient.right - pad, y + rowHeight);
            pdc->DrawText(value, rcValue, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
        }
        else
        {
            pdc->SetTextColor(textColor);
            CRect rcValue(rcLabel.right, y, rcClient.right - pad, y + rowHeight);
            pdc->DrawText(value, rcValue, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
        }
        y += rowHeight + gap;
        rowIndex++;
    }

    // Cleanup and guidance section
    y = rcClient.bottom - bottomStrip;
    pdc->FillSolidRect(rcClient.left + pad, y, rcClient.Width() - pad * 2, 1, frameColor);
    y += lineGap;

    pdc->SetTextColor(labelColor);
    CRect rcHowTo(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
    pdc->DrawText(Localization::Lookup(IDS_INFO_LABEL_HOWTO), rcHowTo,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    y += rowHeight + gap;

    pdc->SetTextColor(textColor);
    for (const auto [noteIndex, note] : std::views::enumerate(m_notes))
    {
        const int lines = noteLines[noteIndex];
        if (y + lines * rowHeight > rcClient.bottom - pad) break;
        CRect rcNote(rcClient.left + pad, y, rcClient.right - pad, y + lines * rowHeight);
        pdc->DrawText(note, rcNote, DT_WORDBREAK | DT_NOPREFIX);
        y += lines * rowHeight + gap;
    }

    // Action buttons, wrapped onto several rows when needed
    int x = rcClient.left + pad;
    int index = 0;
    for (ActionButton& button : m_buttons)
    {
        const int width = pdc->GetTextExtent(button.text).cx + ScaleForDpi(24);
        if (x + width > rcClient.right - pad && x > rcClient.left + pad)
        {
            x = rcClient.left + pad;
            y += buttonHeight + gap;
        }
        button.rect = CRect(x, y, std::min<int>(x + width, rcClient.right - pad), y + buttonHeight);
        const bool hover = m_mouseTracked && button.rect.Contains(m_mousePoint);
        DrawButton(pdc, button, hover, m_pressedButton == index);
        x = button.rect.right + buttonGap;
        index++;
    }
    y += buttonHeight + gap;

    // Keyboard shortcuts hint
    if (y + rowHeight < rcClient.bottom)
    {
        pdc->SetTextColor(labelColor);
        CRect rcHint(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
        pdc->DrawText(Localization::Lookup(IDS_INFO_KEYS_HINT), rcHint,
            DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    }
}

void CFileInfoView::DrawButton(CDC* pdc, const ActionButton& button,
    const bool hover, const bool pressed) const
{
    const COLORREF controlColor = DarkMode::Color(DarkMode::ColorRole::Control);
    const COLORREF windowColor = DarkMode::Color(DarkMode::ColorRole::Window);
    const COLORREF frameColor = DarkMode::Color(DarkMode::ColorRole::Frame);
    const COLORREF textColor = DarkMode::Color(DarkMode::ColorRole::WindowText);
    const COLORREF disabledColor = DarkMode::Color(DarkMode::ColorRole::DisabledText);
    const COLORREF accentColor = DarkMode::Color(DarkMode::ColorRole::Selection);
    const COLORREF accentTextColor = DarkMode::Color(DarkMode::ColorRole::SelectionText);

    // Primary buttons (the delete actions) use a solid accent fill for prominence
    const COLORREF fill = !button.enabled ? windowColor :
        button.primary ? (pressed ? BlendColors(accentColor, RGB(0, 0, 0), 0.3) :
            hover ? BlendColors(accentColor, RGB(255, 255, 255), 0.15) : accentColor) :
        pressed ? BlendColors(controlColor, accentColor, 0.55) :
        hover ? BlendColors(controlColor, accentColor, 0.2) : controlColor;
    const COLORREF border = !button.enabled ? frameColor : button.primary ? accentColor :
        hover || pressed ? accentColor : frameColor;
    const COLORREF text = button.enabled ? (button.primary ? accentTextColor : textColor) : disabledColor;

    // Rounded body and border, drawn like the proportion bars in the file tree
    pdc->SetDCBrushColor(fill);
    pdc->SetDCPenColor(border);
    {
        const StockObjectSelection brushSelection(pdc, DC_BRUSH);
        const StockObjectSelection penSelection(pdc, DC_PEN);
        const int radius = std::min(ScaleForDpi(6), button.rect.Height() / 2 - 1);
        pdc->RoundRect(button.rect, CPoint(radius, radius));
    }

    pdc->SetTextColor(text);
    CRect rcText = button.rect;
    rcText.Deflate(ScaleForDpi(8), 0);
    pdc->DrawText(button.text, rcText,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}

int CFileInfoView::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    return CWinDirStatPane::OnCreate(lpCreateStruct);
}

void CFileInfoView::OnMouseMove(const UINT /*nFlags*/, const CPoint point)
{
    m_mousePoint = point;

    if (!m_mouseTracked)
    {
        TRACKMOUSEEVENT track{ .cbSize = sizeof(track), .dwFlags = TME_LEAVE, .hwndTrack = m_hWnd };
        if (TrackMouseEvent(&track)) m_mouseTracked = true;
    }

    // Repaint while the pointer moves across the action buttons
    for (const ActionButton& button : m_buttons)
    {
        if (button.enabled && button.rect.Contains(point))
        {
            Invalidate(false);
            break;
        }
    }
}

LRESULT CFileInfoView::OnMouseLeave(const WPARAM, const LPARAM)
{
    m_mouseTracked = false;
    m_mousePoint = CPoint(-1, -1);
    m_pressedButton = -1;
    Invalidate(false);
    return 0;
}

void CFileInfoView::OnLButtonDown(const UINT /*nFlags*/, const CPoint point)
{
    m_mousePoint = point;
    for (const auto [index, button] : std::views::enumerate(m_buttons))
    {
        if (button.enabled && button.rect.Contains(point))
        {
            m_pressedButton = static_cast<int>(index);
            SetCapture();
            Invalidate(false);
            return;
        }
    }
}

void CFileInfoView::OnLButtonUp(const UINT /*nFlags*/, const CPoint point)
{
    if (m_pressedButton < 0 || m_pressedButton >= static_cast<int>(m_buttons.size())) return;

    ::ReleaseCapture();
    const ActionButton pressed = m_buttons[static_cast<size_t>(m_pressedButton)];
    m_pressedButton = -1;
    Invalidate(false);

    if (!pressed.enabled || !pressed.rect.Contains(point)) return;

    switch (pressed.command)
    {
    case CMD_MIGRATE:
        OnMigrate();
        break;
    case CMD_SYSADVANCED:
        ShellExecuteWrapper(L"SystemPropertiesAdvanced.exe", {}, {}, GetMainWindowHandle());
        break;
    case CMD_SYSPROTECT:
        ShellExecuteWrapper(L"SystemPropertiesProtection.exe", {}, {}, GetMainWindowHandle());
        break;
    default:
        GetMainWindow()->SendMessage(WM_COMMAND, pressed.command);
        break;
    }
}
