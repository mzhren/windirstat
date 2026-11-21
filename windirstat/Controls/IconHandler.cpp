// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"
#include "IconHandler.h"
#include "HelpersInterface.h"
#include "FinderMtp.h"

CIconHandler::~CIconHandler()
{
    for (auto* queue : { &m_fastQueue, &m_slowQueue })
    {
        // Interrupt any blocking SHGetFileInfo/ReadFile in workers so they return
        // quickly to their Pop() loop where they will see CancelExecution's flag.
        // Without this, the destructor can hang until the current icon fetch completes.
        queue->CancelThreadIo();
        queue->CancelExecution();
    }
}

void CIconHandler::Initialize()
{
    static std::once_flag s_once;
    std::call_once(s_once, [this]
    {
        m_junctionImage = Icons::IconFromFontChar(L'⤷', RGB(0x3A, 0x3A, 0xFF), true);
        m_symlinkImage = Icons::IconFromFontChar(L'⤷', RGB(0x3A, 0xFF, 0x3A), true);
        m_junctionProtected = Icons::IconFromFontChar(L'⤷', RGB(0xFF, 0x3A, 0x3A), true);
        m_freeSpaceImage = Icons::IconFromFontChar(L'▢', RGB(0x3A, 0xCC, 0x3A), true);
        m_emptyImage = Icons::IconFromFontChar(L'▢', DarkMode::SystemColor(COLOR_WINDOWTEXT));
        m_hardlinksImage = Icons::IconFromFontChar(L'⧉', DarkMode::SystemColor(COLOR_WINDOWTEXT));
        m_dupesImage = Icons::IconFromFontChar(L'⧈', DarkMode::SystemColor(COLOR_WINDOWTEXT));
        m_searchImage = Icons::IconFromFontChar(L'⊙', DarkMode::SystemColor(COLOR_WINDOWTEXT));
        m_largestImage = Icons::IconFromFontChar(L'⋙', DarkMode::SystemColor(COLOR_WINDOWTEXT));
        m_unknownImage = Icons::IconFromFontChar(L'?', RGB(0xCC,0xB8,0x66), true);
        m_scanErrorImage = LoadIconW(nullptr, IDI_WARNING);
        m_defaultFileImage = FetchShellIcon(GetSysDirectory() + L"\\~", 0, FILE_ATTRIBUTE_NORMAL);
        m_defaultFolderImage = FetchShellIcon(GetSysDirectory() + L"\\~", 0, FILE_ATTRIBUTE_DIRECTORY);

        // Cache icon for boot drive
        std::wstring drive(MAX_PATH, wds::chrNull);
        drive.resize(std::min<size_t>(wcslen(L"C:\\"), GetWindowsDirectory(drive.data(), MAX_PATH)));
        m_mountPointImage = FetchShellIcon(drive, 0, FILE_ATTRIBUTE_REPARSE_POINT);

        // Cache icon for my computer
        m_myComputerImage = FetchShellIcon(std::to_wstring(CSIDL_DRIVES), SHGFI_PIDL);

        for (auto* queue : { &m_fastQueue, &m_slowQueue }) queue->StartThreads(MAX_ICON_THREADS, [this, queue]
        {
            if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return;
            for (auto itemOpt = queue->Pop(); itemOpt.has_value(); itemOpt = queue->Pop())
            {
                auto& [lookup, requestId] = itemOpt.value();
                auto& [item, control, path, attr, icon, desc] = lookup;
                std::wstring descTmp;
                const HICON iconTmp = FetchShellIcon(path, 0, attr, desc != nullptr ? &descTmp : nullptr);

                // Join the UI thread and see if the item still exists
                // since it could have been deleted since originally
                // requested
                CMainFrame::Get()->InvokeInMessageThread([&]
                {
                    const auto pending = m_pendingLookups.find(item);
                    if (pending == m_pendingLookups.end() || pending->second != requestId) return;
                    m_pendingLookups.erase(pending);
                    const auto i = control->FindListItem(item);
                    if (i == -1 || !item->IsVisible()) return;
                    *icon = iconTmp;
                    if (desc != nullptr) *desc = descTmp;
                    control->RedrawItems(i, i);
                });
            }
            CoUninitialize();
        });
    });
}

