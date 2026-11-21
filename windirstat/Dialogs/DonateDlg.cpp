// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "DonateDlg.h"

namespace
{
    // Decodes an embedded PNG resource into a GDI+ bitmap; the memory stream
    // is kept alive alongside the image for its documented lifetime
    std::unique_ptr<Gdiplus::Bitmap> LoadPngResource(const UINT id, CComPtr<IStream>& stream)
    {
        // The rc keyword RCDATA compiles to the integer type RT_RCDATA, so the
        // lookup must use that id and not the type-name string
        const HRSRC handle = FindResourceW(GetAppInstance(), MAKEINTRESOURCEW(id), RT_RCDATA);
        if (handle == nullptr) return nullptr;
        const HGLOBAL loaded = LoadResource(GetAppInstance(), handle);
        if (loaded == nullptr) return nullptr;
        const void* data = LockResource(loaded);
        const DWORD size = SizeofResource(GetAppInstance(), handle);
        if (data == nullptr || size == 0) return nullptr;

        stream.Attach(SHCreateMemStream(static_cast<const BYTE*>(data), size));
        if (!stream) return nullptr;
        return std::unique_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromStream(stream));
    }
}

// ---------------------------------------------------------------------------

void CDonateDlg::CDonateImage::DrawItem(LPDRAWITEMSTRUCT item)
{
    if (image == nullptr) return;

    const Gdiplus::Rect bounds(item->rcItem.left, item->rcItem.top,
        item->rcItem.right - item->rcItem.left, item->rcItem.bottom - item->rcItem.top);
    Gdiplus::Graphics graphics(item->hDC);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    // Match the dialog background in both color schemes
    const COLORREF back = DarkMode::SystemColor(COLOR_WINDOW);
    graphics.Clear(Gdiplus::Color(GetRValue(back), GetGValue(back), GetBValue(back)));

    // Scale uniformly and center inside the owner-draw rectangle
    const Gdiplus::REAL width = static_cast<Gdiplus::REAL>(image->GetWidth());
    const Gdiplus::REAL height = static_cast<Gdiplus::REAL>(image->GetHeight());
    const Gdiplus::REAL scale = std::min<Gdiplus::REAL>(bounds.Width / width, bounds.Height / height);
    const Gdiplus::REAL drawWidth = width * scale, drawHeight = height * scale;
    const Gdiplus::RectF placed(
        bounds.X + (bounds.Width - drawWidth) / 2.0f,
        bounds.Y + (bounds.Height - drawHeight) / 2.0f, drawWidth, drawHeight);
    graphics.DrawImage(image.get(), placed);
}

bool CDonateDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    Localization::UpdateDialogs(*this);
    DarkMode::AdjustControls(Handle());

    m_wx.image = LoadPngResource(IDR_DONATE_WX, m_wx.stream);
    m_ali.image = LoadPngResource(IDR_DONATE_ALI, m_ali.stream);
    m_wx.SubclassDlgItem(IDC_DONATE_WX, this);
    m_ali.SubclassDlgItem(IDC_DONATE_ALI, this);
    return true;
}
