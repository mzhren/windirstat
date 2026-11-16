// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "FileInfoView.h"

namespace
{
    constexpr COLORREF BlendColors(const COLORREF a, const COLORREF b, const double t)
    {
        const auto mix = [t](const COLORREF x, const COLORREF y, const int shift)
        {
            return static_cast<int>((x >> shift & 0xFF) * (1 - t) + (y >> shift & 0xFF) * t);
        };
        return RGB(mix(a, b, 0), mix(a, b, 8), mix(a, b, 16));
    }
}

/////////////////////////////////////////////////////////////////////////////

void CFileInfoView::ShowPane(const bool show)
{
    m_show = show;
    Invalidate();
}

std::wstring CFileInfoView::TypeDescription(const CItem* item) const
{
    std::wstring suffix;
    if (item->IsTypeOrFlag(ITRP_SYMLINK)) suffix = Localization::Lookup(IDS_INFO_TYPE_SYMLINK);
    else if (item->IsTypeOrFlag(ITRP_JUNCTION)) suffix = Localization::Lookup(IDS_INFO_TYPE_JUNCTION);
    else if (item->IsTypeOrFlag(ITRP_MOUNT)) suffix = Localization::Lookup(IDS_INFO_TYPE_MOUNT);
    else if (item->IsTypeOrFlag(ITRP_CLOUD)) suffix = Localization::Lookup(IDS_INFO_TYPE_CLOUD);

    std::wstring type;
    if (item->IsTypeOrFlag(IT_MYCOMPUTER)) type = Localization::Lookup(IDS_INFO_TYPE_MYCOMPUTER);
    else if (item->IsTypeOrFlag(IT_DRIVE)) type = Localization::Lookup(IDS_INFO_TYPE_DRIVE);
    else if (item->IsTypeOrFlag(IT_DIRECTORY)) type = Localization::Lookup(IDS_INFO_TYPE_FOLDER);
    else if (item->IsTypeOrFlag(IT_FILE))
    {
        type = Localization::Lookup(IDS_INFO_TYPE_FILE);
        if (const std::wstring ext = item->GetExtension(); !ext.empty()) type += L" (" + ext + L")";
    }
    else if (item->IsTypeOrFlag(IT_FREESPACE)) type = Localization::Lookup(IDS_INFO_TYPE_FREESPACE);
    else if (item->IsTypeOrFlag(IT_UNKNOWN)) type = Localization::Lookup(IDS_INFO_TYPE_UNKNOWN);
    else if (item->IsTypeOrFlag(IT_HLINKS, IT_HLINKS_SET, IT_HLINKS_IDX, IT_HLINKS_FILE))
    {
        type = Localization::Lookup(IDS_INFO_TYPE_HARDLINKS);
    }
    else return suffix;

    return suffix.empty() ? type : type + L" (" + suffix + L")";
}

bool CFileInfoView::IsItemDeletable(const CItem* item) const
{
    if (item == nullptr) return false;
    // Mirror the selection rules of the cleanup delete commands (ID_CLEANUP_DELETE*).
    if (item->IsTypeOrFlag(ITF_REMOTE)) return !item->IsScanRoot();
    return !item->IsRootItem() && !item->IsMtpRoot() && !item->IsWslRoot() &&
        item->HasShellIdentity() && item->IsTypeOrFlag(IT_DIRECTORY, IT_FILE);
}