void CIconHandler::DoAsyncShellInfoLookup(IconLookup&& lookupInfo)
{
    auto& [item, control, path, attr, icon, desc] = lookupInfo;

    if (m_pendingLookups.contains(item)) return;
    const std::uint64_t requestId = ++m_nextLookupId;
    m_pendingLookups.emplace(item, requestId);

    // set default icon while loading
    if (*icon == nullptr)
    {
        *icon = (attr & FILE_ATTRIBUTE_DIRECTORY) ? m_defaultFolderImage : m_defaultFileImage;
    }

    static constexpr std::array<std::wstring_view, 4> slowExts = { L".exe", L".ico", L".lnk", L".url" };
    const auto dot = path.rfind(L'.');
    const bool isSlow = dot != std::wstring::npos &&
        std::ranges::any_of(slowExts, [ext = path.c_str() + dot](const std::wstring_view e) { return _wcsicmp(ext, e.data()) == 0; });

    // FILE_ATTRIBUTE_OFFLINE indicates the file is not immediately available locally
    // (cloud placeholder, Remote Storage, etc.). SHGetFileInfo opens these file types
    // directly despite SHGFI_USEFILEATTRIBUTES, triggering a download.
    // Redirect to a synthetic extension-only path so the type icon is used instead.
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_OFFLINE) != 0)
    {
        path = GetSysDirectory() + L"\\~" + (dot == std::wstring::npos ? path : path.substr(dot));
        m_fastQueue.Push({ std::move(lookupInfo), requestId });
        return;
    }

    (isSlow ? m_slowQueue : m_fastQueue).Push({ std::move(lookupInfo), requestId });
}

void CIconHandler::ForgetAsyncShellInfoLookup(const CWdsListItem* item)
{
    m_pendingLookups.erase(item);
}

void CIconHandler::ClearAsyncShellInfoQueue()
{
    m_pendingLookups.clear();
    CWinApp::RunTaskWithUiUpdates([this]
    {
        for (auto* queue : { &m_fastQueue, &m_slowQueue })
        {
            queue->CancelThreadIo();
            queue->SuspendExecution(true);
            queue->ResumeExecution();
        }
    });
}

void CIconHandler::StopAsyncShellInfoQueue()
{
    m_pendingLookups.clear();
    CWinApp::RunTaskWithUiUpdates([this]
    {
        for (auto* queue : { &m_fastQueue, &m_slowQueue })
        {
            queue->CancelThreadIo();
            queue->SuspendExecution();
            queue->CancelExecution();
        }
    });
}

void CIconHandler::DrawIcon(CDC* hdc, HICON image, const CPoint & pt, const CSize& sz)
{
    // Items retain stable icon handles; palette-dependent glyphs are rendered at the current size.
    for (const auto& [handle, glyph] : { std::pair{ m_emptyImage, L'▢' }, { m_hardlinksImage, L'⧉' },
        { m_dupesImage, L'⧈' }, { m_searchImage, L'⊙' }, { m_largestImage, L'⋙' } })
    {
        if (image == nullptr || image != handle) continue;
        const COLORREF color = DarkMode::Color(DarkMode::ColorRole::WindowText);
        if (m_iconColor != color) m_themedIcons.clear();
        m_iconColor = color;
        const auto [entry, inserted] = m_themedIcons.try_emplace(std::pair{ glyph, std::max(sz.cx, sz.cy) }, DestroyIcon);
        if (inserted) entry->second = Icons::IconFromFontChar(glyph, color, false, entry->first.second);
        if (entry->second.IsValid()) image = entry->second;
        break;
    }
    DrawIconEx(*hdc, pt.x, pt.y, image, sz.cx, sz.cy, 0, nullptr, DI_NORMAL);
}

