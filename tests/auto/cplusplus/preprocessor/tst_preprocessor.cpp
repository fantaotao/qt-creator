/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#include <QtTest>
#include <pp.h>

//TESTED_COMPONENT=src/libs/cplusplus
using namespace CPlusPlus;

class tst_Preprocessor: public QObject
{
Q_OBJECT

private Q_SLOTS:
    void va_args();
    void named_va_args();
    void first_empty_macro_arg();
    void param_expanding_as_multiple_params();
    void macro_definition_lineno();
    void unfinished_function_like_macro_call();
    void nasty_macro_expansion();
    void tstst();
};

void tst_Preprocessor::va_args()
{
    Client *client = 0; // no client.
    Environment env;

    Preprocessor preprocess(client, &env);
    QByteArray preprocessed = preprocess(QLatin1String("<stdin>"),
                                         QByteArray("\n#define foo(...) int f(__VA_ARGS__);"
                                                    "\nfoo(  )\n"
                                                    "\nfoo(int a)\n"
                                                    "\nfoo(int a,int b)\n"));

    QVERIFY(preprocessed.contains("int f();"));
    QVERIFY(preprocessed.contains("int f(int a);"));
    QVERIFY(preprocessed.contains("int f(int a,int b);"));
}

void tst_Preprocessor::named_va_args()
{
    Client *client = 0; // no client.
    Environment env;

    Preprocessor preprocess(client, &env);
    QByteArray preprocessed = preprocess(QLatin1String("<stdin>"),
                                         QByteArray("\n#define foo(ARGS...) int f(ARGS);"
                                                    "\nfoo(  )\n"
                                                    "\nfoo(int a)\n"
                                                    "\nfoo(int a,int b)\n"));

    QVERIFY(preprocessed.contains("int f();"));
    QVERIFY(preprocessed.contains("int f(int a);"));
    QVERIFY(preprocessed.contains("int f(int a,int b);"));
}

void tst_Preprocessor::first_empty_macro_arg()
{
    Client *client = 0; // no client.
    Environment env;

    Preprocessor preprocess(client, &env);
    QByteArray preprocessed = preprocess(QLatin1String("<stdin>"),
                                         QByteArray("\n#define foo(a,b) a int b;"
                                                    "\nfoo(const,cVal)\n"
                                                    "\nfoo(,Val)\n"
                                                    "\nfoo( ,Val2)\n"));

    QVERIFY(preprocessed.contains("const int cVal;"));
    QVERIFY(preprocessed.contains("int Val;"));
    QVERIFY(preprocessed.contains("int Val2;"));
}

void tst_Preprocessor::param_expanding_as_multiple_params()
{
    Client *client = 0; // no client.
    Environment env;

    Preprocessor preprocess(client, &env);
    QByteArray preprocessed = preprocess(QLatin1String("<stdin>"),
                                         QByteArray("\n#define foo(a,b) int f(a,b);"
                                                    "\n#define ARGS(t)  t a,t b"
                                                    "\nfoo(ARGS(int))"));
    QVERIFY(preprocessed.contains("int f(int a,int b);"));
}

void tst_Preprocessor::macro_definition_lineno()
{
    Client *client = 0; // no client.
    Environment env;
    Preprocessor preprocess(client, &env);
    QByteArray preprocessed = preprocess(QLatin1String("<stdin>"),
                                         QByteArray("#define foo(ARGS) int f(ARGS)\n"
                                                    "foo(int a);\n"));
    QVERIFY(preprocessed.contains("#gen true\n# 2 "));

    preprocessed = preprocess(QLatin1String("<stdin>"),
                              QByteArray("#define foo(ARGS) int f(ARGS)\n"
                                         "foo(int a)\n"
                                         ";\n"));
    QVERIFY(preprocessed.contains("#gen true\n# 2 "));

    preprocessed = preprocess(QLatin1String("<stdin>"),
                              QByteArray("#define foo(ARGS) int f(ARGS)\n"
                                         "foo(int  \n"
                                         "    a);\n"));
    QVERIFY(preprocessed.contains("#gen true\n# 2 "));

    preprocessed = preprocess(QLatin1String("<stdin>"),
                              QByteArray("#define foo int f\n"
                                         "foo;\n"));
    QVERIFY(preprocessed.contains("#gen true\n# 2 "));

    preprocessed = preprocess(QLatin1String("<stdin>"),
                              QByteArray("#define foo int f\n"
                                         "foo\n"
                                         ";\n"));
    QVERIFY(preprocessed.contains("#gen true\n# 2 "));
}

