// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "SmartCleanDlg.h"
#include "MessageBoxDlg.h"
#include "HelpersInterface.h"

namespace
{
    constexpr UINT_PTR SmartCleanTimerId = 1;
    constexpr UINT SmartCleanTimerInterval = 100;
}

// ---------------------------------------------------------------------------

void CSmartCleanDlg::CSmartCleanList::OnItemChanged(NMHDR* header, LRESULT* result)
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

CSmartCleanDlg::~CSmartCleanDlg()
{
    // Ask the workers to stop; the join waits out the current file operation
    m_scan.cancelled.store(true, std::memory_order_release);
    m_clean.cancelled.store(true, std::memory_order_release);
    if (m_worker.joinable()) m_worker.join();
}

bool CSmartCleanDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    Localization::UpdateDialogs(*this);
    DarkMode::AdjustControls(Handle());

    m_list.SubclassDlgItem(IDC_SMARTCLEAN_LIST, this);
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() |
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const COLORREF listBack = DarkMode::SystemColor(COLOR_WINDOW);
    m_list.SetBkColor(listBack);
    m_list.SetTextBkColor(listBack);
    m_list.SetTextColor(DarkMode::SystemColor(COLOR_WINDOWTEXT));
    m_list.onChanged = [this](const int row, const bool checked) { OnRowChecked(row, checked); };

    // Columns sized proportionally to the control
    const int width = m_list.GetClientRect().Width();
    m_list.InsertColumn(0, Localization::Lookup(IDS_SMARTCLEAN_COL_CATEGORY), LVCFMT_LEFT, width * 5 / 10);
    m_list.InsertColumn(1, Localization::Lookup(IDS_SMARTCLEAN_COL_FILES), LVCFMT_RIGHT, width * 15 / 100);
    m_list.InsertColumn(2, Localization::Lookup(IDS_SMARTCLEAN_COL_SIZE), LVCFMT_RIGHT, width * 2 / 10);
    m_list.InsertColumn(3, Localization::Lookup(IDS_SMARTCLEAN_COL_RISK), LVCFMT_LEFT, width - width * 85 / 100);

    SetText(IDC_SMARTCLEAN_NOTE, Localization::Lookup(IDS_SMARTCLEAN_NOTE));
    SetText(IDC_SMARTCLEAN_STATUS, Localization::Lookup(IDS_SMARTCLEAN_READY));
    m_selected.assign(std::to_underlying(SmartClean::Category::Count), false);

    SetTimer(SmartCleanTimerId, SmartCleanTimerInterval);
    UpdateControls();
    return true;
}

void CSmartCleanDlg::OnRowChecked(const int row, const bool checked)
{
    if (m_populating || row < 0 || row >= static_cast<int>(m_rows.size())) return;
    const int index = m_rows[static_cast<std::size_t>(row)];
    if (index < 0) return;

    const SmartClean::CategoryInfo& info = SmartClean::Categories()[static_cast<std::size_t>(index)];
    if (info.systemManaged)
    {
        // System-managed locations never leave through this dialog
        m_list.SetCheck(row, false);
        SetText(IDC_SMARTCLEAN_STATUS, Localization::Lookup(IDS_SMARTCLEAN_SYSTEMHINT));
        return;
    }

    m_selected[static_cast<std::size_t>(index)] = checked;
    UpdateControls();
}

