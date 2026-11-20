// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"
#include "AppMigrate.h"

//
// CAppMigrateDlg. Modal dialog shared by the WeChat and WPS migration toolbar
// buttons. It detects the preset's known folders on their default C: locations,
// shows their sizes, and moves the checked ones to a folder on another drive
// through the shell while leaving directory junctions at the original paths.
// Detection and migration run on worker threads polled by a dialog timer.
//
class CAppMigrateDlg final : public MessageTarget<CAppMigrateDlg, CDialog>
{
public:
    explicit CAppMigrateDlg(const AppMigrate::Preset preset, WindowRef parent = {}) :
        MessageTarget(IDD_APPMIGRATE, parent), m_preset(preset) {}
    ~CAppMigrateDlg() override;

    static std::span<const RouteEntry> Routes();

protected:
    enum : std::uint8_t { IDD = IDD_APPMIGRATE };

    bool OnInitDialog() override;
    void OnPickDest();
    void OnMigrate();
    void OnTimer(const UINT_PTR id);

private:
    friend class CAppMigrateList;

    // Candidate list with per-row check boxes; reports toggle changes
    class CAppMigrateList final : public MessageTarget<CAppMigrateList, CListCtrl>
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

    void StartDetect();
    void StartMigrate();
    void FinishDetect();
    void FinishMigrate();
    void PopulateList();
    void UpdateControls();
    void OnRowChecked(int row, bool checked);
    int SelectedCount() const;
    ULONGLONG SelectedSize() const;

    AppMigrate::Preset m_preset;
    CAppMigrateList m_list;
    AppMigrate::DetectState m_detect;
    AppMigrate::MigrateState m_migrate;
    std::vector<bool> m_selected;         // Per-candidate check state
    std::wstring m_dest;                  // Chosen destination folder
    std::jthread m_worker;                // Active detect or migrate job
    bool m_detectActive = false;
    bool m_migrateActive = false;
    bool m_detected = false;
    bool m_populating = false;
};

inline std::span<const RouteEntry> CAppMigrateDlg::Routes()
{
    static constexpr std::array entries
    {
        Route::Command<&OnPickDest>(IDC_APPMIGRATE_PICKDEST),
        Route::Command<&OnMigrate>(IDC_APPMIGRATE_MIGRATE),
        Route::Window<&OnTimer>(WM_TIMER),
    };
    return entries;
}
