// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef QTCOMPAT_H
#define QTCOMPAT_H

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QRegularExpression>
class QEnterEvent;

#define SKIP_EMPTY_PARTS Qt::SkipEmptyParts
#define QT_ENDL Qt::endl

// #define QT_ARG_BOOL(type, value) QGenericArgument("bool", value))

// auto arg = Q_ARG(QString, id);
// QGenericArgument genArg("QString", arg.data);

// #define QT_ARG(type, value) { \
//     auto arg = Q_ARG(type, value); \
//     QGenericArgument genArg(arg.name, arg.data); \
//     return genArg;\
// }

#define QT_ARG(type, value) QGenericArgument(QMetaType::fromType<type>().name(), &value)
#define QT_RE_ARG(type, value) QGenericReturnArgument(QMetaType::fromType<type>().name(), &value)

using EnterEvent = QEnterEvent;
#else
class QEvent;

#define QT_ARG(type, value) Q_ARG(type, value)
#define QT_RE_ARG(type, value) Q_RETURN_ARG(type, value)

#define SKIP_EMPTY_PARTS QString::SkipEmptyParts
#define QT_ENDL endl

using EnterEvent = QEvent;
#endif

#endif // QTCOMPAT_H
