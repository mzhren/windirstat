// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#ifndef GIT_COMMIT
#define GIT_COMMIT "deadbeef"
#endif

#ifndef GIT_DATE
#define GIT_DATE "0000-00-00"
#endif

#ifndef GIT_COUNT
#define GIT_COUNT 0
#endif

#ifndef PRODUCTION
#define PRODUCTION 0
#endif

#define PRD_MAJVER                  2 // major product version
#define PRD_MINVER                  10 // minor product version
#define PRD_PATCH                   0 // patch number for product
#define PRD_BUILD                   GIT_COUNT // build number for product
#define FILE_MAJVER                 PRD_MAJVER // major file version
#define FILE_MINVER                 PRD_MINVER // minor file version
#define FILE_PATCH                  PRD_PATCH // patch number for version
#define FILE_BUILD                  PRD_BUILD // build number for version
#define TEXT_WEBSITE                https:/##/diskseek.space // website
#define TEXT_PRODUCTNAME            盘寻 // product's name
#define TEXT_FILEDESC               盘寻 (diskseek.space) - 磁盘空间统计 // component description

#define STRING_COMPANY              diskseek.space
#define STRING_COPYRIGHT            "© diskseek.space / WinDirStat Team"
#define STRING_EXENAME              DiskSeek.exe
#define SOURCE_REPOSITORY           https://github.com/windirstat/windirstat
