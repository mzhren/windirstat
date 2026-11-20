// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"
#include "WinDirStatPane.h"

//
// CFileInfoView. The upper right view, which shows details of the selected
// item: sizes, timestamps, location, attributes, whether (and how) the item
// can be deleted, guidance for system files, and migration hints for known
// relocatable app data (WeChat, QQ, WPS).
//
class CFileInfoView final : public MessageTarget<CFileInfoView, CWinDirStatPane>
{
public:
    CFileInfoView() = default;
    ~CFileInfoView() override = default;

    bool IsPaneShown() const { return m_show; }
    void ShowPane(bool show);

    void OnDraw(CDC* pDC) override;

static std::span<const RouteEntry> Routes();

protected:
    int OnCreate(LPCREATESTRUCT lpCreateStruct);
    bool OnEraseBkgnd(CDC*) { return true; }
    void OnMouseMove(UINT nFlags, CPoint point);
    LRESULT OnMouseLeave(WPARAM, LPARAM);
    void OnLButtonDown(UINT nFlags, CPoint point);
    void OnLButtonUp(UINT nFlags, CPoint point);

    // Protection classification for items that must not be deleted directly.
    enum class Protection : std::uint8_t
    {
        None,          // Ordinary user data
        SystemFolder,  // Inside the Windows directory
        ComponentStore,// Windows\WinSxS component store
        InstalledProgram, // Under Program Files
        SystemDriver,  // Windows driver file
        Hiberfil,      // hiberfil.sys
        Pagefile,      // pagefile.sys / swapfile.sys
        RecycleBin,    // $Recycle.Bin storage
        RestorePoints  // System Volume Information
    };

    // One owner-drawn action at the bottom of the pane. Custom sentinel
    // command values dispatch to pane-local actions instead of WM_COMMAND.
    static constexpr UINT CMD_MIGRATE = 0xE0000001;
    static constexpr UINT CMD_SYSADVANCED = 0xE0000002;
    static constexpr UINT CMD_SYSPROTECT = 0xE0000003;

    struct ActionButton
    {
        CRect rect;
        std::wstring text;
        UINT command = 0;
        bool enabled = false;
        bool primary = false; // Rendered with an accent fill for prominence
    };

    void RefreshInfo();
    void DrawButton(CDC* pdc, const ActionButton& button, const bool hover, const bool pressed) const;
    std::wstring TypeDescription(const CItem* item) const;
    bool IsItemDeletable(const CItem* item) const;
    Protection ClassifyProtection(const CItem* item, DWORD attributes) const;
    std::wstring MigratableHint(const CItem* item) const;
    void OnMigrate();

    bool m_show = true;            // Whether this pane is shown (F8 option)
    CItem* m_item = nullptr;       // Item described by the pane (first selection)
    int m_selectionCount = 0;      // Number of currently selected items
    std::wstring m_title;          // Item name
    std::wstring m_subtitle;       // Type description
    std::vector<std::pair<std::wstring, std::wstring>> m_rows; // Label/value detail rows
    std::vector<std::wstring> m_notes;                         // Guidance notes about deletion
    std::vector<ActionButton> m_buttons;                       // Cleanup, guidance, and migration actions
    int m_deletableRowIndex = -1;  // Row index of the deletability verdict, -1 if absent
    int m_deletableVerdict = -1;   // 1 = deletable, 0 = not deletable, -1 = not shown
    CPoint m_mousePoint{ -1, -1 }; // Last mouse position in client coordinates
    bool m_mouseTracked = false;   // Whether WM_MOUSELEAVE is armed
    int m_pressedButton = -1;      // Index of the button held down, -1 if none
};

inline std::span<const RouteEntry> CFileInfoView::Routes()
{
    static constexpr std::array entries
    {
        Route::Window<&OnCreate>(WM_CREATE),
        Route::Window<&OnEraseBkgnd>(WM_ERASEBKGND),
        Route::Window<&OnMouseMove>(WM_MOUSEMOVE),
        Route::Window<&OnMouseLeave>(WM_MOUSELEAVE),
        Route::Window<&OnLButtonDown>(WM_LBUTTONDOWN),
        Route::Window<&OnLButtonUp>(WM_LBUTTONUP),
    };
    return entries;
}