void CSmartCleanDlg::PopulateList()
{
    const ScopedRedrawPause pause(&m_list);
    m_populating = true;
    m_list.DeleteAllItems();
    m_rows.fill(-1);
    m_selected.assign(m_selected.size(), false);

    // Show only non-empty categories, largest first
    std::vector<int> order;
    order.reserve(m_scan.results.size());
    for (const int index : std::views::iota(0, static_cast<int>(m_scan.results.size())))
    {
        if (m_scan.results[static_cast<std::size_t>(index)].count > 0) order.push_back(index);
    }
    std::ranges::sort(order, [this](const int left, const int right)
    {
        return m_scan.results[static_cast<std::size_t>(left)].size >
            m_scan.results[static_cast<std::size_t>(right)].size;
    });

    const auto& categories = SmartClean::Categories();
    int row = 0;
    for (const int index : order)
    {
        const std::size_t i = static_cast<std::size_t>(index);
        const SmartClean::CategoryResult& result = m_scan.results[i];
        const SmartClean::CategoryInfo& info = categories[i];

        LVITEMW item{ .mask = LVIF_TEXT | LVIF_PARAM | LVIF_STATE, .iItem = row, .lParam = index };
        item.stateMask = LVIS_STATEIMAGEMASK;
        item.state = info.systemManaged ? 0 : INDEXTOSTATEIMAGEMASK(info.risk <= 2 ? 2 : 1);
        std::wstring name = Localization::Lookup(info.nameId);
        item.pszText = name.data();
        m_list.InsertItem(&item);
        m_list.SetItemText(row, 1, std::to_wstring(result.count));
        m_list.SetItemText(row, 2, FormatBytes(result.size));
        m_list.SetItemText(row, 3, Localization::Lookup(
            info.risk <= 1 ? IDS_SMARTCLEAN_RISK_1 :
            info.risk == 2 ? IDS_SMARTCLEAN_RISK_2 : IDS_SMARTCLEAN_RISK_3));

        m_rows[static_cast<std::size_t>(row)] = index;
        m_selected[i] = !info.systemManaged && info.risk <= 2;
        row++;
    }
    m_populating = false;
    UpdateControls();
}

void CSmartCleanDlg::UpdateControls()
{
    const int checkedCount = SelectedCount();
    const bool busy = m_scanActive || m_cleanActive;

    // Lock the list while a worker owns the shared scan/selection state
    m_list.EnableWindow(!busy);
    GetDlgItem(IDC_SMARTCLEAN_CLEAN).EnableWindow(!busy && m_scanned && checkedCount > 0);
    GetDlgItem(IDC_SMARTCLEAN_SELECTALL).EnableWindow(!busy && m_scanned);
    GetDlgItem(IDC_SMARTCLEAN_SCAN).EnableWindow(!m_cleanActive);

    SetText(IDC_SMARTCLEAN_SCAN, m_scanActive ? Localization::Lookup(IDS_GENERIC_CANCEL) :
        Localization::Lookup(m_scanned ? IDS_SMARTCLEAN_RESCAN : IDS_SMARTCLEAN_SCAN));
    SetText(IDC_SMARTCLEAN_CLEAN, checkedCount > 0 ?
        Localization::Format(IDS_SMARTCLEAN_CLEANBTN, checkedCount) :
        Localization::Lookup(IDS_SMARTCLEAN_CLEAN));
}

int CSmartCleanDlg::SelectedCount() const
{
    int count = 0;
    for (const int index : std::views::iota(0, static_cast<int>(m_selected.size())))
    {
        if (m_selected[static_cast<std::size_t>(index)]) count +=
            static_cast<int>(m_scan.results[static_cast<std::size_t>(index)].count);
    }
    return count;
}

ULONGLONG CSmartCleanDlg::SelectedSize() const
{
    ULONGLONG size = 0;
    for (const int index : std::views::iota(0, static_cast<int>(m_selected.size())))
    {
        if (m_selected[static_cast<std::size_t>(index)]) size += m_scan.results[static_cast<std::size_t>(index)].size;
    }
    return size;
}

void CSmartCleanDlg::OnScan()
{
    if (m_cleanActive) return;
    if (m_scanActive)
    {
        m_scan.cancelled.store(true, std::memory_order_release);
        return;
    }
    StartScan();
}

void CSmartCleanDlg::StartScan()
{
    if (m_worker.joinable()) m_worker.join();

    m_scanActive = true;
    m_scanned = false;
    m_list.DeleteAllItems();
    m_rows.fill(-1);
    m_selected.assign(m_selected.size(), false);
    SetText(IDC_SMARTCLEAN_STATUS, Localization::Lookup(IDS_SMARTCLEAN_SCANNING0));
    UpdateControls();

    m_worker = std::jthread([this] { SmartClean::ScanAll(m_scan); });
}

