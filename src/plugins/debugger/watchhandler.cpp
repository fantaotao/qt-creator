/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://www.qtsoftware.com/contact.
**
**************************************************************************/

#include "watchhandler.h"
#include "watchutils.h"
#include "debuggeractions.h"

#if USE_MODEL_TEST
#include "modeltest.h"
#endif

#include <utils/qtcassert.h>

#include <QtCore/QDebug>
#include <QtCore/QEvent>
#include <QtCore/QtAlgorithms>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>

#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QLabel>
#include <QtGui/QToolTip>
#include <QtGui/QTextEdit>

#include <ctype.h>


// creates debug output regarding pending watch data results
//#define DEBUG_PENDING 1
// creates debug output for accesses to the model
//#define DEBUG_MODEL 1

#if DEBUG_MODEL
#   define MODEL_DEBUG(s) qDebug() << s
#else
#   define MODEL_DEBUG(s) 
#endif
#define MODEL_DEBUGX(s) qDebug() << s

namespace Debugger {
namespace Internal {

static const QString strNotInScope =
        QCoreApplication::translate("Debugger::Internal::WatchData", "<not in scope>");

static int watcherCounter = 0;
static int generationCounter = 0;

////////////////////////////////////////////////////////////////////
//
// WatchItem
//
////////////////////////////////////////////////////////////////////

class WatchItem : public WatchData
{
public:
    WatchItem() { parent = 0; fetchTriggered = false; }

    WatchItem(const WatchData &data) : WatchData(data)
        { parent = 0; fetchTriggered = false; }

    void setData(const WatchData &data)
        { static_cast<WatchData &>(*this) = data; }

