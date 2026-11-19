// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "DuplicateCleanDlg.h"
#include "MessageBoxDlg.h"
#include "HelpersInterface.h"
#include "WinDirStatModel.h"

namespace
{
    constexpr UINT_PTR DuplicateCleanTimerId = 1;
    constexpr UINT DuplicateCleanTimerInterval = 100;
}

// ---------------------------------------------------------------------------

void CDuplicateCleanDlg::CDuplicateCleanList::OnItemChanged(NMHDR* header, LRESULT* result)
{
    *result = 0;
    const auto changed = reinterpret_cast<NMLISTVIEW*>(header);
    if (changed == nullptr || !(changed->uChanged & LVIF_STATE) ||
        (changed->uNewState & LVIS_STATEIMAGEMASK) == (changed->uOldState & LVIS_STATEIMAGEMASK))
    {
        return;
    }
    if (onChanged)
    {
        onChanged(changed->iItem, ((changed->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2);
    }
}

CDuplicateCleanDlg::~CDuplicateCleanDlg()
{
    // Ask the workers to stop; the join waits out the current file operation
    m_scan.cancelled.store(true, std::memory_order_release);
    m_clean.cancelled.store(true, std::memory_order_release);
    if (m_worker.joinable()) m_worker.join();
}

bool CDuplicateCleanDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    Localization::UpdateDialogs(*this);
    DarkMode::AdjustControls(Handle());

    m_list.SubclassDlgItem(IDC_DUPCLEAN_LIST, this);
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() |
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const COLORREF listBack = DarkMode::SystemColor(COLOR_WINDOW);
    m_list.SetBkColor(listBack);
    m_list.SetTextBkColor(listBack);
    m_list.SetTextColor(DarkMode::SystemColor(COLOR_WINDOWTEXT));
    m_list.onChanged = [this](const int row, const bool checked) { OnRowChecked(row, checked); };

    // Columns sized proportionally to the control
    const int width = m_list.GetClientRect().Width();
    m_list.InsertColumn(0, Localization::Lookup(IDS_DUPCLEAN_COL_FILE), LVCFMT_LEFT, width * 55 / 100);
    m_list.InsertColumn(1, Localization::Lookup(IDS_DUPCLEAN_COL_SIZE), LVCFMT_RIGHT, width * 2 / 10);
    m_list.InsertColumn(2, Localization::Lookup(IDS_DUPCLEAN_COL_MODIFIED), LVCFMT_LEFT, width - width * 75 / 100);

    SetText(IDC_DUPCLEAN_NOTE, Localization::Lookup(IDS_DUPCLEAN_NOTE));
    SetText(IDC_DUPCLEAN_STATUS, Localization::Lookup(IDS_DUPCLEAN_READY));

    SetTimer(DuplicateCleanTimerId, DuplicateCleanTimerInterval);
    UpdateControls();
    return true;
}

void CDuplicateCleanDlg::OnRowChecked(const int row, const bool checked)
{
    if (m_populating || row < 0 || row >= static_cast<int>(m_rows.size())) return;
    const RowRef ref = m_rows[static_cast<std::size_t>(row)];
    if (ref.group < 0 || ref.file < 0) return;

    // A group must always keep at least one copy on disk
    if (checked)
    {
        int unchecked = 0;
        for (const bool selected : m_selected[static_cast<std::size_t>(ref.group)])
        {
            if (!selected) unchecked++;
        }
        if (unchecked <= 1)
        {
            m_list.SetCheck(row, false);
            SetText(IDC_DUPCLEAN_STATUS, Localization::Lookup(IDS_DUPCLEAN_KEEPLAST));
            return;
        }
    }

    m_selected[static_cast<std::size_t>(ref.group)][static_cast<std::size_t>(ref.file)] = checked;
    UpdateControls();
}

void CDuplicateCleanDlg::PopulateList()
{
    const ScopedRedrawPause pause(&m_list);
    m_populating = true;
    m_list.DeleteAllItems();
    m_rows.clear();
    m_selected.assign(m_scan.groups.size(), {});

    int row = 0;
    for (const int groupIndex : std::views::iota(0, static_cast<int>(m_scan.groups.size())))
    {
        const DuplicateClean::DupGroup& group = m_scan.groups[static_cast<std::size_t>(groupIndex)];
        auto& selection = m_selected[static_cast<std::size_t>(groupIndex)];
        selection.assign(group.files.size(), false);

        // Group header without a check box
        LVITEMW header{ .mask = LVIF_TEXT, .iItem = row };
        std::wstring caption = Localization::Format(IDS_DUPCLEAN_GROUP,
            groupIndex + 1, static_cast<int>(group.files.size()), FormatBytes(group.size));
        header.pszText = caption.data();
        m_list.InsertItem(&header);
        m_list.SetItemText(row, 2, FormatBytes(group.wasted));
        m_rows.push_back({ groupIndex, -1 });
        row++;

        // File rows; the oldest copy (index 0) is the default keeper
        for (const int fileIndex : std::views::iota(0, static_cast<int>(group.files.size())))
        {
            const DuplicateClean::FileEntry& entry = group.files[static_cast<std::size_t>(fileIndex)];
            const bool keep = fileIndex == 0;
            LVITEMW item{ .mask = LVIF_TEXT | LVIF_STATE, .iItem = row };
            item.stateMask = LVIS_STATEIMAGEMASK;
            item.state = INDEXTOSTATEIMAGEMASK(keep ? 1 : 2);
            std::wstring path = entry.path;
            item.pszText = path.data();
            m_list.InsertItem(&item);
            m_list.SetItemText(row, 1, FormatBytes(entry.size));
            m_list.SetItemText(row, 2, entry.modified);
            m_rows.push_back({ groupIndex, fileIndex });
            selection[static_cast<std::size_t>(fileIndex)] = !keep;
            row++;
        }
    }

    m_populating = false;
    UpdateControls();
}

void CDuplicateCleanDlg::UpdateControls()
{
    const int checkedCount = SelectedCount();
    const bool busy = m_scanActive || m_cleanActive;

    // Lock the list while a worker owns the shared scan/selection state
    m_list.EnableWindow(!busy);
    GetDlgItem(IDC_DUPCLEAN_CLEAN).EnableWindow(!busy && m_scanned && checkedCount > 0);
    GetDlgItem(IDC_DUPCLEAN_SELECTALL).EnableWindow(!busy && m_scanned);
    GetDlgItem(IDC_DUPCLEAN_SCAN).EnableWindow(!m_cleanActive);

    SetText(IDC_DUPCLEAN_SCAN, m_scanActive ? Localization::Lookup(IDS_GENERIC_CANCEL) :
        Localization::Lookup(m_scanned ? IDS_SMARTCLEAN_RESCAN : IDS_SMARTCLEAN_SCAN));
    SetText(IDC_DUPCLEAN_CLEAN, checkedCount > 0 ?
        Localization::Format(IDS_DUPCLEAN_CLEANBTN, checkedCount) :
        Localization::Lookup(IDS_DUPCLEAN_CLEAN));
}

int CDuplicateCleanDlg::SelectedCount() const
{
    int count = 0;
    for (const auto& group : m_selected)
    {
        for (const bool selected : group) count += selected ? 1 : 0;
    }
    return count;
}

ULONGLONG CDuplicateCleanDlg::SelectedSize() const
{
    ULONGLONG size = 0;
    for (const int groupIndex : std::views::iota(0, static_cast<int>(m_selected.size())))
    {
        const auto& group = m_selected[static_cast<std::size_t>(groupIndex)];
        for (const int fileIndex : std::views::iota(0, static_cast<int>(group.size())))
        {
            if (group[static_cast<std::size_t>(fileIndex)])
            {
                size += m_scan.groups[static_cast<std::size_t>(groupIndex)]
                    .files[static_cast<std::size_t>(fileIndex)].size;
            }
        }
    }
    return size;
}

void CDuplicateCleanDlg::OnScan()
{
    if (m_cleanActive) return;
    if (m_scanActive)
    {
        m_scan.cancelled.store(true, std::memory_order_release);
        return;
    }

    CItem* root = CWinDirStatModel::Get()->GetRootItem();
    if (root == nullptr)
    {
        SetText(IDC_DUPCLEAN_STATUS, Localization::Lookup(IDS_DUPCLEAN_NOSCAN));
        return;
    }
    StartScan();
}

void CDuplicateCleanDlg::StartScan()
{
    if (m_worker.joinable()) m_worker.join();

    m_scanActive = true;
    m_scanned = false;
    m_list.DeleteAllItems();
    m_rows.clear();
    SetText(IDC_DUPCLEAN_STATUS, Localization::Lookup(IDS_DUPCLEAN_SCANNING0));
    UpdateControls();

    m_worker = std::jthread([this]
    {
        DuplicateClean::RunScan(CWinDirStatModel::Get()->GetRootItem(), m_scan);
    });
}

void CDuplicateCleanDlg::OnClean()
{
    if (m_scanActive || m_cleanActive || !m_scanned) return;

    const int count = SelectedCount();
    if (count <= 0) return;

    const std::wstring message = Localization::Format(IDS_DUPCLEAN_CONFIRM,
        count, FormatBytes(SelectedSize()));
    if (ShowMessageBox(message, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    if (m_worker.joinable()) m_worker.join();
    m_cleanActive = true;
    UpdateControls();

    m_worker = std::jthread([this]
    {
        std::vector<std::wstring> paths;
        std::vector<ULONGLONG> sizes;
        for (const int groupIndex : std::views::iota(0, static_cast<int>(m_selected.size())))
        {
            const auto& group = m_selected[static_cast<std::size_t>(groupIndex)];
            for (const int fileIndex : std::views::iota(0, static_cast<int>(group.size())))
            {
                if (group[static_cast<std::size_t>(fileIndex)])
                {
                    const DuplicateClean::FileEntry& entry =
                        m_scan.groups[static_cast<std::size_t>(groupIndex)]
                            .files[static_cast<std::size_t>(fileIndex)];
                    paths.push_back(entry.path);
                    sizes.push_back(entry.size);
                }
            }
        }
        DuplicateClean::RunClean(paths, sizes, m_clean);
    });
}

void CDuplicateCleanDlg::OnSelectAll()
{
    if (m_scanActive || m_cleanActive || !m_scanned) return;

    // Toggle: check every file except the first of each group, or clear all
    bool anyUncheckedExceptFirst = false;
    for (const int groupIndex : std::views::iota(0, static_cast<int>(m_selected.size())))
    {
        const auto& group = m_selected[static_cast<std::size_t>(groupIndex)];
        for (const int fileIndex : std::views::iota(1, static_cast<int>(group.size())))
        {
            if (!group[static_cast<std::size_t>(fileIndex)]) anyUncheckedExceptFirst = true;
        }
    }

    const bool check = anyUncheckedExceptFirst;
    for (const int row : std::views::iota(0, static_cast<int>(m_rows.size())))
    {
        const RowRef ref = m_rows[static_cast<std::size_t>(row)];
        if (ref.file <= 0) continue;   // Headers and the per-group keeper
        m_list.SetCheck(row, check);
    }
}

void CDuplicateCleanDlg::OnTimer(const UINT_PTR id)
{
    if (id != DuplicateCleanTimerId) return;

    if (m_scanActive)
    {
        if (m_scan.running.load(std::memory_order_acquire))
        {
            SetText(IDC_DUPCLEAN_STATUS, Localization::Format(IDS_DUPCLEAN_SCANNING,
                m_scan.hashed.load(std::memory_order_relaxed),
                m_scan.candidates.load(std::memory_order_relaxed)));
            return;
        }
        m_scanActive = false;
        m_scanned = true;
        FinishScan();
        return;
    }

    if (m_cleanActive)
    {
        if (m_clean.running.load(std::memory_order_acquire))
        {
            SetText(IDC_DUPCLEAN_STATUS, Localization::Format(IDS_DUPCLEAN_CLEANING,
                m_clean.processed.load(std::memory_order_relaxed),
                m_clean.filesTotal.load(std::memory_order_relaxed)));
            return;
        }
        m_cleanActive = false;
        FinishClean();
    }
}

void CDuplicateCleanDlg::FinishScan()
{
    PopulateList();

    int redundant = 0;
    ULONGLONG wasted = 0;
    for (const auto& group : m_scan.groups)
    {
        redundant += static_cast<int>(group.files.size()) - 1;
        wasted += group.wasted;
    }
    SetText(IDC_DUPCLEAN_STATUS, redundant == 0 ? Localization::Lookup(IDS_DUPCLEAN_NONE) :
        Localization::Format(IDS_DUPCLEAN_SCANDONE,
            static_cast<int>(m_scan.groups.size()), redundant, FormatBytes(wasted)));
    UpdateControls();
}

void CDuplicateCleanDlg::FinishClean()
{
    // Drop the removed copies from the groups, then drop emptied groups
    const std::unordered_set<std::wstring> removed(m_clean.removedPaths.begin(),
        m_clean.removedPaths.end());
    for (auto& group : m_scan.groups)
    {
        std::erase_if(group.files, [&removed](const DuplicateClean::FileEntry& entry)
        {
            return removed.contains(entry.path);
        });
        group.wasted = group.files.size() > 1 ? group.size * (group.files.size() - 1) : 0;
    }
    std::erase_if(m_scan.groups, [](const DuplicateClean::DupGroup& group)
    {
        return group.files.size() < 2;
    });

    PopulateList();

    std::wstring summary = Localization::Format(IDS_DUPCLEAN_DONE,
        m_clean.removed.load(std::memory_order_relaxed),
        FormatBytes(m_clean.bytesFreed.load(std::memory_order_relaxed)));
    if (const int failed = m_clean.failed.load(std::memory_order_relaxed); failed > 0)
    {
        summary += L" " + Localization::Format(IDS_DUPCLEAN_DONE_FAILED, failed);
    }
    SetText(IDC_DUPCLEAN_STATUS, summary);
    UpdateControls();
}
