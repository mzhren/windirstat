// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "AppMigrateDlg.h"
#include "MessageBoxDlg.h"
#include "HelpersInterface.h"

namespace
{
    constexpr UINT_PTR AppMigrateTimerId = 1;
    constexpr UINT AppMigrateTimerInterval = 100;
}

// ---------------------------------------------------------------------------

void CAppMigrateDlg::CAppMigrateList::OnItemChanged(NMHDR* header, LRESULT* result)
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

CAppMigrateDlg::~CAppMigrateDlg()
{
    m_detect.cancelled.store(true, std::memory_order_release);
    m_migrate.cancelled.store(true, std::memory_order_release);
    if (m_worker.joinable()) m_worker.join();
}

bool CAppMigrateDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    const AppMigrate::PresetInfo& info = AppMigrate::Presets(m_preset);
    Localization::UpdateDialogs(*this);
    SetText(Localization::Lookup(info.titleId));
    SetText(IDC_APPMIGRATE_DESC, Localization::Lookup(info.descId));
    SetText(IDC_APPMIGRATE_NOTE, Localization::Lookup(info.noteId));
    SetText(IDC_APPMIGRATE_DESTPATH, Localization::Lookup(IDS_MIGRATE_DEST_EMPTY));
    DarkMode::AdjustControls(Handle());

    m_list.SubclassDlgItem(IDC_APPMIGRATE_LIST, this);
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() |
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const COLORREF listBack = DarkMode::SystemColor(COLOR_WINDOW);
    m_list.SetBkColor(listBack);
    m_list.SetTextBkColor(listBack);
    m_list.SetTextColor(DarkMode::SystemColor(COLOR_WINDOWTEXT));
    m_list.onChanged = [this](const int row, const bool checked) { OnRowChecked(row, checked); };

    const int width = m_list.GetClientRect().Width();
    m_list.InsertColumn(0, Localization::Lookup(IDS_DUPCLEAN_COL_FILE), LVCFMT_LEFT, width * 8 / 10);
    m_list.InsertColumn(1, Localization::Lookup(IDS_DUPCLEAN_COL_SIZE), LVCFMT_RIGHT, width - width * 8 / 10);

    SetText(IDC_APPMIGRATE_STATUS, Localization::Lookup(IDS_MIGRATE_DETECT));

    SetTimer(AppMigrateTimerId, AppMigrateTimerInterval);
    StartDetect();
    UpdateControls();
    return true;
}

void CAppMigrateDlg::OnRowChecked(const int row, const bool checked)
{
    if (m_populating || row < 0 || row >= static_cast<int>(m_selected.size())) return;
    m_selected[static_cast<std::size_t>(row)] = checked;
    UpdateControls();
}

void CAppMigrateDlg::PopulateList()
{
    const ScopedRedrawPause pause(&m_list);
    m_populating = true;
    m_list.DeleteAllItems();
    m_selected.assign(m_detect.candidates.size(), true);

    for (const int index : std::views::iota(0, static_cast<int>(m_detect.candidates.size())))
    {
        const AppMigrate::Candidate& candidate = m_detect.candidates[static_cast<std::size_t>(index)];
        LVITEMW item{ .mask = LVIF_TEXT | LVIF_STATE, .iItem = index };
        item.stateMask = LVIS_STATEIMAGEMASK;
        item.state = INDEXTOSTATEIMAGEMASK(2);
        std::wstring path = candidate.path;
        item.pszText = path.data();
        m_list.InsertItem(&item);
        m_list.SetItemText(index, 1, FormatBytes(candidate.size));
    }

    m_populating = false;
    UpdateControls();
}

void CAppMigrateDlg::UpdateControls()
{
    const int checkedCount = SelectedCount();
    const bool busy = m_detectActive || m_migrateActive;

    m_list.EnableWindow(!busy && m_detected);
    GetDlgItem(IDC_APPMIGRATE_MIGRATE).EnableWindow(!busy && m_detected && checkedCount > 0 && !m_dest.empty());
    GetDlgItem(IDC_APPMIGRATE_PICKDEST).EnableWindow(!busy);

    SetText(IDC_APPMIGRATE_MIGRATE, checkedCount > 0 ?
        Localization::Format(IDS_MIGRATE_MIGRATEBTN, checkedCount) :
        Localization::Lookup(IDS_MIGRATE_MIGRATE));
}

int CAppMigrateDlg::SelectedCount() const
{
    int count = 0;
    for (const bool selected : m_selected) count += selected ? 1 : 0;
    return count;
}

ULONGLONG CAppMigrateDlg::SelectedSize() const
{
    ULONGLONG size = 0;
    for (const int index : std::views::iota(0, static_cast<int>(m_selected.size())))
    {
        if (m_selected[static_cast<std::size_t>(index)])
        {
            size += m_detect.candidates[static_cast<std::size_t>(index)].size;
        }
    }
    return size;
}