    WatchItem *parent;
    bool fetchTriggered;      // children fetch has been triggered
    QList<WatchItem *> children;  // fetched children
};

////////////////////////////////////////////////////////////////////
//
// WatchData
//
////////////////////////////////////////////////////////////////////
   
WatchData::WatchData() :
    hasChildren(false),
    generation(-1),
    valuedisabled(false),
    source(0),
    state(InitialState),
    changed(false)
{
}

void WatchData::setError(const QString &msg)
{
    setAllUnneeded();
    value = msg;
    setHasChildren(false);
    valuedisabled = true;
}

void WatchData::setValue(const QString &value0)
{
    value = value0;
    if (value == "{...}") {
        value.clear();
        hasChildren = true; // at least one...
    }

    // avoid duplicated information
    if (value.startsWith("(") && value.contains(") 0x"))
        value = value.mid(value.lastIndexOf(") 0x") + 2);

    // doubles are sometimes displayed as "@0x6141378: 1.2".
    // I don't want that.
    if (/*isIntOrFloatType(type) && */ value.startsWith("@0x")
         && value.contains(':')) {
        value = value.mid(value.indexOf(':') + 2);
        setHasChildren(false);
    }

    // "numchild" is sometimes lying
    //MODEL_DEBUG("\n\n\nPOINTER: " << type << value);
    if (isPointerType(type))
        setHasChildren(value != "0x0" && value != "<null>");

    // pointer type information is available in the 'type'
    // column. No need to duplicate it here.
    if (value.startsWith("(" + type + ") 0x"))
        value = value.section(" ", -1, -1);
    
    setValueUnneeded();
}

void WatchData::setValueToolTip(const QString &tooltip)
{
    valuetooltip = tooltip;
}

void WatchData::setType(const QString &str)
{
    type = str.trimmed();
    bool changed = true;
    while (changed) {
        if (type.endsWith(QLatin1String("const")))
            type.chop(5);
        else if (type.endsWith(QLatin1Char(' ')))
            type.chop(1);
        else if (type.endsWith(QLatin1Char('&')))
            type.chop(1);
        else if (type.startsWith(QLatin1String("const ")))
            type = type.mid(6);
        else if (type.startsWith(QLatin1String("volatile ")))
            type = type.mid(9);
        else if (type.startsWith(QLatin1String("class ")))
            type = type.mid(6);
        else if (type.startsWith(QLatin1String("struct ")))
            type = type.mid(6);
        else if (type.startsWith(QLatin1Char(' ')))
            type = type.mid(1);
        else
            changed = false;
    }
    setTypeUnneeded();
    if (isIntOrFloatType(type))
        setHasChildren(false);
}

void WatchData::setAddress(const QString &str)
{
    addr = str;
}

WatchData WatchData::pointerChildPlaceHolder() const
{
    WatchData data1;
    data1.iname = iname + QLatin1String(".*");
    data1.name = QLatin1Char('*') + name;
    data1.exp = QLatin1String("(*(") + exp + QLatin1String("))");
    data1.type = stripPointerType(type);
    data1.setValueNeeded();
    return data1;
}

QString WatchData::toString() const
{
    const char *doubleQuoteComma = "\",";
    QString res;
    QTextStream str(&res);
    if (!iname.isEmpty())
        str << "iname=\"" << iname << doubleQuoteComma;
    if (!exp.isEmpty())
        str << "exp=\"" << exp << doubleQuoteComma;

    if (!variable.isEmpty())
        str << "variable=\"" << variable << doubleQuoteComma;

    if (isValueNeeded())
        str << "value=<needed>,";
    if (isValueKnown() && !value.isEmpty())
        str << "value=\"" << value << doubleQuoteComma;

    if (!editvalue.isEmpty())
        str << "editvalue=\"" << editvalue << doubleQuoteComma;

    if (isTypeNeeded())
        str << "type=<needed>,";
    if (isTypeKnown() && !type.isEmpty())
        str << "type=\"" << type << doubleQuoteComma;

    if (isHasChildrenNeeded())
        str << "hasChildren=<needed>,";
    if (isHasChildrenKnown())
        str << "hasChildren=\"" << (hasChildren ? "true" : "false") << doubleQuoteComma;

    if (isChildrenNeeded())
        str << "children=<needed>,";
    str.flush();
    if (res.endsWith(QLatin1Char(',')))
        res.truncate(res.size() - 1);
    return res + QLatin1Char('}');
}

// Format a tooltip fow with aligned colon
template <class Streamable>
void formatToolTipRow(QTextStream &str, const QString &category, const Streamable &value)
{
    str << "<tr><td>" << category << "</td><td> : </td><td>" << value  << "</td></tr>";
}

QString WatchData::toToolTip() const
{
    if (!valuetooltip.isEmpty())
        return QString::number(valuetooltip.size());
    QString res;
    QTextStream str(&res);
    str << "<html><body><table>";
    formatToolTipRow(str, WatchHandler::tr("Expression"), Qt::escape(exp));
    formatToolTipRow(str, WatchHandler::tr("Type"), Qt::escape(type));
    QString val = value;
    if (value.size() > 1000) {
        val.truncate(1000);
        val +=  WatchHandler::tr(" ... <cut off>");
    }
    formatToolTipRow(str, WatchHandler::tr("Value"), Qt::escape(val));
    formatToolTipRow(str, WatchHandler::tr("Object Address"), Qt::escape(addr));
    formatToolTipRow(str, WatchHandler::tr("Stored Address"), Qt::escape(saddr));
    formatToolTipRow(str, WatchHandler::tr("iname"), Qt::escape(iname));
    str << "</table></body></html>";
    return res;
}

///////////////////////////////////////////////////////////////////////
//
// WatchModel
//
///////////////////////////////////////////////////////////////////////

WatchModel::WatchModel(WatchHandler *handler, WatchType type)
    : QAbstractItemModel(handler), m_handler(handler), m_type(type)
{
    m_root = new WatchItem;
    m_root->hasChildren = 1;
    m_root->state = 0;
    m_root->name = WatchHandler::tr("Root");

    WatchItem *item = new WatchItem;

    switch (m_type) {
        case LocalsWatch:
            item->iname = QLatin1String("local");
            item->name = WatchHandler::tr("Locals");
            break;
        case WatchersWatch:
            item->iname = QLatin1String("watch");
            item->name = WatchHandler::tr("Watchers");
            break;
        case TooltipsWatch:
            item->iname = QLatin1String("tooltip");
            item->name = WatchHandler::tr("Tooltip");
            break;
    }
    item->hasChildren = true;
    item->state = 0;
    item->parent = m_root;
    item->fetchTriggered = true;

    m_root->children.append(item);
}

WatchItem *WatchModel::dummyRoot() const
{
    QTC_ASSERT(!m_root->children.isEmpty(), return 0);
    return m_root->children.front();
}

void WatchModel::reinitialize()
{
    WatchItem *item = dummyRoot();
    QTC_ASSERT(item, return);
    QModelIndex index = watchIndex(item);
    int n = item->children.size();
    if (n == 0)
        return;
    //MODEL_DEBUG("REMOVING " << n << " CHILDREN OF " << item->iname);
    beginRemoveRows(index, 0, n - 1);
    qDeleteAll(item->children);
    item->children.clear();
    endRemoveRows();
}

void WatchModel::removeOutdated()
{
    WatchItem *item = dummyRoot();
    QTC_ASSERT(item, return);
    foreach (WatchItem *child, item->children)
        removeOutdatedHelper(child);
#if DEBUG_MODEL
#if USE_MODEL_TEST
    //(void) new ModelTest(this, this);
#endif
#endif
}

void WatchModel::removeOutdatedHelper(WatchItem *item)
{
    if (item->generation < generationCounter)
        removeItem(item);
    else {
        foreach (WatchItem *child, item->children)
            removeOutdatedHelper(child);
        item->fetchTriggered = false;
    }
}

void WatchModel::removeItem(WatchItem *item)
{
    WatchItem *parent = item->parent;
    QModelIndex index = watchIndex(parent);
    int n = parent->children.indexOf(item);
    //MODEL_DEBUG("NEED TO REMOVE: " << item->iname << "AT" << n);
    beginRemoveRows(index, n, n);
    parent->children.removeAt(n);
    endRemoveRows();
}

static QString parentName(const QString &iname)
{
    int pos = iname.lastIndexOf(QLatin1Char('.'));
    if (pos == -1)
        return QString();
    return iname.left(pos);
}


static QString chopConst(QString type)
{
   while (1) {
        if (type.startsWith("const"))
            type = type.mid(5);
        else if (type.startsWith(' '))
            type = type.mid(1);
        else if (type.endsWith("const"))
            type.chop(5);
        else if (type.endsWith(' '))
            type.chop(1);
        else
            break;
    }
    return type;
}

QString niceType(QString type)
{
    type.replace('*', '@');

    for (int i = 0; i < 10; ++i) {
        int start = type.indexOf("std::allocator<");
        if (start == -1)
            break; 
        // search for matching '>'
        int pos;
        int level = 0;
        for (pos = start + 12; pos < type.size(); ++pos) {
            int c = type.at(pos).unicode();
            if (c == '<') {
                ++level;
            } else if (c == '>') {
                --level;
                if (level == 0)
                    break;
            }
        }
        QString alloc = type.mid(start, pos + 1 - start).trimmed();
        QString inner = alloc.mid(15, alloc.size() - 16).trimmed();

        if (inner == QLatin1String("char"))
            // std::string
            type.replace(QLatin1String("basic_string<char, std::char_traits<char>, "
                "std::allocator<char> >"), QLatin1String("string"));
        else if (inner == QLatin1String("wchar_t"))
            // std::wstring
            type.replace(QLatin1String("basic_string<wchar_t, std::char_traits<wchar_t>, "
                "std::allocator<wchar_t> >"), QLatin1String("wstring"));

        // std::vector, std::deque, std::list
        QRegExp re1(QString("(vector|list|deque)<%1, %2\\s*>").arg(inner, alloc));
        if (re1.indexIn(type) != -1)
            type.replace(re1.cap(0), QString("%1<%2>").arg(re1.cap(1), inner));


        // std::stack
        QRegExp re6(QString("stack<%1, std::deque<%2> >").arg(inner, inner));
        re6.setMinimal(true);
        if (re6.indexIn(type) != -1)
            type.replace(re6.cap(0), QString("stack<%1>").arg(inner));

        // std::set
        QRegExp re4(QString("set<%1, std::less<%2>, %3\\s*>").arg(inner, inner, alloc));
        re4.setMinimal(true);
        if (re4.indexIn(type) != -1)
            type.replace(re4.cap(0), QString("set<%1>").arg(inner));


        // std::map
        if (inner.startsWith("std::pair<")) {
            // search for outermost ','
            int pos;
            int level = 0;
            for (pos = 10; pos < inner.size(); ++pos) {
                int c = inner.at(pos).unicode();
                if (c == '<')
                    ++level;
                else if (c == '>')
                    --level;
                else if (c == ',' && level == 0)
                    break;
            }
            QString ckey = inner.mid(10, pos - 10);
            QString key = chopConst(ckey);
            QString value = inner.mid(pos + 2, inner.size() - 3 - pos);

            QRegExp re5(QString("map<%1, %2, std::less<%3>, %4\\s*>")
                .arg(key, value, key, alloc));
            re5.setMinimal(true);
            if (re5.indexIn(type) != -1)
                type.replace(re5.cap(0), QString("map<%1, %2>").arg(key, value));
            else {
                QRegExp re7(QString("map<const %1, %2, std::less<const %3>, %4\\s*>")
                    .arg(key, value, key, alloc));
                re7.setMinimal(true);
                if (re7.indexIn(type) != -1)
                    type.replace(re7.cap(0), QString("map<const %1, %2>").arg(key, value));
            }
        }
    }
    type.replace('@', '*');
    type.replace(QLatin1String(" >"), QString(QLatin1Char('>')));
    return type;
}

static QString formattedValue(const WatchData &data,
    int individualFormat, int typeFormat)
{
    if (data.type == "int") {
        int format = individualFormat == -1 ? typeFormat : individualFormat;
        int value = data.value.toInt();
        if (format == 1)
            return ("(hex) ") + QString::number(value, 16);
        if (format == 2)
            return ("(bin) ") + QString::number(value, 2);
        if (format == 3)
            return ("(oct) ") + QString::number(value, 8);
    }

    return data.value;
}

bool WatchModel::canFetchMore(const QModelIndex &index) const
{
    return index.isValid() && !watchItem(index)->fetchTriggered;
}

void WatchModel::fetchMore(const QModelIndex &index)
{
    QTC_ASSERT(index.isValid(), return);
    QTC_ASSERT(!watchItem(index)->fetchTriggered, return);
    if (WatchItem *item = watchItem(index)) {
        item->fetchTriggered = true;
        WatchData data = *item;
        data.setChildrenNeeded();
        emit m_handler->watchDataUpdateNeeded(data);
    }
}

QModelIndex WatchModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    const WatchItem *item = watchItem(parent);
    QTC_ASSERT(item, return QModelIndex());
    return createIndex(row, column, (void*)(item->children.at(row)));
}

QModelIndex WatchModel::parent(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return QModelIndex();

    const WatchItem *item = watchItem(idx);
    if (!item->parent || item->parent == m_root)
        return QModelIndex();

    const WatchItem *grandparent = item->parent->parent;
    if (!grandparent)
        return QModelIndex();

    for (int i = 0; i < grandparent->children.size(); ++i)
        if (grandparent->children.at(i) == item->parent)
            return createIndex(i, 0, (void*) item->parent);

    return QModelIndex();
}

int WatchModel::rowCount(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return 1;
    if (idx.column() > 0)
        return 0;
    return watchItem(idx)->children.size();
}

int WatchModel::columnCount(const QModelIndex &idx) const
{
    Q_UNUSED(idx);
    return 3;
}

bool WatchModel::hasChildren(const QModelIndex &parent) const
{
    WatchItem *item = watchItem(parent);
    return !item || item->hasChildren;
}

WatchItem *WatchModel::watchItem(const QModelIndex &idx) const
{
    return idx.isValid() 
        ? static_cast<WatchItem*>(idx.internalPointer()) : m_root;
}

QModelIndex WatchModel::watchIndex(const WatchItem *item) const
{
    return watchIndexHelper(item, m_root, QModelIndex());
}

QModelIndex WatchModel::watchIndexHelper(const WatchItem *needle, 
    const WatchItem *parentItem, const QModelIndex &parentIndex) const
{
    if (needle == parentItem)
        return parentIndex;
    for (int i = parentItem->children.size(); --i >= 0; ) {
        const WatchItem *childItem = parentItem->children.at(i);
        QModelIndex childIndex = index(i, 0, parentIndex);
        QModelIndex idx = watchIndexHelper(needle, childItem, childIndex);
        if (idx.isValid())
            return idx;
    }
    return QModelIndex();
}

void WatchModel::emitDataChanged(int column, const QModelIndex &parentIndex) 
{
    QModelIndex idx1 = index(0, column, parentIndex);
    QModelIndex idx2 = index(rowCount(parentIndex) - 1, column, parentIndex);
    if (idx1.isValid() && idx2.isValid())
        emit dataChanged(idx1, idx2);
    //qDebug() << "CHANGING:\n" << idx1 << "\n" << idx2 << "\n"
    //    << data(parentIndex, INameRole).toString();
    for (int i = rowCount(parentIndex); --i >= 0; )
        emitDataChanged(column, index(i, 0, parentIndex));
}

QVariant WatchModel::data(const QModelIndex &idx, int role) const
{
    const WatchItem &data = *watchItem(idx);

    switch (role) {
        case Qt::DisplayRole: {
            switch (idx.column()) {
                case 0: return data.name;
                case 1: return formattedValue(data,
                    m_handler->m_individualFormats[data.iname],
                    m_handler->m_typeFormats[data.type]);
                case 2: return niceType(data.type);
                default: break;
            }
            break;
        }

        case Qt::ToolTipRole:
            return data.toToolTip();

        case Qt::ForegroundRole: {
            static const QVariant red(QColor(200, 0, 0));
            static const QVariant black(QColor(0, 0, 0));
            static const QVariant gray(QColor(140, 140, 140));
            switch (idx.column()) {
                case 0: return black;
                case 1: return data.valuedisabled ? gray : data.changed ? red : black;
                case 2: return black;
            }
            break;
        }

        case ExpressionRole:
            return data.exp;

        case INameRole:
            return data.iname;

        case ExpandedRole:
            return m_handler->m_expandedINames.contains(data.iname);
            //FIXME return node < 4 || m_expandedINames.contains(data.iname);

        case ActiveDataRole:
            qDebug() << "ASK FOR" << data.iname;
            return true;
   
        case TypeFormatListRole:
            if (data.type == "int")
                return QStringList() << tr("decimal") << tr("hexadecimal")
                    << tr("binary") << tr("octal");
            break;

        case TypeFormatRole:
            return m_handler->m_typeFormats[data.type];

        case IndividualFormatRole: {
            int format = m_handler->m_individualFormats[data.iname];
            if (format == -1)
                return m_handler->m_typeFormats[data.type];
            return format;
        }

        default:
            break; 
    }
    return QVariant();
}

bool WatchModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    WatchItem &data = *watchItem(index);
    if (role == ExpandedRole) {
        if (value.toBool())
            m_handler->m_expandedINames.insert(data.iname);
        else
            m_handler->m_expandedINames.remove(data.iname);
    } else if (role == TypeFormatRole) {
        m_handler->setFormat(data.type, value.toInt());
    } else if (role == IndividualFormatRole) {
        m_handler->m_individualFormats[data.iname] = value.toInt();
    }
    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags WatchModel::flags(const QModelIndex &idx) const
{
    using namespace Qt;

    if (!idx.isValid())
        return ItemFlags();

    // enabled, editable, selectable, checkable, and can be used both as the
    // source of a drag and drop operation and as a drop target.

    static const ItemFlags notEditable =
          ItemIsSelectable
        | ItemIsDragEnabled
        | ItemIsDropEnabled
        // | ItemIsUserCheckable
        // | ItemIsTristate
        | ItemIsEnabled;

    static const ItemFlags editable = notEditable | ItemIsEditable;

    const WatchData &data = *watchItem(idx);

    if (data.isWatcher() && idx.column() == 0)
        return editable; // watcher names are editable
    if (data.isWatcher() && idx.column() == 2)
        return editable; // watcher types are
    if (idx.column() == 1)
        return editable; // locals and watcher values are editable
    return  notEditable;
}

QVariant WatchModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical)
        return QVariant();
    if (role == Qt::DisplayRole) {
        switch (section) {
            case 0: return QString(tr("Name")  + QLatin1String("     "));
            case 1: return QString(tr("Value") + QLatin1String("     "));
            case 2: return QString(tr("Type")  + QLatin1String("     "));
        }
    }
    return QVariant(); 
}

