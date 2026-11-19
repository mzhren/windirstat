// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"
#include "SmartClean.h"

//
// CSmartCleanDlg. Modal dialog that scans well-known junk categories on the
// system drive (temp files, system/browser caches, logs, dumps, error
// reports) and cleans the checked ones in place, mirroring the junk-clean
// flow of the LightC reference implementation. The scan and the clean each
// run on a worker thread; a timer polls the shared state for progress.
//
class CSmartCleanDlg final : public MessageTarget<CSmartCleanDlg, CDialog>
{
public:
    explicit CSmartCleanDlg(WindowRef parent = {}) : MessageTarget(IDD_SMARTCLEAN, parent) {}
    ~CSmartCleanDlg() override;

    static std::span<const RouteEntry> Routes();

protected:
    enum : std::uint8_t { IDD = IDD_SMARTCLEAN };

    bool OnInitDialog() override;
    void OnScan();
    void OnClean();
    void OnSelectAll();
    void OnTimer(const UINT_PTR id);

private:
    friend class CSmartCleanList;

    // Category list with per-row check boxes; reports toggle changes
    class CSmartCleanList final : public MessageTarget<CSmartCleanList, CListCtrl>
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

    CSmartCleanList m_list;
    SmartClean::ScanState m_scan;
    SmartClean::CleanState m_clean;
    std::vector<bool> m_selected;                 // Per-category check state
    std::jthread m_worker;                        // Active scan or clean job
    bool m_scanActive = false;                    // Worker thread is scanning
    bool m_cleanActive = false;                   // Worker thread is cleaning
    bool m_scanned = false;                       // Results are available
    bool m_populating = false;                    // Rows are being rebuilt
    std::array<int, std::to_underlying(SmartClean::Category::Count)> m_rows{};
};

inline std::span<const RouteEntry> CSmartCleanDlg::Routes()
{
    static constexpr std::array entries
    {
        Route::Command<&OnScan>(IDC_SMARTCLEAN_SCAN),
        Route::Command<&OnClean>(IDC_SMARTCLEAN_CLEAN),
        Route::Command<&OnSelectAll>(IDC_SMARTCLEAN_SELECTALL),
        Route::Window<&OnTimer>(WM_TIMER),
    };
    return entries;
}