void CSmartCleanDlg::OnClean()
{
    if (m_scanActive || m_cleanActive || !m_scanned) return;

    const int count = SelectedCount();
    if (count <= 0) return;

    const std::wstring message = Localization::Format(IDS_SMARTCLEAN_CONFIRM,
        count, FormatBytes(SelectedSize()));
    if (ShowMessageBox(message, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    if (m_worker.joinable()) m_worker.join();
    m_cleanActive = true;
    UpdateControls();

    m_worker = std::jthread([this] { SmartClean::CleanSelected(m_scan, m_selected, m_clean); });
}

void CSmartCleanDlg::OnSelectAll()
{
    if (m_scanActive || m_cleanActive || !m_scanned) return;

    const auto& categories = SmartClean::Categories();
    bool anyUnchecked = false;
    for (const int row : std::views::iota(0, static_cast<int>(m_rows.size())))
    {
        const int index = m_rows[static_cast<std::size_t>(row)];
        if (index < 0 || categories[static_cast<std::size_t>(index)].systemManaged) continue;
        if (!m_list.GetCheck(row)) anyUnchecked = true;
    }

    for (const int row : std::views::iota(0, static_cast<int>(m_rows.size())))
    {
        const int index = m_rows[static_cast<std::size_t>(row)];
        if (index < 0 || categories[static_cast<std::size_t>(index)].systemManaged) continue;
        m_list.SetCheck(row, anyUnchecked);
    }
}

void CSmartCleanDlg::OnTimer(const UINT_PTR id)
{
    if (id != SmartCleanTimerId) return;

    if (m_scanActive)
    {
        if (m_scan.running.load(std::memory_order_acquire))
        {
            SetText(IDC_SMARTCLEAN_STATUS, Localization::Format(IDS_SMARTCLEAN_SCANNING,
                m_scan.filesFound.load(std::memory_order_relaxed)));
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
            SetText(IDC_SMARTCLEAN_STATUS, Localization::Format(IDS_SMARTCLEAN_CLEANING,
                m_clean.filesProcessed.load(std::memory_order_relaxed),
                m_clean.filesTotal.load(std::memory_order_relaxed)));
            return;
        }
        m_cleanActive = false;
        FinishClean();
    }
}

void CSmartCleanDlg::FinishScan()
{
    PopulateList();

    int totalFiles = 0;
    ULONGLONG totalSize = 0;
    for (const auto& result : m_scan.results)
    {
        totalFiles += static_cast<int>(result.count);
        totalSize += result.size;
    }
    SetText(IDC_SMARTCLEAN_STATUS, totalFiles == 0 ? Localization::Lookup(IDS_SMARTCLEAN_NONE) :
        Localization::Format(IDS_SMARTCLEAN_SCANDONE, totalFiles, FormatBytes(totalSize)));
    UpdateControls();
}

void CSmartCleanDlg::FinishClean()
{
    // Apply the per-category results to the scan snapshot, then rebuild rows
    for (const int index : std::views::iota(0, static_cast<int>(m_scan.results.size())))
    {
        const std::size_t i = static_cast<std::size_t>(index);
        auto& result = m_scan.results[i];
        result.count = static_cast<std::size_t>(std::max<INT_PTR>(0,
            static_cast<INT_PTR>(result.count) - m_clean.deletedPerCategory[i]));
        result.size = result.size > m_clean.freedPerCategory[i] ?
            result.size - m_clean.freedPerCategory[i] : 0;
    }

    PopulateList();

    std::wstring summary = Localization::Format(IDS_SMARTCLEAN_DONE,
        FormatBytes(m_clean.bytesFreed.load(std::memory_order_relaxed)),
        m_clean.filesDeleted.load(std::memory_order_relaxed));
    if (const int reboot = m_clean.filesReboot.load(std::memory_order_relaxed); reboot > 0)
    {
        summary += L" " + Localization::Format(IDS_SMARTCLEAN_DONE_REBOOT, reboot);
    }
    if (const int failed = m_clean.filesFailed.load(std::memory_order_relaxed); failed > 0)
    {
        summary += L" " + Localization::Format(IDS_SMARTCLEAN_DONE_FAILED, failed);
    }
    SetText(IDC_SMARTCLEAN_STATUS, summary);
    UpdateControls();
}
