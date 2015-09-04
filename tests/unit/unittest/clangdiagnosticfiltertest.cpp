/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://www.qt.io/licensing.  For further information
** use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file.  Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include <clangdiagnosticfilter.h>
#include <diagnosticcontainer.h>

#include <gmock/gmock.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include "gtest-qt-printing.h"

namespace {

using ::testing::Contains;
using ::testing::Not;

using ClangBackEnd::DiagnosticContainer;

DiagnosticContainer createDiagnostic(const QString &text,
                                     ClangBackEnd::DiagnosticSeverity severity,
                                     const QString &filePath)
{
    return DiagnosticContainer(
            text,
            Utf8StringLiteral(""),
            {},
            severity,
            {filePath, 0u, 0u},
            {},
            {},
            {}
    );
}

const QString mainFilePath = QString::fromUtf8(TESTDATA_DIR "/diagnostic_erroneous_source.cpp");
const QString includedFilePath = QString::fromUtf8(TESTDATA_DIR "/diagnostic_erroneous_header.cpp");

DiagnosticContainer warningFromIncludedFile()
{
    return createDiagnostic(
                QStringLiteral("warning: control reaches end of non-void function"),
                ClangBackEnd::DiagnosticSeverity::Warning,
                includedFilePath);
}

DiagnosticContainer errorFromIncludedFile()
{
    return createDiagnostic(
                QStringLiteral("C++ requires a type specifier for all declarations"),
                ClangBackEnd::DiagnosticSeverity::Error,
                includedFilePath);
}

DiagnosticContainer warningFromMainFile()
{
    return createDiagnostic(
                QStringLiteral("warning: enumeration value 'Three' not handled in switch"),
                ClangBackEnd::DiagnosticSeverity::Warning,
                mainFilePath);
}

DiagnosticContainer errorFromMainFile()
{
    return createDiagnostic(
                QStringLiteral("error: void function 'g' should not return a value"),
                ClangBackEnd::DiagnosticSeverity::Error,
                mainFilePath);
}

class ClangDiagnosticFilter : public ::testing::Test
{
protected:
    ClangCodeModel::Internal::ClangDiagnosticFilter clangDiagnosticFilter{mainFilePath};
};

TEST_F(ClangDiagnosticFilter, WarningsFromMainFileAreLetThrough)
{
    clangDiagnosticFilter.filter({warningFromMainFile()});

    ASSERT_THAT(clangDiagnosticFilter.takeWarnings(),Contains(warningFromMainFile()));
}

TEST_F(ClangDiagnosticFilter, WarningsFromIncludedFileAreIgnored)
{
    clangDiagnosticFilter.filter({warningFromIncludedFile()});

    ASSERT_THAT(clangDiagnosticFilter.takeWarnings(), Not(Contains(warningFromIncludedFile())));
}

TEST_F(ClangDiagnosticFilter, ErrorsFromMainFileAreLetThrough)
{
    clangDiagnosticFilter.filter({errorFromMainFile()});

    ASSERT_THAT(clangDiagnosticFilter.takeErrors(), Contains(errorFromMainFile()));
}

TEST_F(ClangDiagnosticFilter, ErrorsFromIncludedFileAreIgnored)
{
    clangDiagnosticFilter.filter({errorFromIncludedFile()});

    ASSERT_THAT(clangDiagnosticFilter.takeErrors(), Not(Contains(errorFromIncludedFile())));
}

} // anonymous namespace