static bool iNameSorter(const WatchItem *item1, const WatchItem *item2)
{
    QString name1 = item1->iname.section('.', -1);
    QString name2 = item2->iname.section('.', -1);
    if (!name1.isEmpty() && !name2.isEmpty()) {
        if (name1.at(0).isDigit() && name2.at(0).isDigit())
            return name1.toInt() < name2.toInt();
    }
    return name1 < name2; 
}

static int findInsertPosition(const QList<WatchItem *> &list, const WatchItem *item)
{
    QList<WatchItem *>::const_iterator it =
        qLowerBound(list.begin(), list.end(), item, iNameSorter);
    return it - list.begin(); 
}

void WatchModel::insertData(const WatchData &data)
{
    QTC_ASSERT(!data.iname.isEmpty(), return);
    WatchItem *parent = findItem(parentName(data.iname), m_root);
    if (!parent) {
        WatchData parent;
        parent.iname = parentName(data.iname);
        insertData(parent);
        //MODEL_DEBUG("\nFIXING MISSING PARENT FOR\n" << data.iname);
        return;
    }
    QModelIndex index = watchIndex(parent);
    if (WatchItem *oldItem = findItem(data.iname, parent)) {
        // overwrite old entry
        //MODEL_DEBUG("OVERWRITE : " << data.iname << data.value);
        bool changed = !data.value.isEmpty()
            && data.value != oldItem->value
            && data.value != strNotInScope;
        oldItem->setData(data);
        oldItem->changed = changed;
        oldItem->generation = generationCounter;
        QModelIndex idx = watchIndex(oldItem);
        emit dataChanged(idx, idx.sibling(idx.row(), 2));
    } else {
        // add new entry
        //MODEL_DEBUG("INSERT : " << data.iname << data.value);
        WatchItem *item = new WatchItem(data);
        item->parent = parent;
        item->generation = generationCounter;
        item->changed = true;
        int n = findInsertPosition(parent->children, item);
        beginInsertRows(index, n, n);
        parent->children.insert(n, item);
        endInsertRows();
    }
}