HICON CIconHandler::FetchShellIcon(const std::wstring & path, UINT flags, const DWORD attr, std::wstring* psTypeName)
{
    flags |= WDS_SHGFI_DEFAULTS;

    // Also retrieve the file type description
    if (psTypeName != nullptr && !path.empty()) flags |= SHGFI_TYPENAME;

    SHFILEINFO sfi{};
    bool success = false;

    // Resolve MTP virtual paths to PIDLs so the shell can return their native icons
    if (FinderMtp::IsPath(path))
    {
        const SmartPointer pidl(CoTaskMemFree, CreateShellPidl(path));
        flags = flags & ~SHGFI_USEFILEATTRIBUTES | SHGFI_PIDL;
        success = pidl != nullptr && std::bit_cast<HIMAGELIST>(::SHGetFileInfo(
            static_cast<LPCWSTR>(static_cast<LPVOID>(pidl)), 0, &sfi, sizeof(sfi), flags)) != nullptr;
    }
    else if (flags & SHGFI_PIDL)
    {
        // Assume folder id is numeric and encoded as a string
        CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl;
        if (SUCCEEDED(SHGetSpecialFolderLocation(nullptr, std::stoi(path), &pidl)))
        {
            success = std::bit_cast<HIMAGELIST>(::SHGetFileInfo(
                static_cast<LPCWSTR>(static_cast<LPVOID>(pidl)), attr, &sfi,
                sizeof(sfi), flags)) != nullptr;
        }
    }
    else
    {
        success = std::bit_cast<HIMAGELIST>(::SHGetFileInfo(path.c_str(),
            attr, &sfi, sizeof(sfi), flags)) != nullptr;
    }

    if (!success)
    {
        VTRACE(L"SHGetFileInfo() failed: {}", path);
        return GetEmptyImage();
    }

    if (psTypeName != nullptr)
    {
        *psTypeName = path.empty() ?
            Localization::Lookup(IDS_EXTENSION_MISSING) : sfi.szTypeName;
    }

    std::scoped_lock lock(m_cachedIconMutex);
    if (const auto it = m_cachedIcons.find(sfi.iIcon); it != m_cachedIcons.end())
    {
        if (sfi.hIcon != nullptr) DestroyIcon(sfi.hIcon);
        return it->second;
    }

    m_cachedIcons[sfi.iIcon] = sfi.hIcon;
    return sfi.hIcon;
}

namespace Icons
{
    using namespace Gdiplus;

    static void PaintDocument(Graphics& g)
    {
        const Pen outlinePen(Neutral(), 4);
        SolidBrush lineBrush(Neutral());
        const Point body[] = { {8, 6}, {44, 6}, {56, 18}, {56, 58}, {8, 58} };
        g.DrawPolygon(&outlinePen, body, std::size(body));
        const Point docFoldPts[] = { {44, 6}, {44, 18}, {56, 18} };
        g.DrawLines(&outlinePen, docFoldPts, std::size(docFoldPts));
    }

    static void PaintBin(Graphics& g, const Color body, const Color bar, const bool recycle = false)
    {
        const SolidBrush bodyBrush(body);
        const SolidBrush barBrush(bar);
        g.FillRectangle(&bodyBrush, 24, 6,  16, 6);
        g.FillRectangle(&bodyBrush,  6, 12, 52, 6);
        g.FillRectangle(&bodyBrush, 12, 18,  4, 40);
        g.FillRectangle(&bodyBrush, 48, 18,  4, 40);
        g.FillRectangle(&bodyBrush, 12, 54, 40, 4);
        if (recycle)
        {
            const GraphicsState state = g.Save();
            g.TranslateTransform(32.0f, 36.0f);
            g.ScaleTransform(0.5f, 0.5f);
            g.TranslateTransform(-32.0f, -32.0f);
            PaintCharacter(g, L'♻', RGB(bar.GetR(), bar.GetG(), bar.GetB()));
            g.Restore(state);
        }
        else
        {
            for (const int i : std::views::iota(0, 3))
                g.FillRectangle(&barBrush, 23 + i * 8, 26, 3, 24);
        }
    }

    void PaintDelete(Graphics& g)    { PaintBin(g, C(204, 0, 0), C(204, 40, 40)); }
    void PaintDeleteBin(Graphics& g) { PaintBin(g, Neutral(), Neutral(), true); }

    void PaintExplorerSelect(Graphics& g)
    {
        PaintDocument(g);
        const SolidBrush cursorBrush(C(0, 102, 204));
        const Point cursor[] = {
            {21, 14}, {21, 44}, {29, 36},
            {37, 48}, {43, 44}, {35, 32}, {45, 32}
        };
        g.FillPolygon(&cursorBrush, cursor, static_cast<INT>(std::size(cursor)));
    }