void CAppMigrateDlg::StartDetect()
{
    if (m_worker.joinable()) m_worker.join();

    m_detectActive = true;
    m_detected = false;
    m_list.DeleteAllItems();
    m_selected.clear();
    SetText(IDC_APPMIGRATE_STATUS, Localization::Lookup(IDS_MIGRATE_DETECT));
    UpdateControls();

    m_worker = std::jthread([this] { AppMigrate::RunDetect(m_preset, m_detect); });
}

void CAppMigrateDlg::FinishDetect()
{
    m_detected = true;
    PopulateList();

    SetText(IDC_APPMIGRATE_STATUS, m_detect.candidates.empty() ?
        Localization::Lookup(IDS_MIGRATE_NONE) :
        Localization::Format(IDS_MIGRATE_DETECTDONE,
            static_cast<int>(m_detect.candidates.size()), FormatBytes(SelectedSize())));
    UpdateControls();
}

void CAppMigrateDlg::OnPickDest()
{
    const std::optional<std::wstring> dest = CDialog::PickFolder();
    if (!dest) return;
    m_dest = *dest;
    while (m_dest.ends_with(L'\\') && m_dest.size() > 3) m_dest.pop_back();
    SetText(IDC_APPMIGRATE_DESTPATH, m_dest);
    UpdateControls();
}

void CAppMigrateDlg::OnMigrate()
{
    if (m_detectActive || m_migrateActive || !m_detected) return;
    if (m_dest.empty())
    {
        SetText(IDC_APPMIGRATE_STATUS, Localization::Lookup(IDS_MIGRATE_NEEDDEST));
        return;
    }

    const int count = SelectedCount();
    if (count <= 0) return;

    const std::wstring message = Localization::Format(IDS_MIGRATE_CONFIRM,
        count, FormatBytes(SelectedSize()), m_dest);
    if (ShowMessageBox(message, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    if (m_worker.joinable()) m_worker.join();
    m_migrateActive = true;
    UpdateControls();

    m_worker = std::jthread([this]
    {
        std::vector<std::wstring> sources;
        std::vector<ULONGLONG> sizes;
        for (const int index : std::views::iota(0, static_cast<int>(m_selected.size())))
        {
            if (m_selected[static_cast<std::size_t>(index)])
            {
                const AppMigrate::Candidate& candidate =
                    m_detect.candidates[static_cast<std::size_t>(index)];
                sources.push_back(candidate.path);
                sizes.push_back(candidate.size);
            }
        }
        AppMigrate::RunMigration(m_preset, sources, sizes, m_dest, m_migrate);
    });
}

void CAppMigrateDlg::OnTimer(const UINT_PTR id)
{
    if (id != AppMigrateTimerId) return;

    if (m_detectActive)
    {
        if (m_detect.running.load(std::memory_order_acquire)) return;
        m_detectActive = false;
        FinishDetect();
        return;
    }

    if (m_migrateActive)
    {
        if (m_migrate.running.load(std::memory_order_acquire))
        {
            const int index = m_migrate.currentIndex.load(std::memory_order_relaxed);
            std::wstring leaf;
            if (index >= 0 && index < static_cast<int>(m_detect.candidates.size()))
            {
                leaf = AppMigrate::LeafName(m_detect.candidates[static_cast<std::size_t>(index)].path);
            }
            SetText(IDC_APPMIGRATE_STATUS, Localization::Format(IDS_MIGRATE_MOVING,
                leaf, FormatBytes(m_migrate.bytesMoved.load(std::memory_order_relaxed)),
                FormatBytes(m_migrate.bytesTotal.load(std::memory_order_relaxed))));
            return;
        }
        m_migrateActive = false;
        FinishMigrate();
    }
}

void CAppMigrateDlg::FinishMigrate()
{
    std::wstring summary = Localization::Format(IDS_MIGRATE_DONE,
        m_migrate.done.load(std::memory_order_relaxed),
        FormatBytes(m_migrate.bytesTotal.load(std::memory_order_relaxed)));
    if (const int locked = m_migrate.locked.load(std::memory_order_relaxed); locked > 0)
    {
        summary += L" " + Localization::Format(IDS_MIGRATE_LOCKED, locked);
    }
    if (const int failed = m_migrate.failed.load(std::memory_order_relaxed) +
        static_cast<int>(m_migrate.junctionFailed.size()); failed > 0)
    {
        summary += L" " + Localization::Format(IDS_MIGRATE_DONE_PARTIAL, failed);
    }
    SetText(IDC_APPMIGRATE_STATUS, summary);
    UpdateControls();
}