WatchItem *WatchModel::findItem(const QString &iname, WatchItem *root) const
{
    if (root->iname == iname)
        return root;
    for (int i = root->children.size(); --i >= 0; )
        if (WatchItem *item = findItem(iname, root->children.at(i)))
            return item;
    return 0;
}


///////////////////////////////////////////////////////////////////////
//
// WatchHandler
//
///////////////////////////////////////////////////////////////////////

WatchHandler::WatchHandler()
{
    m_expandPointers = true;
    m_inChange = false;

    m_locals = new WatchModel(this, LocalsWatch);
    m_watchers = new WatchModel(this, WatchersWatch);
    m_tooltips = new WatchModel(this, TooltipsWatch);

    connect(theDebuggerAction(WatchExpression),
        SIGNAL(triggered()), this, SLOT(watchExpression()));
    connect(theDebuggerAction(RemoveWatchExpression),
        SIGNAL(triggered()), this, SLOT(removeWatchExpression()));
}

void WatchHandler::endCycle()
{
    m_locals->removeOutdated();
    m_watchers->removeOutdated();
    m_tooltips->removeOutdated();
}

void WatchHandler::cleanup()
{
    m_expandedINames.clear();
    m_displayedINames.clear();
    m_locals->reinitialize();
    m_tooltips->reinitialize();
#if 0
    for (EditWindows::ConstIterator it = m_editWindows.begin();
            it != m_editWindows.end(); ++it) {
        if (!it.value().isNull())
            delete it.value();
    }
    m_editWindows.clear();
#endif
}