    void PaintOpenInConsole(Graphics& g)
    {
        const SolidBrush grayBrush(Neutral());
        const Pen framePen(Neutral(), 4);
        Pen chevronPen(Neutral(), 4);
        chevronPen.SetStartCap(LineCapRound);
        chevronPen.SetEndCap(LineCapRound);
        chevronPen.SetLineJoin(LineJoinRound);
        g.DrawRectangle(&framePen, 6, 8, 52, 48);
        g.FillRectangle(&grayBrush, 6, 17, 52, 2);
        const Point chevronPts[] = { {14, 26}, {22, 34}, {14, 42} };
        g.DrawLines(&chevronPen, chevronPts, static_cast<INT>(std::size(chevronPts)));
        g.DrawLine(&chevronPen, 26, 42, 38, 42);
    }

    void PaintOpenSelected(Graphics& g)
    {
        PaintDocument(g);
        const SolidBrush greenBrush(C(40, 140, 50));
        const Point triangle[] = { {20, 18}, {44, 32}, {20, 46} };
        g.FillPolygon(&greenBrush, triangle, static_cast<INT>(std::size(triangle)));
    }

    void PaintRefreshSelected(Graphics& g)
    {
        PaintDocument(g);
        const GraphicsState state = g.Save();
        g.TranslateTransform(31.0f, 35.0f);
        g.ScaleTransform(0.65f, 0.65f);
        g.TranslateTransform(-32.0f, -32.0f);
        PaintCharacter(g, L'↻', RGB(0, 156, 221));
        g.Restore(state);
    }

    void PaintProperties(Graphics& g)
    {
        PaintDocument(g);
        const SolidBrush blueBrush(C(0, 102, 204));
        g.FillEllipse(&blueBrush, 26, 15, 8, 8);
        g.FillRectangle(&blueBrush, 27, 27, 6, 23);
    }

    void PaintEditCopyClipboard(Graphics& g)
    {
        const SolidBrush goldBrush(C(200, 140, 30)), goldLightBrush(C(230, 180, 80)),
            clipBrush(C(120, 90, 20)), grayBrush(C(180, 180, 180));
        const Pen outlinePen(Neutral(), 3);
        g.FillRectangle(&goldBrush, 4, 10, 36, 50);
        g.FillRectangle(&clipBrush, 12, 2, 20, 10);
        g.FillRectangle(&goldLightBrush, 16, 5, 12, 4);
        const SolidBrush whiteBrush(C(255, 255, 255));
        g.FillRectangle(&whiteBrush, 28, 24, 32, 36);
        g.DrawRectangle(&outlinePen, 28, 24, 32, 36);
        for (const int i : std::views::iota(0, 3))
            g.FillRectangle(&grayBrush, 33, 32 + i * 7, 22, 3);
    }

    void PaintFileSelect(Graphics& g)
    {
        const Pen outlinePen(C(238, 203, 74), 4);
        const Point folderShapePts[] = { {2, 4}, {28, 4}, {32, 12}, {62, 12}, {62, 58}, {2, 58} };
        const SolidBrush fillBrush(C(255, 231, 146));
        const SolidBrush ringRed(C(200, 30, 30)), ringWhite(C(240, 240, 240)), ringRed2(C(200, 30, 30));
        g.FillPolygon(&fillBrush, folderShapePts, static_cast<INT>(std::size(folderShapePts)));
        g.DrawPolygon(&outlinePen, folderShapePts, static_cast<INT>(std::size(folderShapePts)));
        g.FillEllipse(&ringRed,   14, 17, 36, 36);
        g.FillEllipse(&ringWhite, 21, 24, 22, 22);
        g.FillEllipse(&ringRed2,  27, 30, 10, 10);
    }

    void PaintFolderAppend(Graphics& g)
    {
        PaintFileSelect(g);
        const SolidBrush badgeBrush(C(40, 140, 50)), plusBrush(C(255, 255, 255));
        g.FillEllipse(&badgeBrush, 36, 36, 28, 28);
        g.FillRectangle(&plusBrush, 41, 47, 18, 6);
        g.FillRectangle(&plusBrush, 47, 41, 6, 18);
    }