void tst_Preprocessor::unfinished_function_like_macro_call()
{
    Client *client = 0; // no client.
    Environment env;

    Preprocessor preprocess(client, &env);
    QByteArray preprocessed = preprocess(QLatin1String("<stdin>"),
                                         QByteArray("\n#define foo(a,b) a + b"
                                         "\nfoo(1, 2\n"));

    QCOMPARE(preprocessed.trimmed(), QByteArray("foo"));
}

void tst_Preprocessor::nasty_macro_expansion()
{
    QByteArray input("\n"
                     "#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))\n"
                     "#define is_power_of_two(x)      ( !((x) & ((x)-1)) )\n"
                     "#define low_bit_mask(x)         ( ((x)-1) & ~(x) )\n"
                     "#define is_valid_mask(x)        is_power_of_two(1LU + (x) + low_bit_mask(x))\n"
                     "#define compile_ffs2(__x) \\\n"
                     "        __builtin_choose_expr(((__x) & 0x1), 0, 1)\n"
                     "#define compile_ffs4(__x) \\\n"
                     "        __builtin_choose_expr(((__x) & 0x3), \\\n"
                     "                              (compile_ffs2((__x))), \\\n"
                     "                              (compile_ffs2((__x) >> 2) + 2))\n"
                     "#define compile_ffs8(__x) \\\n"
                     "        __builtin_choose_expr(((__x) & 0xf), \\\n"
                     "                              (compile_ffs4((__x))), \\\n"
                     "                              (compile_ffs4((__x) >> 4) + 4))\n"
                     "#define compile_ffs16(__x) \\\n"
                     "        __builtin_choose_expr(((__x) & 0xff), \\\n"
                     "                              (compile_ffs8((__x))), \\\n"
                     "                              (compile_ffs8((__x) >> 8) + 8))\n"
                     "#define compile_ffs32(__x) \\\n"
                     "        __builtin_choose_expr(((__x) & 0xffff), \\\n"
                     "                              (compile_ffs16((__x))), \\\n"
                     "                              (compile_ffs16((__x) >> 16) + 16))\n"
                     "#define FIELD_CHECK(__mask, __type)                     \\\n"
                     "        BUILD_BUG_ON(!(__mask) ||                       \\\n"
                     "                     !is_valid_mask(__mask) ||          \\\n"
                     "                     (__mask) != (__type)(__mask))      \\\n"
                     "\n"
                     "#define FIELD32(__mask)                         \\\n"
                     "({                                              \\\n"
                     "        FIELD_CHECK(__mask, u32);               \\\n"
                     "        (struct rt2x00_field32) {               \\\n"
                     "                compile_ffs32(__mask), (__mask) \\\n"
                     "        };                                      \\\n"
                     "})\n"
                     "#define BBPCSR                          0x00f0\n"
                     "#define BBPCSR_BUSY                     FIELD32(0x00008000)\n"
                     "#define WAIT_FOR_BBP(__dev, __reg)  \\\n"
                     "        rt2x00pci_regbusy_read((__dev), BBPCSR, BBPCSR_BUSY, (__reg))\n"
                     "if (WAIT_FOR_BBP(rt2x00dev, &reg)) {}\n"
                     );

    Client *client = 0; // no client.
    Environment env;

    Preprocessor preprocess(client, &env);
    QByteArray preprocessed = preprocess(QLatin1String("<stdin>"), input);

    QVERIFY(!preprocessed.contains("FIELD32"));
}

void tst_Preprocessor::tstst()
{
    Client *client = 0; // no client.
    Environment env;

    Preprocessor preprocess(client, &env);
    QByteArray preprocessed = preprocess(
                QLatin1String("<stdin>"),
                QByteArray("\n"
                           "# define _GLIBCXX_VISIBILITY(V) __attribute__ ((__visibility__ (#V)))\n"
                           "namespace std _GLIBCXX_VISIBILITY(default) {\n"
                           "}\n"
                           ));
    const QByteArray result =
            "namespace std \n"
            "#gen true\n"
            "# 3 \"<stdin>\"\n"
            "              __attribute__ ((__visibility__ (\"default\")))\n"
            "#gen false\n"
            "# 3 \"<stdin>\"\n"
            "                                           {\n"
            "}";

    QVERIFY(preprocessed.contains(result));
}

QTEST_APPLESS_MAIN(tst_Preprocessor)
#include "tst_preprocessor.moc"