void CFileInfoView::RefreshInfo()
{
    m_item = nullptr;
    m_selectionCount = 0;
    m_title.clear();
    m_subtitle.clear();
    m_rows.clear();
    m_notes.clear();
    m_btnRecycle = {};
    m_btnDelete = {};

    const auto model = CWinDirStatModel::Get();
    if (!model->HasRootItem()) return;

    const auto items = model->GetAllSelected();
    m_selectionCount = static_cast<int>(items.size());
    if (items.empty()) return;

    m_item = items.front();
    m_title = m_item->GetName();
    m_subtitle = TypeDescription(m_item);

    // Path and, for files, the containing folder
    m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_PATH), m_item->GetPath());
    if (m_item->IsTypeOrFlag(IT_FILE))
    {
        m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_DIRECTORY), m_item->GetFolderPath());
    }

    // Sizes and proportion, mirroring the file tree columns
    if (m_item->HasSizePhysical())
    {
        m_rows.emplace_back(Localization::Lookup(IDS_COL_SIZE_PHYSICAL), FormatBytes(m_item->GetSizePhysical()));
    }
    m_rows.emplace_back(Localization::Lookup(IDS_COL_SIZE_LOGICAL), FormatBytes(m_item->GetSizeLogical()));
    m_rows.emplace_back(Localization::Lookup(IDS_COL_PERCENTAGE),
        FormatDouble((COptions::UseAbsolutePercentages ? m_item->GetAbsoluteFraction() : m_item->GetFraction()) * 100) + L"%");

    // Contents of containers
    if (!m_item->IsTypeOrFlag(IT_FILE, IT_FREESPACE, IT_UNKNOWN, IT_HLINKS, IT_HLINKS_SET, IT_HLINKS_IDX))
    {
        m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_CONTENTS), std::format(L"{} {}, {} {}, {} {}",
            Localization::Lookup(IDS_COL_ITEMS), FormatCount(m_item->GetItemsCount()),
            Localization::Lookup(IDS_COL_FILES), FormatCount(m_item->GetFilesCount()),
            Localization::Lookup(IDS_COL_FOLDERS), FormatCount(m_item->GetFoldersCount())));
    }

    // Owner
    if (m_item->IsTypeOrFlag(IT_FILE, IT_DIRECTORY))
    {
        m_rows.emplace_back(Localization::Lookup(IDS_COL_OWNER), m_item->GetOwner());
    }

    // Modification time comes from the model; creation and access times are
    // fetched from disk, which also refreshes the current attributes
    if (!m_item->IsTypeOrFlag(IT_FREESPACE, IT_UNKNOWN, IT_HLINKS, IT_HLINKS_SET, IT_HLINKS_IDX))
    {
        m_rows.emplace_back(Localization::Lookup(IDS_COL_LAST_CHANGE),
            FormatFileTime(m_item->GetLastChange(), true));
    }

    DWORD attributes = m_item->GetAttributes();
    if (m_item->SupportsFilesystemApis() && model->IsScanSettled())
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(m_item->GetPathLong().c_str(), GetFileExInfoStandard, &data))
        {
            m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_CREATED),
                FormatFileTime(data.ftCreationTime, true));
            m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_ACCESSED),
                FormatFileTime(data.ftLastAccessTime, true));
            attributes = data.dwFileAttributes;
        }
    }
    m_rows.emplace_back(Localization::Lookup(IDS_COL_ATTRIBUTES), FormatAttributes(attributes));

    // Cleanup actions and their availability
    m_btnRecycle.text = Localization::Lookup(IDS_INFO_BTN_RECYCLEBIN);
    m_btnRecycle.command = ID_CLEANUP_DELETE_BIN;
    m_btnDelete.text = Localization::Lookup(IDS_INFO_BTN_PERMANENT);
    m_btnDelete.command = ID_CLEANUP_DELETE;

    if (!IsItemDeletable(m_item))
    {
        const std::wstring reason = m_item->IsTypeOrFlag(ITF_REMOTE) ?
            Localization::Lookup(IDS_INFO_DELETABLE_NO_REMOTE_ROOT) :
            !m_item->IsTypeOrFlag(IT_DIRECTORY, IT_FILE) ?
            Localization::Lookup(IDS_INFO_DELETABLE_NO_VIRTUAL) :
            Localization::Lookup(IDS_INFO_DELETABLE_NO_PROTECTED);
        m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_DELETABLE), reason);
        return;
    }

    const bool recycleBinOk = m_item->SupportsFilesystemApis() && !m_item->IsRootItem() &&
        IsLocalDrive(m_item->GetPath());
    m_rows.emplace_back(Localization::Lookup(IDS_INFO_LABEL_DELETABLE),
        Localization::Lookup(IDS_INFO_DELETABLE_YES));
    m_notes.emplace_back(recycleBinOk ?
        Localization::Lookup(IDS_INFO_RECYCLE_YES) : Localization::Lookup(IDS_INFO_RECYCLE_NO));
    if (attributes & FILE_ATTRIBUTE_READONLY)
    {
        m_notes.emplace_back(Localization::Lookup(IDS_INFO_READONLY));
    }

    m_btnRecycle.enabled = recycleBinOk;
    m_btnDelete.enabled = true;
}