    void PaintSmartClean(Graphics& g)
    {
        // Broom handle
        Pen handlePen(Neutral(), 6);
        handlePen.SetStartCap(LineCapRound);
        handlePen.SetEndCap(LineCapRound);
        g.DrawLine(&handlePen, 46, 6, 26, 34);

        // Bristle head flaring toward the bottom
        const SolidBrush headBrush(C(0, 156, 221));
        const Point head[] = { {17, 32}, {35, 32}, {43, 56}, {9, 56} };
        g.FillPolygon(&headBrush, head, static_cast<INT>(std::size(head)));
        Pen bristlePen(C(255, 255, 255), 3);
        g.DrawLine(&bristlePen, 21, 38, 18, 53);
        g.DrawLine(&bristlePen, 26, 38, 26, 53);
        g.DrawLine(&bristlePen, 31, 38, 34, 53);

        // Sparkles
        Pen sparklePen(C(255, 190, 60), 3);
        sparklePen.SetStartCap(LineCapRound);
        sparklePen.SetEndCap(LineCapRound);
        g.DrawLine(&sparklePen, 10, 6, 10, 18);
        g.DrawLine(&sparklePen, 4, 12, 16, 12);
        g.DrawLine(&sparklePen, 52, 42, 52, 50);
        g.DrawLine(&sparklePen, 48, 46, 56, 46);
    }

    void PaintDuplicateClean(Graphics& g)
    {
        // Back sheet with its corner fold
        const Pen backPen(Neutral(), 4);
        const Point back[] = { {6, 2}, {30, 2}, {40, 12}, {40, 24}, {6, 24} };
        g.DrawPolygon(&backPen, back, static_cast<INT>(std::size(back)));
        const Point backFold[] = { {30, 2}, {30, 12}, {40, 12} };
        g.DrawLines(&backPen, backFold, static_cast<INT>(std::size(backFold)));

        // Front sheet occluding the back one
        const SolidBrush frontBrush(C(255, 255, 255));
        const Pen frontPen(C(0, 102, 204), 4);
        const Point front[] = { {22, 18}, {46, 18}, {58, 30}, {58, 60}, {22, 60} };
        g.FillPolygon(&frontBrush, front, static_cast<INT>(std::size(front)));
        g.DrawPolygon(&frontPen, front, static_cast<INT>(std::size(front)));
        const Point frontFold[] = { {46, 18}, {46, 30}, {58, 30} };
        g.DrawLines(&frontPen, frontFold, static_cast<INT>(std::size(frontFold)));

        // Content lines
        const SolidBrush lineBrush(C(0, 102, 204));
        g.FillRectangle(&lineBrush, 28, 40, 24, 3);
        g.FillRectangle(&lineBrush, 28, 48, 24, 3);
    }

    void PaintMigrateWeChat(Graphics& g)
    {
        // Speech bubble with a tail
        const SolidBrush bubble(C(96, 174, 76));
        g.FillEllipse(&bubble, 4, 4, 56, 42);
        const Point tail[] = { {16, 36}, {26, 58}, {32, 40} };
        g.FillPolygon(&bubble, tail, static_cast<INT>(std::size(tail)));

        // Chat dots
        const SolidBrush dots(C(255, 255, 255));
        g.FillEllipse(&dots, 17, 18, 9, 12);
        g.FillEllipse(&dots, 34, 18, 9, 12);
        g.FillEllipse(&dots, 25, 32, 9, 12);

        // Move arrow accent
        const SolidBrush arrow(C(0, 130, 220));
        const Point arrowShape[] = { {44, 46}, {60, 46}, {60, 40}, {44, 40} };
        g.FillPolygon(&arrow, arrowShape, static_cast<INT>(std::size(arrowShape)));
    }

    void PaintMigrateWps(Graphics& g)
    {
        // Cloud silhouette
        const SolidBrush cloud(C(0, 130, 220));
        g.FillEllipse(&cloud, 4, 26, 30, 24);
        g.FillEllipse(&cloud, 18, 12, 32, 28);
        g.FillEllipse(&cloud, 36, 24, 24, 24);
        g.FillRectangle(&cloud, 17, 36, 30, 12);

        // Small document being moved onto the cloud
        const SolidBrush paper(C(255, 255, 255));
        const Pen outline(Neutral(), 3);
        const Point sheet[] = { {30, 30}, {48, 30}, {54, 36}, {54, 56}, {30, 56} };
        g.FillPolygon(&paper, sheet, static_cast<INT>(std::size(sheet)));
        g.DrawPolygon(&outline, sheet, static_cast<INT>(std::size(sheet)));
        const SolidBrush lines(C(0, 102, 204));
        g.FillRectangle(&lines, 34, 42, 16, 3);
        g.FillRectangle(&lines, 34, 48, 16, 3);
    }