void WatchHandler::insertData(const WatchData &data)
{
    MODEL_DEBUG("INSERTDATA: " << data.toString());
    QTC_ASSERT(data.isValid(), return);
    if (data.isSomethingNeeded()) {
        emit watchDataUpdateNeeded(data);
    } else {
        WatchModel *model = modelForIName(data.iname);
        QTC_ASSERT(model, return);
        model->insertData(data);
    }
}

void WatchHandler::removeData(const QString &iname)
{
    WatchModel *model = modelForIName(iname);
    if (!model)
        return;
    WatchItem *item = model->findItem(iname, model->m_root);
    if (item)
        model->removeItem(item);
}

void WatchHandler::watchExpression()
{
    if (QAction *action = qobject_cast<QAction *>(sender()))
        watchExpression(action->data().toString());
}

QString WatchHandler::watcherName(const QString &exp)
{
    return QLatin1String("watch.") + QString::number(m_watcherNames[exp]);
}

void WatchHandler::watchExpression(const QString &exp)
{
    // FIXME: 'exp' can contain illegal characters
    m_watcherNames[exp] = watcherCounter++;
    WatchData data;
    data.exp = exp;
    data.name = exp;
    if (exp.isEmpty() || exp == watcherEditPlaceHolder())
        data.setAllUnneeded();
    data.iname = watcherName(exp);
    insertData(data);
    saveWatchers();
    //emit watchModelUpdateRequested();
}