void CFileInfoView::OnDraw(CDC* pdc)
{
    RefreshInfo();

    CRect rcClient = GetClientRect();
    const COLORREF windowColor = DarkMode::Color(DarkMode::ColorRole::Window);
    const COLORREF textColor = DarkMode::Color(DarkMode::ColorRole::WindowText);
    const COLORREF labelColor = DarkMode::Color(DarkMode::ColorRole::DisabledText);
    const COLORREF frameColor = DarkMode::Color(DarkMode::ColorRole::Frame);

    pdc->FillSolidRect(rcClient, windowColor);

    const ScopedBkMode bkMode(pdc, TRANSPARENT);
    const HFONT font = GetAppFont(m_hWnd);
    const CFont titleFont(font, FW_SEMIBOLD);
    const GdiObjectSelection fontSelection(pdc, font);

    constexpr int margin = 8;
    const int pad = ScaleForDpi(margin);
    const int gap = ScaleForDpi(2);
    const int lineGap = ScaleForDpi(4);
    const int rowHeight = ScaleForDpi(18);
    const int buttonHeight = ScaleForDpi(26);
    const int buttonGap = ScaleForDpi(8);
    const int iconTextGap = ScaleForDpi(6);

    int y = rcClient.top + pad;

    if (m_item == nullptr)
    {
        // Nothing is selected: explain what this pane shows.
        pdc->SetTextColor(labelColor);
        CRect rcText(rcClient.left + pad, rcClient.Center().y - rowHeight,
            rcClient.right - pad, rcClient.Center().y + rowHeight * 2);
        pdc->DrawText(Localization::Lookup(IDS_INFO_SELECT_HINT), rcText,
            DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        return;
    }

    // Title: icon plus name
    {
        const GdiObjectSelection titleSelection(pdc, titleFont);
        pdc->SetTextColor(textColor);
        int textLeft = rcClient.left + pad;
        if (const HICON icon = m_item->GetIcon(); icon != nullptr)
        {
            const int iconSize = GetSystemMetrics(SM_CXSMICON);
            ::DrawIconEx(pdc->Handle(), textLeft, y + (rowHeight - iconSize) / 2,
                icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            textLeft += iconSize + iconTextGap;
        }
        CRect rcTitle(textLeft, y, rcClient.right - pad, y + rowHeight);
        pdc->DrawText(m_title, rcTitle, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    }
    y += rowHeight + gap;

    // Type subtitle and multiple-selection notice
    pdc->SetTextColor(labelColor);
    CRect rcSubtitle(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
    pdc->DrawText(m_subtitle, rcSubtitle, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    y += rowHeight + gap;

    if (m_selectionCount > 1)
    {
        CRect rcMulti(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
        pdc->DrawText(Localization::Format(IDS_INFO_MULTI, FormatCount(m_selectionCount)),
            rcMulti, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
        y += rowHeight + gap;
    }

    // Reserve the bottom strip for the cleanup actions
    const int notesHeight = static_cast<int>(m_notes.size()) * (rowHeight + gap);
    const int bottomStrip = lineGap + rowHeight + gap + notesHeight + buttonHeight + gap + rowHeight + pad;
    const int rowsBottom = rcClient.bottom - bottomStrip;

    // Separator above the detail rows
    if (y < rowsBottom)
    {
        pdc->FillSolidRect(rcClient.left + pad, y, rcClient.Width() - pad * 2, 1, frameColor);
        y += lineGap;
    }

    // Detail rows: label column on the left, values on the right
    int labelWidth = 0;
    for (const auto& [label, value] : m_rows)
    {
        labelWidth = std::max(labelWidth, static_cast<int>(pdc->GetTextExtent(label).cx));
    }
    labelWidth += ScaleForDpi(10);

    for (const auto& [label, value] : m_rows)
    {
        if (y + rowHeight > rowsBottom) break;
        pdc->SetTextColor(labelColor);
        CRect rcLabel(rcClient.left + pad, y, rcClient.left + pad + labelWidth, y + rowHeight);
        pdc->DrawText(label, rcLabel, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);

        pdc->SetTextColor(textColor);
        CRect rcValue(rcLabel.right, y, rcClient.right - pad, y + rowHeight);
        pdc->DrawText(value, rcValue, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
        y += rowHeight + gap;
    }

    // Cleanup section
    y = rcClient.bottom - bottomStrip;
    pdc->FillSolidRect(rcClient.left + pad, y, rcClient.Width() - pad * 2, 1, frameColor);
    y += lineGap;

    pdc->SetTextColor(labelColor);
    CRect rcHowTo(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
    pdc->DrawText(Localization::Lookup(IDS_INFO_LABEL_HOWTO), rcHowTo,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    y += rowHeight + gap;

    for (const std::wstring& note : m_notes)
    {
        if (y + rowHeight > rcClient.bottom - pad) break;
        CRect rcNote(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
        pdc->DrawText(note, rcNote, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
        y += rowHeight + gap;
    }

    // Action buttons, left-aligned on one line
    int x = rcClient.left + pad;
    for (auto* button : { &m_btnRecycle, &m_btnDelete })
    {
        const int width = pdc->GetTextExtent(button->text).cx + ScaleForDpi(24);
        button->rect = CRect(x, y, std::min<int>(x + width, rcClient.right - pad), y + buttonHeight);
        const bool hover = m_mouseTracked && button->rect.Contains(m_mousePoint);
        DrawButton(pdc, *button, hover, m_pressedButton == (button == &m_btnRecycle ? 0 : 1));
        x = button->rect.right + buttonGap;
    }
    y += buttonHeight + gap;

    // Keyboard shortcuts hint
    if (y + rowHeight < rcClient.bottom)
    {
        pdc->SetTextColor(labelColor);
        CRect rcHint(rcClient.left + pad, y, rcClient.right - pad, y + rowHeight);
        pdc->DrawText(Localization::Lookup(IDS_INFO_KEYS_HINT), rcHint,
            DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    }
}

void CFileInfoView::DrawButton(CDC* pdc, const ActionButton& button,
    const bool hover, const bool pressed) const
{
    const COLORREF controlColor = DarkMode::Color(DarkMode::ColorRole::Control);
    const COLORREF windowColor = DarkMode::Color(DarkMode::ColorRole::Window);
    const COLORREF frameColor = DarkMode::Color(DarkMode::ColorRole::Frame);
    const COLORREF textColor = DarkMode::Color(DarkMode::ColorRole::WindowText);
    const COLORREF disabledColor = DarkMode::Color(DarkMode::ColorRole::DisabledText);
    const COLORREF accentColor = DarkMode::Color(DarkMode::ColorRole::Selection);

    const COLORREF fill = !button.enabled ? windowColor :
        pressed ? BlendColors(controlColor, accentColor, 0.55) :
        hover ? BlendColors(controlColor, accentColor, 0.2) : controlColor;
    const COLORREF border = !button.enabled ? frameColor : hover || pressed ? accentColor : frameColor;
    const COLORREF text = button.enabled ? textColor : disabledColor;

    pdc->FillSolidRect(button.rect, fill);
    pdc->FillSolidRect(button.rect.left, button.rect.top, button.rect.Width(), 1, border);
    pdc->FillSolidRect(button.rect.left, button.rect.bottom - 1, button.rect.Width(), 1, border);
    pdc->FillSolidRect(button.rect.left, button.rect.top, 1, button.rect.Height(), border);
    pdc->FillSolidRect(button.rect.right - 1, button.rect.top, 1, button.rect.Height(), border);

    pdc->SetTextColor(text);
    CRect rcText = button.rect;
    rcText.Deflate(ScaleForDpi(8), 0);
    pdc->DrawText(button.text, rcText,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}

int CFileInfoView::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    return CWinDirStatPane::OnCreate(lpCreateStruct);
}

void CFileInfoView::OnMouseMove(const UINT /*nFlags*/, const CPoint point)
{
    m_mousePoint = point;

    if (!m_mouseTracked)
    {
        TRACKMOUSEEVENT track{ .cbSize = sizeof(track), .dwFlags = TME_LEAVE, .hwndTrack = m_hWnd };
        if (TrackMouseEvent(&track)) m_mouseTracked = true;
    }

    // Repaint while the pointer moves across the action buttons
    for (const ActionButton* button : { &m_btnRecycle, &m_btnDelete })
    {
        if (button->enabled && button->rect.Contains(point))
        {
            Invalidate(false);
            break;
        }
    }
}

LRESULT CFileInfoView::OnMouseLeave(const WPARAM, const LPARAM)
{
    m_mouseTracked = false;
    m_mousePoint = CPoint(-1, -1);
    m_pressedButton = -1;
    Invalidate(false);
    return 0;
}

void CFileInfoView::OnLButtonDown(const UINT /*nFlags*/, const CPoint point)
{
    m_mousePoint = point;
    for (const auto [index, button] : std::views::enumerate(std::array{ &m_btnRecycle, &m_btnDelete }))
    {
        if (button->enabled && button->rect.Contains(point))
        {
            m_pressedButton = static_cast<int>(index);
            SetCapture();
            Invalidate(false);
            return;
        }
    }
}

void CFileInfoView::OnLButtonUp(const UINT /*nFlags*/, const CPoint point)
{
    if (m_pressedButton < 0) return;

    ::ReleaseCapture();
    const ActionButton* pressed = (m_pressedButton == 0 ? &m_btnRecycle : &m_btnDelete);
    m_pressedButton = -1;
    Invalidate(false);

    if (pressed->enabled && pressed->rect.Contains(point))
    {
        GetMainWindow()->SendMessage(WM_COMMAND, pressed->command);
    }
}
