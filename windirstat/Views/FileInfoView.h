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
// item: sizes, timestamps, location, attributes, and whether (and how) the
// item can be deleted.
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

    // One owner-drawn cleanup action at the bottom of the pane.
    struct ActionButton
    {
        CRect rect;
        std::wstring text;
        UINT command = 0;
        bool enabled = false;
    };

    void RefreshInfo();
    void DrawButton(CDC* pdc, const ActionButton& button, const bool hover, const bool pressed) const;
    std::wstring TypeDescription(const CItem* item) const;
    bool IsItemDeletable(const CItem* item) const;

    bool m_show = true;            // Whether this pane is shown (F8 option)
    CItem* m_item = nullptr;       // Item described by the pane (first selection)
    int m_selectionCount = 0;      // Number of currently selected items
    std::wstring m_title;          // Item name
    std::wstring m_subtitle;       // Type description
    std::vector<std::pair<std::wstring, std::wstring>> m_rows; // Label/value detail rows
    std::vector<std::wstring> m_notes;                         // Notes about deletion
    ActionButton m_btnRecycle;     // "Delete to Recycle Bin"
    ActionButton m_btnDelete;      // "Delete permanently"
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
