// 盘寻 - diskseek.space
// Copyright © diskseek.space / WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"

//
// CDonateDlg. Modal dialog showing the WeChat Pay and Alipay collection QR
// codes. The codes are embedded as PNG resources and painted through two
// owner-draw statics, scaled uniformly to fit at any DPI.
//
class CDonateDlg final : public MessageTarget<CDonateDlg, CDialog>
{
public:
    explicit CDonateDlg(WindowRef parent = {}) : MessageTarget(IDD_DONATE, parent) {}
    ~CDonateDlg() override = default;

    static std::span<const RouteEntry> Routes()
    {
        // The dialog template handles everything; there is nothing to route
        static constexpr std::array<RouteEntry, 0> entries {};
        return entries;
    }

protected:
    enum : std::uint8_t { IDD = IDD_DONATE };

    bool OnInitDialog() override;

private:
    // Owner-draw static painting one embedded QR code
    class CDonateImage final : public CStatic
    {
    public:
        CComPtr<IStream> stream;     // Kept alive for the image lifetime
        std::unique_ptr<Gdiplus::Bitmap> image;
        void DrawItem(LPDRAWITEMSTRUCT item) override;
    };

    CDonateImage m_wx;
    CDonateImage m_ali;
};