void WatchHandler::setDisplayedIName(const QString &iname, bool on)
{
    Q_UNUSED(iname);
    Q_UNUSED(on);
/*
    WatchData *d = findData(iname);
    if (!on || !d) {
        delete m_editWindows.take(iname);
        m_displayedINames.remove(iname);
        return;
    }
    if (d->exp.isEmpty()) {
        //emit statusMessageRequested(tr("Sorry. Cannot visualize objects without known address."), 5000);
        return;
    }
    d->setValueNeeded();
    m_displayedINames.insert(iname);
    insertData(*d);
*/
}

void WatchHandler::showEditValue(const WatchData &data)
{
    // editvalue is always base64 encoded
    QByteArray ba = QByteArray::fromBase64(data.editvalue);
    //QByteArray ba = data.editvalue;
    QWidget *w = m_editWindows.value(data.iname);
    qDebug() << "SHOW_EDIT_VALUE " << data.toString() << data.type
            << data.iname << w;
    if (data.type == QLatin1String("QImage")) {
        if (!w) {
            w = new QLabel;
            m_editWindows[data.iname] = w;
        }
        QDataStream ds(&ba, QIODevice::ReadOnly);
        QVariant v;
        ds >> v;
        QString type = QString::fromAscii(v.typeName());
        QImage im = v.value<QImage>();
        if (QLabel *l = qobject_cast<QLabel *>(w))
            l->setPixmap(QPixmap::fromImage(im));
    } else if (data.type == QLatin1String("QPixmap")) {
        if (!w) {
            w = new QLabel;
            m_editWindows[data.iname] = w;
        }
        QDataStream ds(&ba, QIODevice::ReadOnly);
        QVariant v;
        ds >> v;
        QString type = QString::fromAscii(v.typeName());
        QPixmap im = v.value<QPixmap>();
        if (QLabel *l = qobject_cast<QLabel *>(w))
            l->setPixmap(im);
    } else if (data.type == QLatin1String("QString")) {
        if (!w) {
            w = new QTextEdit;
            m_editWindows[data.iname] = w;
        }
#if 0
        QDataStream ds(&ba, QIODevice::ReadOnly);
        QVariant v;
        ds >> v;
        QString type = QString::fromAscii(v.typeName());
        QString str = v.value<QString>();
#else
        MODEL_DEBUG("DATA: " << ba);
        QString str = QString::fromUtf16((ushort *)ba.constData(), ba.size()/2);
#endif
        if (QTextEdit *t = qobject_cast<QTextEdit *>(w))
            t->setText(str);
    }
    if (w)
        w->show();
}