    void PaintJianYingClean(Graphics& g)
    {
        // Video-editor play tile
        const SolidBrush tile(C(0, 152, 220));
        g.FillEllipse(&tile, 4, 10, 50, 50);
        const SolidBrush play(C(255, 255, 255));
        const Point playShape[] = { {21, 24}, {21, 46}, {39, 35} };
        g.FillPolygon(&play, playShape, static_cast<INT>(std::size(playShape)));

        // Cleanup sparkle accent
        Pen sparklePen(C(255, 190, 60), 4);
        sparklePen.SetStartCap(LineCapRound);
        sparklePen.SetEndCap(LineCapRound);
        g.DrawLine(&sparklePen, 52, 6, 52, 18);
        g.DrawLine(&sparklePen, 46, 12, 58, 12);
        g.DrawLine(&sparklePen, 44, 40, 44, 48);
        g.DrawLine(&sparklePen, 40, 44, 48, 44);
    }

    void PaintDonate(Graphics& g)
    {
        // Rounded heart: one closed cubic-bezier outline, no sharp corners
        GraphicsPath heart;
        heart.AddBezier(32, 55, 15, 43, 5, 33, 5, 22);
        heart.AddBezier(5, 22, 5, 11, 13, 7, 19, 7);
        heart.AddBezier(19, 7, 25, 7, 30, 10, 32, 15);
        heart.AddBezier(32, 15, 34, 10, 39, 7, 45, 7);
        heart.AddBezier(45, 7, 51, 7, 59, 11, 59, 22);
        heart.AddBezier(59, 22, 59, 33, 49, 43, 32, 55);
        heart.CloseFigure();

        const SolidBrush fill(C(232, 62, 88));
        g.FillPath(&fill, &heart);

        // Soft rounded highlight on the left lobe
        const SolidBrush shine(C(255, 255, 255, 90));
        g.FillEllipse(&shine, 14, 13, 9, 6);
    }

    void PaintFilter(Graphics& g, const bool active)
    {
        const Point funnelShape[] = { {8, 6}, {56, 6}, {56, 14}, {38, 34}, {38, 56}, {26, 52}, {26, 34}, {8, 14} };
        const SolidBrush fillBrush(active ? C(255, 140, 0) : Neutral());
        g.FillPolygon(&fillBrush, funnelShape, static_cast<INT>(std::size(funnelShape)));
        Pen outlinePen(Neutral(), 4);
        outlinePen.SetLineJoin(LineJoinMiter);
        g.DrawPolygon(&outlinePen, funnelShape, static_cast<INT>(std::size(funnelShape)));
    }

    void PaintHelp(Graphics& g)
    {
        const Color blue = C(100, 149, 237);
        const Pen pen(blue, 10);
        const SolidBrush brush(blue);
        GraphicsPath path;
        path.AddBezier(20, 22, 20, 5, 44, 5, 44, 22);
        path.AddBezier(44, 22, 44, 32, 30, 28, 32, 42);
        g.DrawPath(&pen, &path);
        g.FillEllipse(&brush, 26, 48, 12, 12);
    }

    void PaintPause(Graphics& g)
    {
        const SolidBrush amberBrush(C(200, 160, 0));
        g.FillRectangle(&amberBrush, 5, 5, 19, 54);
        g.FillRectangle(&amberBrush, 39, 5, 19, 54);
    }

    void PaintMagnifier(Graphics& g, const bool plus)
    {
        const SolidBrush blueBrush(C(0, 102, 204));
        const Pen rimPen(Neutral(), 6);
        Pen handlePen(Neutral(), 10);
        handlePen.SetEndCap(LineCapRound);
        g.DrawEllipse(&rimPen, 6, 6, 40, 40);
        g.DrawLine(&handlePen, 39, 39, 58, 58);
        g.FillRectangle(&blueBrush, 14, 23, 24, 6);
        if (plus) g.FillRectangle(&blueBrush, 23, 14, 6, 24);
    }

