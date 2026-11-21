// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "JianYingCleanDlg.h"
#include "MessageBoxDlg.h"
#include "HelpersInterface.h"

namespace
{
    constexpr UINT_PTR JianYingTimerId = 1;
    constexpr UINT JianYingTimerInterval = 100;
}

// ---------------------------------------------------------------------------

void CJianYingCleanDlg::CJianYingList::OnItemChanged(NMHDR* header, LRESULT* result)
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

CJianYingCleanDlg::~CJianYingCleanDlg()
{
    // Ask the workers to stop; the join waits out the current file operation
    m_scan.cancelled.store(true, std::memory_order_release);
    m_clean.cancelled.store(true, std::memory_order_release);
    if (m_worker.joinable()) m_worker.join();
}

bool CJianYingCleanDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    Localization::UpdateDialogs(*this);
    DarkMode::AdjustControls(Handle());

    m_list.SubclassDlgItem(IDC_JIANYING_LIST, this);
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() |
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const COLORREF listBack = DarkMode::SystemColor(COLOR_WINDOW);
    m_list.SetBkColor(listBack);
    m_list.SetTextBkColor(listBack);
    m_list.SetTextColor(DarkMode::SystemColor(COLOR_WINDOWTEXT));
    m_list.onChanged = [this](const int row, const bool checked) { OnRowChecked(row, checked); };

    // Columns sized proportionally to the control
    const int width = m_list.GetClientRect().Width();
    m_list.InsertColumn(0, Localization::Lookup(IDS_JIANYING_COL_TARGET), LVCFMT_LEFT, width * 6 / 10);
    m_list.InsertColumn(1, Localization::Lookup(IDS_JIANYING_COL_TYPE), LVCFMT_LEFT, width * 15 / 100);
    m_list.InsertColumn(2, Localization::Lookup(IDS_SMARTCLEAN_COL_FILES), LVCFMT_RIGHT, width * 10 / 100);
    m_list.InsertColumn(3, Localization::Lookup(IDS_SMARTCLEAN_COL_SIZE), LVCFMT_RIGHT, width - width * 85 / 100);

    SetText(IDC_JIANYING_STATUS, Localization::Lookup(IDS_JIANYING_SCANNING0));

    SetTimer(JianYingTimerId, JianYingTimerInterval);
    StartScan();
    UpdateControls();
    return true;
}

void CJianYingCleanDlg::OnRowChecked(const int row, const bool checked)
{
    if (m_populating || row < 0 || row >= static_cast<int>(m_rows.size())) return;
    const int index = m_rows[static_cast<std::size_t>(row)];
    if (index < 0) return;

    m_selected[static_cast<std::size_t>(index)] = checked;
    UpdateControls();
}

void CJianYingCleanDlg::PopulateList()
{
    const ScopedRedrawPause pause(&m_list);
    m_populating = true;
    m_list.DeleteAllItems();
    m_rows.assign(m_scan.results.size(), -1);
    m_selected.assign(m_scan.results.size(), false);

    // Show only non-empty targets, largest first
    std::vector<std::size_t> order;
    order.reserve(m_scan.results.size());
    for (const std::size_t i : std::views::iota(std::size_t{ 0 }, m_scan.results.size()))
    {
        if (m_scan.results[i].count > 0) order.push_back(i);
    }
    std::ranges::sort(order, [this](const std::size_t left, const std::size_t right)
    {
        return m_scan.results[left].size > m_scan.results[right].size;
    });

    const auto& targets = JianYingClean::Targets();
    int row = 0;
    for (const std::size_t i : order)
    {
        const JianYingClean::TargetResult& result = m_scan.results[i];

        LVITEMW item{ .mask = LVIF_TEXT | LVIF_PARAM | LVIF_STATE, .iItem = row, .lParam = static_cast<LPARAM>(i) };
        item.stateMask = LVIS_STATEIMAGEMASK;
        item.state = INDEXTOSTATEIMAGEMASK(2);
        std::wstring path = result.path;
        item.pszText = path.data();
        m_list.InsertItem(&item);
        m_list.SetItemText(row, 1, Localization::Lookup(targets[result.type].typeId));
        m_list.SetItemText(row, 2, std::to_wstring(result.count));
        m_list.SetItemText(row, 3, FormatBytes(result.size));

        m_rows[static_cast<std::size_t>(row)] = static_cast<int>(i);
        m_selected[i] = true;
        row++;
    }
    m_populating = false;
    UpdateControls();
}

void CJianYingCleanDlg::UpdateControls()
{
    const int checkedCount = SelectedCount();
    const bool busy = m_scanActive || m_cleanActive;

    // Lock the list while a worker owns the shared scan/selection state
    m_list.EnableWindow(!busy);
    GetDlgItem(IDC_JIANYING_CLEAN).EnableWindow(!busy && m_scanned && checkedCount > 0);
    GetDlgItem(IDC_JIANYING_SELECTALL).EnableWindow(!busy && m_scanned);
    GetDlgItem(IDC_JIANYING_SCAN).EnableWindow(!m_cleanActive);

    SetText(IDC_JIANYING_SCAN, m_scanActive ? Localization::Lookup(IDS_GENERIC_CANCEL) :
        Localization::Lookup(m_scanned ? IDS_SMARTCLEAN_RESCAN : IDS_SMARTCLEAN_SCAN));
    SetText(IDC_JIANYING_CLEAN, checkedCount > 0 ?
        Localization::Format(IDS_SMARTCLEAN_CLEANBTN, checkedCount) :
        Localization::Lookup(IDS_SMARTCLEAN_CLEAN));
}