void WatchHandler::removeWatchExpression()
{
    if (QAction *action = qobject_cast<QAction *>(sender()))
        removeWatchExpression(action->data().toString());
}

void WatchHandler::removeWatchExpression(const QString &exp)
{
    MODEL_DEBUG("REMOVE WATCH: " << exp);
    m_watcherNames.remove(exp);
    foreach (WatchItem *item, m_watchers->dummyRoot()->children) {
        if (item->exp == exp) {
            m_watchers->removeItem(item);
            saveWatchers();
            break;
        }
    }
}

void WatchHandler::beginCycle()
{
    ++generationCounter;
    //m_locals->beginCycle();
}

void WatchHandler::updateWatchers()
{
    //qDebug() << "UPDATE WATCHERS";
    // copy over all watchers and mark all watchers as incomplete
    foreach (const QString &exp, m_watcherNames.keys()) {
        WatchData data;
        data.iname = watcherName(exp);
        data.setAllNeeded();
        data.name = exp;
        data.exp = exp;
        insertData(data);
    }
}

void WatchHandler::loadWatchers()
{
    QVariant value;
    sessionValueRequested("Watchers", &value);
    foreach (const QString &exp, value.toStringList())
        m_watcherNames[exp] = watcherCounter++;

    //qDebug() << "LOAD WATCHERS: " << m_watchers;
    //reinitializeWatchersHelper();
}

