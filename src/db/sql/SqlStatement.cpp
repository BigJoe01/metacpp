/****************************************************************************
* Copyright 2014-2015 Trefilov Dmitrij                                      *
*                                                                           *
* Licensed under the Apache License, Version 2.0 (the "License");           *
* you may not use this file except in compliance with the License.          *
* You may obtain a copy of the License at                                   *
*                                                                           *
*    http://www.apache.org/licenses/LICENSE-2.0                             *
*                                                                           *
* Unless required by applicable law or agreed to in writing, software       *
* distributed under the License is distributed on an "AS IS" BASIS,         *
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
* See the License for the specific language governing permissions and       *
* limitations under the License.                                            *
****************************************************************************/
#include "SqlStatementImpl.h"
#include "SqlStatement.h"
#include "SqlTransaction.h"
#include "SqlStorable.h"

namespace metacpp
{
namespace db
{
namespace sql
{

SqlStatementBase::SqlStatementBase()
{

}

SqlStatementBase::~SqlStatementBase()
{

}

std::shared_ptr<connectors::SqlStatementImpl> SqlStatementBase::createImpl(SqlTransaction& transaction)
{
    auto transactionImpl = transaction.impl();
    connectors::SqlStatementImpl *stmt = transactionImpl->createStatement(type(), buildQuery(transaction.connector()->sqlSyntax()));
    if (!stmt)
        throw std::runtime_error("Failed to create statement");
    std::shared_ptr<connectors::SqlStatementImpl> impl(stmt,
        [transactionImpl](connectors::SqlStatementImpl *stmt){ transactionImpl->closeStatement(stmt); });
    return m_impl = impl;
}

SqlStatementSelect::SqlStatementSelect(SqlStorable *storable)
    : m_joinType(JoinTypeNone), m_storable(storable)
{

}

SqlStatementSelect::~SqlStatementSelect()
{

}

SqlStatementType SqlStatementSelect::type() const
{
    return SqlStatementTypeSelect;
}

String SqlStatementSelect::buildQuery(SqlSyntax syntax) const
{
    (void)syntax;
    if (!m_storable->record()->metaObject()->totalFields())
        throw std::runtime_error("Invalid storable");
    String res;
    String tblName = m_storable->record()->metaObject()->name();
    StringArray columns;
    for (size_t i = 0; i < m_storable->record()->metaObject()->totalFields(); ++i)
        columns.push_back(tblName + "." + m_storable->record()->metaObject()->field(i)->name());
    res = "SELECT " + join(columns, ", ") + " FROM " + tblName;
    if (!m_whereClause.empty())
    {
        if (m_joins.size())
        {
            switch (m_joinType)
            {
            case JoinTypeInner:
                res += " INNER JOIN ";
                break;
            case JoinTypeLeftOuter:
                res += " LEFT OUTER JOIN ";
                break;
            default:
                throw std::runtime_error("Unknown join type");
            }

            for (size_t i = 0; i < m_joins.size(); ++i)
            {
                res += m_joins[i]->name();
                if (m_joins.size() - 1 != i) res += ", ";
            }
            res += " ON " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
        }
        else
        {
            res += " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
        }
    }
    if (m_order.size()) res += " ORDER BY " + join(m_order, ", ");
    if (m_limit) res += " LIMIT " + String::fromValue(*m_limit);
    if (m_offset) res += " OFFSET " + String::fromValue(*m_offset);
    return res;
}

SqlStatementSelect &SqlStatementSelect::limit(size_t lim)
{
    m_limit = lim;
    return *this;
}

SqlStatementSelect &SqlStatementSelect::offset(size_t off)
{
    m_offset = off;
    return *this;
}

SqlStatementSelect &SqlStatementSelect::where(const ExpressionNodeWhereClause &whereClause)
{
    m_whereClause = whereClause;
    return *this;
}

SqlResultSet SqlStatementSelect::exec(SqlTransaction &transaction)
{
    SqlResultSet res(transaction, createImpl(transaction), m_storable);
    if (!transaction.impl()->prepare(m_impl.get()))
        throw std::runtime_error("Failed to prepare statement");
    return res;
}

SqlStatementInsert::SqlStatementInsert(SqlStorable *storable)
    : m_storable(storable)
{
}

SqlStatementInsert::~SqlStatementInsert()
{
}

SqlStatementType SqlStatementInsert::type() const
{
    return SqlStatementTypeInsert;
}

String SqlStatementInsert::buildQuery(SqlSyntax syntax) const
{
    (void)syntax;
    // TODO: bind arguments
    if (!m_storable->record()->metaObject()->totalFields())
        throw std::runtime_error("Invalid storable");
    String res;
    String tblName = m_storable->record()->metaObject()->name();
    res = "INSERT INTO " + tblName;
    auto pkey = m_storable->primaryKey();
    StringArray columns, values;
    for (size_t i = 0; i < m_storable->record()->metaObject()->totalFields(); ++i)
    {
        auto field = m_storable->record()->metaObject()->field(i);
        if (field != pkey)
        {
            columns.push_back(field->name());
            values.push_back(m_storable->fieldValue(field));
        }
    }
    res += "(" + join(columns, ", ") + ") VALUES (" + join(values, ", ") + ")";
    return res;
}

int SqlStatementInsert::exec(SqlTransaction &transaction)
{
    createImpl(transaction);
    if (!transaction.impl()->prepare(m_impl.get()))
        throw std::runtime_error("Failed to prepare statement");
    int numRows = 0;
    if (!transaction.impl()->execStatement(m_impl.get(), &numRows))
        throw std::runtime_error("Failed to execute statement");
    transaction.impl()->getLastInsertId(m_impl.get(), m_storable);
    return numRows;
}

SqlStatementUpdate::SqlStatementUpdate(SqlStorable *storable)
    : m_storable(storable)
{
}

SqlStatementUpdate::~SqlStatementUpdate()
{
}

SqlStatementType SqlStatementUpdate::type() const
{
    return SqlStatementTypeUpdate;
}

String SqlStatementUpdate::buildQuery(SqlSyntax syntax) const
{
    // TODO: bind arguments
    if (!m_storable->record()->metaObject()->totalFields())
        throw std::runtime_error("Invalid storable");
    String res;
    String tblName = m_storable->record()->metaObject()->name();
    res = "UPDATE " + tblName;
    auto pkey = m_storable->primaryKey();
    StringArray sets;
    if (!m_sets.size())
    {
        for (size_t i = 0; i < m_storable->record()->metaObject()->totalFields(); ++i)
        {
            auto field = m_storable->record()->metaObject()->field(i);
            if (field != pkey)
                sets.push_back(String(field->name()) + " = " + m_storable->fieldValue(field));
        }
    }
    else
        sets = m_sets;

    if (m_joins.size())
    {
        String joins;
        for (size_t i = 0; i < m_joins.size(); ++i)
        {
            joins += m_joins[i]->name();
            if (i != m_joins.size() - 1)
                joins += ", ";
        }

        if (SqlSyntaxSqlite == syntax)
        {
            res += " SET " + join(sets, ", ") +
                   " WHERE EXISTS (SELECT 1 FROM " + joins;

            if (!m_whereClause.empty()) res += " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk() + ")";
        }
        else if (SqlSyntaxPostgreSQL == syntax)
        {
            res += " SET " + join(sets, ", ") +
                   " FROM " + joins;
            if (!m_whereClause.empty()) res += " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
        }
        else if (SqlSyntaxMySql == syntax)
        {
            res += ", " + joins + " SET " + join(sets, ", ");
            if (!m_whereClause.empty()) res += " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
        }
        else
            throw std::runtime_error("Unimplemented syntax");
    }
    else
        res += " SET " + join(sets, ", ") + " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
    return res;
}

SqlStatementUpdate &SqlStatementUpdate::where(const ExpressionNodeWhereClause &whereClause)
{
    m_whereClause = whereClause;
    return *this;
}

int SqlStatementUpdate::exec(SqlTransaction &transaction)
{
    createImpl(transaction);
    if (!transaction.impl()->prepare(m_impl.get()))
        throw std::runtime_error("Failed to prepare statement");
    int numRows = 0;
    if (!transaction.impl()->execStatement(m_impl.get(), &numRows))
        throw std::runtime_error("Failed to execute statement");
    return numRows;
}

SqlStatementDelete::SqlStatementDelete(SqlStorable *storable)
    : m_storable(storable)
{
}

SqlStatementDelete::~SqlStatementDelete()
{
}

SqlStatementType SqlStatementDelete::type() const
{
    return SqlStatementTypeDelete;
}

String SqlStatementDelete::buildQuery(SqlSyntax syntax) const
{
    String res;
    String tblName = m_storable->record()->metaObject()->name();

    if (m_joins.size())
    {
        StringArray joins = m_joins.template map<String>([](const MetaObject * mo) { return mo->name(); });
        if (SqlSyntaxSqlite == syntax)
        {
            res = "DELETE FROM " + tblName + " WHERE EXISTS (SELECT 1 FROM " + join(joins, ", ");
            if (!m_whereClause.empty()) res += " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
            res += ")";
        }
        else if (SqlSyntaxPostgreSQL == syntax)
        {
            res = "DELETE FROM " + tblName + " USING " + join(joins, ", ");
            if (!m_whereClause.empty()) res += " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
        }
        else // MySql
        {
            res = "DELETE " + tblName + " FROM " + tblName + " JOIN " + join(joins, " JOIN ");
            if (!m_whereClause.empty()) res += " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
        }
    }
    else
    {
        res = "DELETE FROM " + tblName;
        if (!m_whereClause.empty())
            res += " WHERE " + detail::SqlExpressionTreeWalker(m_whereClause.impl(), true, syntax).doWalk();
    }
    return res;

}

SqlStatementDelete &SqlStatementDelete::where(const ExpressionNodeWhereClause &whereClause)
{
    m_whereClause = whereClause;
    return *this;
}

int SqlStatementDelete::exec(SqlTransaction &transaction)
{
    createImpl(transaction);
    if (!transaction.impl()->prepare(m_impl.get()))
        throw std::runtime_error("Failed to prepare statement");
    int numRows = 0;
    if (!transaction.impl()->execStatement(m_impl.get(), &numRows))
        throw std::runtime_error("Failed to execute statement");
    return numRows;
}

SqlStatementCustom::SqlStatementCustom(const String &queryText)
    : m_queryText(queryText)
{

}

SqlStatementCustom::~SqlStatementCustom()
{
}

SqlStatementType SqlStatementCustom::type() const
{
    return SqlStatementTypeUnknown;
}

String SqlStatementCustom::buildQuery(SqlSyntax syntax) const
{
    (void)syntax;
    return m_queryText;
}

void SqlStatementCustom::exec(SqlTransaction &transaction)
{
    createImpl(transaction);
    if (!transaction.impl()->prepare(m_impl.get()))
        throw std::runtime_error("Failed to prepare statement");
    if (!transaction.impl()->execStatement(m_impl.get()))
        throw std::runtime_error("Failed to execute statement");
}

} // namespace sql
} // namespace db
} // namespace metacpp