int CJianYingCleanDlg::SelectedCount() const
{
    int count = 0;
    for (const std::size_t i : std::views::iota(std::size_t{ 0 }, m_selected.size()))
    {
        if (m_selected[i]) count += static_cast<int>(m_scan.results[i].count);
    }
    return count;
}

ULONGLONG CJianYingCleanDlg::SelectedSize() const
{
    ULONGLONG size = 0;
    for (const std::size_t i : std::views::iota(std::size_t{ 0 }, m_selected.size()))
    {
        if (m_selected[i]) size += m_scan.results[i].size;
    }
    return size;
}

void CJianYingCleanDlg::OnScan()
{
    if (m_cleanActive) return;
    if (m_scanActive)
    {
        m_scan.cancelled.store(true, std::memory_order_release);
        return;
    }
    StartScan();
}

void CJianYingCleanDlg::StartScan()
{
    if (m_worker.joinable()) m_worker.join();

    m_scanActive = true;
    m_scanned = false;
    m_list.DeleteAllItems();
    m_rows.clear();
    m_selected.clear();
    SetText(IDC_JIANYING_STATUS, Localization::Lookup(IDS_JIANYING_SCANNING0));
    UpdateControls();

    m_worker = std::jthread([this] { JianYingClean::RunScan(m_scan); });
}

void CJianYingCleanDlg::OnClean()
{
    if (m_scanActive || m_cleanActive || !m_scanned) return;

    const int count = SelectedCount();
    if (count <= 0) return;

    const std::wstring message = Localization::Format(IDS_JIANYING_CONFIRM,
        count, FormatBytes(SelectedSize()));
    if (ShowMessageBox(message, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    if (m_worker.joinable()) m_worker.join();
    m_cleanActive = true;
    UpdateControls();

    m_worker = std::jthread([this] { JianYingClean::RunClean(m_scan, m_selected, m_clean); });
}

void CJianYingCleanDlg::OnSelectAll()
{
    if (m_scanActive || m_cleanActive || !m_scanned) return;

    bool anyUnchecked = false;
    for (const int row : std::views::iota(0, static_cast<int>(m_rows.size())))
    {
        if (!m_list.GetCheck(row)) anyUnchecked = true;
    }

    for (const int row : std::views::iota(0, static_cast<int>(m_rows.size())))
    {
        m_list.SetCheck(row, anyUnchecked);
    }
}

void CJianYingCleanDlg::OnTimer(const UINT_PTR id)
{
    if (id != JianYingTimerId) return;

    if (m_scanActive)
    {
        if (m_scan.running.load(std::memory_order_acquire))
        {
            SetText(IDC_JIANYING_STATUS, Localization::Format(IDS_JIANYING_SCANNING,
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
            SetText(IDC_JIANYING_STATUS, Localization::Format(IDS_JIANYING_CLEANING,
                m_clean.filesProcessed.load(std::memory_order_relaxed),
                m_clean.filesTotal.load(std::memory_order_relaxed)));
            return;
        }
        m_cleanActive = false;
        FinishClean();
    }
}

void CJianYingCleanDlg::FinishScan()
{
    PopulateList();

    int totalFiles = 0;
    ULONGLONG totalSize = 0;
    for (const auto& result : m_scan.results)
    {
        totalFiles += static_cast<int>(result.count);
        totalSize += result.size;
    }

    std::wstring status = totalFiles == 0 ? Localization::Lookup(IDS_JIANYING_NONE) :
        Localization::Format(IDS_JIANYING_SCANDONE, totalFiles, FormatBytes(totalSize));
    if (JianYingClean::IsAppRunning())
    {
        status += L" " + Localization::Lookup(IDS_JIANYING_RUNNING);
    }
    SetText(IDC_JIANYING_STATUS, status);
    UpdateControls();
}

void CJianYingCleanDlg::FinishClean()
{
    // Apply the per-target results to the scan snapshot, then rebuild rows
    for (const std::size_t i : std::views::iota(std::size_t{ 0 }, m_scan.results.size()))
    {
        auto& result = m_scan.results[i];
        result.count = static_cast<std::size_t>(std::max<INT_PTR>(0,
            static_cast<INT_PTR>(result.count) - m_clean.deletedPerTarget[i]));
        result.size = result.size > m_clean.freedPerTarget[i] ?
            result.size - m_clean.freedPerTarget[i] : 0;
    }

    PopulateList();

    std::wstring summary = Localization::Format(IDS_JIANYING_DONE,
        FormatBytes(m_clean.bytesFreed.load(std::memory_order_relaxed)),
        m_clean.filesDeleted.load(std::memory_order_relaxed));
    if (const int reboot = m_clean.filesReboot.load(std::memory_order_relaxed); reboot > 0)
    {
        summary += L" " + Localization::Format(IDS_JIANYING_DONE_REBOOT, reboot);
    }
    if (const int failed = m_clean.filesFailed.load(std::memory_order_relaxed); failed > 0)
    {
        summary += L" " + Localization::Format(IDS_JIANYING_DONE_FAILED, failed);
    }
    SetText(IDC_JIANYING_STATUS, summary);
    UpdateControls();
}
