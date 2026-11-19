// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"
#include "DuplicateClean.h"

//
// CDuplicateCleanDlg. Modal dialog that finds duplicate files inside the
// current scan results (grouping by size and content hash through the model's
// own hashing) and lets the user remove redundant copies through the Recycle
// Bin. Each group always keeps at least one copy; the oldest one is kept by
// default. Scan and removal run on worker threads polled by a dialog timer.
//
class CDuplicateCleanDlg final : public MessageTarget<CDuplicateCleanDlg, CDialog>
{
public:
    explicit CDuplicateCleanDlg(WindowRef parent = {}) : MessageTarget(IDD_DUPLICATECLEAN, parent) {}
    ~CDuplicateCleanDlg() override;

    static std::span<const RouteEntry> Routes();

protected:
    enum : std::uint8_t { IDD = IDD_DUPLICATECLEAN };

    bool OnInitDialog() override;
    void OnScan();
    void OnClean();
    void OnSelectAll();
    void OnTimer(const UINT_PTR id);

private:
    friend class CDuplicateCleanList;

    // Grouped list with per-row check boxes; reports toggle changes
    class CDuplicateCleanList final : public MessageTarget<CDuplicateCleanList, CListCtrl>
    {
    public:
        std::function<void(int, bool)> onChanged;
        static std::span<const RouteEntry> Routes()
        {
            static constexpr std::array entries
            {
                Route::ReflectNotify<&OnItemChanged>(LVN_ITEMCHANGED),
            };
            return entries;
        }
        void OnItemChanged(NMHDR* header, LRESULT* result);
    };

    // One list row: a group header (file < 0) or a file entry
    struct RowRef
    {
        int group = -1;
        int file = -1;
    };

    void StartScan();
    void StartClean();
    void FinishScan();
    void FinishClean();
    void PopulateList();
    void UpdateControls();
    void OnRowChecked(int row, bool checked);
    int SelectedCount() const;
    ULONGLONG SelectedSize() const;

    CDuplicateCleanList m_list;
    DuplicateClean::ScanState m_scan;
    DuplicateClean::CleanState m_clean;
    std::vector<std::vector<bool>> m_selected;  // [group][file]
    std::vector<RowRef> m_rows;
    std::jthread m_worker;                      // Active scan or clean job
    bool m_scanActive = false;                  // Worker thread is scanning
    bool m_cleanActive = false;                 // Worker thread is removing
    bool m_scanned = false;                     // Results are available
    bool m_populating = false;                  // Rows are being rebuilt
};

inline std::span<const RouteEntry> CDuplicateCleanDlg::Routes()
{
    static constexpr std::array entries
    {
        Route::Command<&OnScan>(IDC_DUPCLEAN_SCAN),
        Route::Command<&OnClean>(IDC_DUPCLEAN_CLEAN),
        Route::Command<&OnSelectAll>(IDC_DUPCLEAN_SELECTALL),
        Route::Window<&OnTimer>(WM_TIMER),
    };
    return entries;
}