void WatchHandler::saveWatchers()
{
    //qDebug() << "SAVE WATCHERS: " << m_watchers;
    // Filter out valid watchers.
    QStringList watcherNames;
    QHashIterator<QString, int> it(m_watcherNames);
    while (it.hasNext()) {
        it.next();
        const QString &watcherName = it.key();
        if (!watcherName.isEmpty() && watcherName != watcherEditPlaceHolder())
            watcherNames.push_back(watcherName);
    }
    setSessionValueRequested("Watchers", QVariant(watcherNames));
}

void WatchHandler::loadTypeFormats()
{
    QVariant value;
    sessionValueRequested("DefaultFormats", &value);
    QMap<QString, QVariant> typeFormats = value.toMap();
    QMapIterator<QString, QVariant> it(typeFormats);
    while (it.hasNext()) {
        it.next();
        if (!it.key().isEmpty())
            m_typeFormats.insert(it.key(), it.value().toInt());
    }
}

void WatchHandler::saveTypeFormats()
{
    QMap<QString, QVariant> typeFormats;
    QHashIterator<QString, int> it(m_typeFormats);
    while (it.hasNext()) {
        it.next();
        QString key = it.key().trimmed();
        if (!key.isEmpty())
            typeFormats.insert(key, it.value());
    }
    setSessionValueRequested("DefaultFormats", QVariant(typeFormats));
}

void WatchHandler::saveSessionData()
{
    saveWatchers();
    saveTypeFormats();
}

void WatchHandler::loadSessionData()
{
    loadWatchers();
    loadTypeFormats();
    foreach (const QString &exp, m_watcherNames.keys()) {
        WatchData data;
        data.iname = watcherName(exp);
        data.setAllUnneeded();
        data.name = exp;
        data.exp = exp;
        insertData(data);
    }
}

WatchModel *WatchHandler::model(WatchType type) const
{
    switch (type) {
        case LocalsWatch: return m_locals;
        case WatchersWatch: return m_watchers;
        case TooltipsWatch: return m_tooltips;
    }
    QTC_ASSERT(false, /**/);
    return 0;
}
    
WatchModel *WatchHandler::modelForIName(const QString &iname) const
{
    if (iname.startsWith(QLatin1String("local.")))
        return m_locals;
    if (iname.startsWith(QLatin1String("watch.")))
        return m_watchers;
    if (iname.startsWith(QLatin1String("tooltip")))
        return m_tooltips;
    QTC_ASSERT(false, /**/);
    return 0;
}

WatchData *WatchHandler::findItem(const QString &iname) const
{
    const WatchModel *model = modelForIName(iname);
    QTC_ASSERT(model, return 0);
    return model->findItem(iname, model->m_root);
}

QString WatchHandler::watcherEditPlaceHolder()
{
    static const QString rc = tr("<Edit>");
    return rc;
}

void WatchHandler::setFormat(const QString &type, int format)
{
    m_typeFormats[type] = format;
    saveTypeFormats();
    m_locals->emitDataChanged(1);
    m_watchers->emitDataChanged(1);
    m_tooltips->emitDataChanged(1);
}

} // namespace Internal
} // namespace Debugger