    void PaintGear(Graphics& g)
    {
        const SolidBrush gearBrush(Neutral());
        const Point primaryTooth[] = { {-3, -27}, {3, -27}, {6, -17}, {-6, -17} };

        const GraphicsState state = g.Save();
        g.TranslateTransform(32, 32);

        for ([[maybe_unused]] const int i : std::views::iota(0, 8))
        {
            g.FillPolygon(&gearBrush, primaryTooth, static_cast<INT>(std::size(primaryTooth)));
            g.RotateTransform(45);
        }
        g.Restore(state);

        const Pen ringPen(Neutral(), 8);
        g.DrawEllipse(&ringPen, 15, 15, 34, 34);

        const Pen bearingPen(Neutral(), 1);
        g.DrawEllipse(&bearingPen, 23, 23, 18, 18);
    }

    void PaintWindowLayout(Graphics& g)
    {
        const Pen framePen(Neutral(), 4);
        g.DrawRectangle(&framePen, 4, 4, 56, 56);
        g.DrawLine(&framePen, 4, 34, 60, 34);
        g.DrawLine(&framePen, 36, 4, 36, 34);

        const SolidBrush afBrush(C(70, 120, 200)), ftBrush(C(60, 160, 90)), tmBrush(C(200, 140, 40));
        g.FillRectangle(&afBrush,  8,  8, 24, 22);
        g.FillRectangle(&ftBrush, 40,  8, 16, 22);
        g.FillRectangle(&tmBrush,  8, 38, 48, 18);
    }

    void PaintCharacter(Graphics& g, const WCHAR ch, const COLORREF clr, const bool bold)
    {
        const WCHAR text[]{ ch, L'\0' };
        const FontFamily fontFamily(wds::strFontSegoeUISymbol);
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        format.SetFormatFlags(StringFormatFlagsNoClip);

        GraphicsPath path;
        path.AddString(text, 1, &fontFamily,
            bold ? FontStyleBold : FontStyleRegular, 56,
            Rect(0, 0, 64, 64), &format);

        Rect bounds;
        path.GetBounds(&bounds);
        if (bounds.Width <= 0 || bounds.Height <= 0) return;

        const REAL scale = std::min(56.0f / bounds.Width, 56.0f / bounds.Height);
        Matrix m;
        m.Scale(scale, scale);
        path.Transform(&m);
        path.GetBounds(&bounds);
        m.Reset();
        m.Translate((64.0f - bounds.Width) / 2.0f - bounds.X,
                    (64.0f - bounds.Height) / 2.0f - bounds.Y);
        path.Transform(&m);

        const SolidBrush brush(Color(255, GetRValue(clr), GetGValue(clr), GetBValue(clr)));
        g.FillPath(&brush, &path);
    }

    std::function<void(Graphics&)> Char(const WCHAR ch, const COLORREF clr)
    {
        return [=](Graphics& g) { PaintCharacter(g, ch, clr); };
    }

    HICON IconFromFontChar(const WCHAR ch, const COLORREF clr, const bool bold, const int iconSize)
    {
        const int size = iconSize > 0 ? iconSize : GetSystemMetrics(SM_CXICON);
        return MakeIcon(size, [=](Graphics& g) { PaintCharacter(g, ch, clr, bold); });
    }
}

HBITMAP Icons::MakeBitmap(const int size, const std::function<void(Graphics&)>& painter)
{
    // Render directly at the target size in the canonical 64-unit space.
    // GDI+'s native antialiasing at the destination resolution produces
    // crisper edges than super-sampling and bicubic downsampling, which
    // compounds two AA passes and introduces ringing on thin strokes.
    Bitmap renderBitmap(size, size, PixelFormat32bppPARGB);
    Graphics g(&renderBitmap);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    const REAL scale = static_cast<REAL>(size) / 64.0f;
    g.ScaleTransform(scale, scale);
    painter(g);

    HBITMAP hbm = nullptr;
    renderBitmap.GetHBITMAP(Color(0, 0, 0, 0), &hbm);
    return hbm;
}

HICON Icons::MakeIcon(const int size, const std::function<void(Graphics&)>& painter)
{
    const SmartPointer color(DeleteObject, MakeBitmap(size, painter));
    if (color == nullptr) return nullptr;

    const CBitmap mask(size, size, 1, 1, nullptr);

    ICONINFO ii{
        .fIcon = true,
        .hbmMask = static_cast<HBITMAP>(mask.m_hObject),
        .hbmColor = color
    };
    return CreateIconIndirect(&ii);
}
