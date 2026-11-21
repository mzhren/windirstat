// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"
#include "JianYingClean.h"

//
// CJianYingCleanDlg. Modal dialog that scans the whitelisted JianYing
// (剪映/CapCut) cache, log, and temp directories, lists them with their
// sizes, and cleans the checked ones in place. A scan starts on open; the
// scan and the clean each run on a worker thread while a timer polls the
// shared state, mirroring the SmartClean dialog flow.
//
class CJianYingCleanDlg final : public MessageTarget<CJianYingCleanDlg, CDialog>
{
public:
    explicit CJianYingCleanDlg(WindowRef parent = {}) : MessageTarget(IDD_JIANYINGCLEAN, parent) {}
    ~CJianYingCleanDlg() override;

    static std::span<const RouteEntry> Routes();

protected:
    enum : std::uint8_t { IDD = IDD_JIANYINGCLEAN };

    bool OnInitDialog() override;
    void OnScan();
    void OnClean();
    void OnSelectAll();
    void OnTimer(const UINT_PTR id);

private:
    friend class CJianYingList;

    // Target list with per-row check boxes; reports toggle changes
    class CJianYingList final : public MessageTarget<CJianYingList, CListCtrl>
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

    void StartScan();
    void StartClean();
    void FinishScan();
    void FinishClean();
    void PopulateList();
    void UpdateControls();
    void OnRowChecked(int row, bool checked);
    int SelectedCount() const;
    ULONGLONG SelectedSize() const;

    CJianYingList m_list;
    JianYingClean::ScanState m_scan;
    JianYingClean::CleanState m_clean;
    std::vector<bool> m_selected;                 // Per-target check state
    std::vector<int> m_rows;                      // Row to scan result index
    std::jthread m_worker;                        // Active scan or clean job
    bool m_scanActive = false;                    // Worker thread is scanning
    bool m_cleanActive = false;                   // Worker thread is cleaning
    bool m_scanned = false;                       // Results are available
    bool m_populating = false;                    // Rows are being rebuilt
};

inline std::span<const RouteEntry> CJianYingCleanDlg::Routes()
{
    static constexpr std::array entries
    {
        Route::Command<&OnScan>(IDC_JIANYING_SCAN),
        Route::Command<&OnClean>(IDC_JIANYING_CLEAN),
        Route::Command<&OnSelectAll>(IDC_JIANYING_SELECTALL),
        Route::Window<&OnTimer>(WM_TIMER),
    };
    return entries;
}
