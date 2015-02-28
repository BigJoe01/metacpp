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
#include "PostgresTransactionImpl.h"

namespace metacpp {
namespace sql {
namespace connectors {
namespace postgres {

PostgresTransactionImpl::PostgresTransactionImpl(PGconn *dbConn)
    : m_dbConn(dbConn)
{

}

PostgresTransactionImpl::~PostgresTransactionImpl()
{
    std::lock_guard<std::mutex> _guard(m_statementsMutex);
    if (m_statements.size())
    {
        std::cerr << "There's still " << m_statements.size() <<
                     " unclosed statements while destroing the postgres transaction" << std::endl;
    }
}

bool PostgresTransactionImpl::begin()
{
    return execCommand("BEGIN", "PostgresTransactionImpl::begin()");
}

bool PostgresTransactionImpl::commit()
{
    return execCommand("COMMIT", "PostgresTransactionImpl::commit()");
}

bool PostgresTransactionImpl::rollback()
{
    return execCommand("ROLLBACK", "PostgresTransactionImpl::rollback()");
}

SqlStatementImpl *PostgresTransactionImpl::createStatement(SqlStatementType type, const String& queryText)
{
    std::lock_guard<std::mutex> _guard(m_statementsMutex);
    PostgresStatementImpl *statement = new PostgresStatementImpl(type, queryText);
    m_statements.push_back(statement);
    return statement;
}

bool PostgresTransactionImpl::prepare(SqlStatementImpl *statement)
{
    static int statementId = 0;
    String idString = "metacpp_prepared_stmt_" + String::fromValue(statementId++);
    PGresult *result = PQprepare(m_dbConn, idString.c_str(), statement->queryText().c_str(), 0, nullptr);
    ExecStatusType status = PQresultStatus(result);
    if (PGRES_TUPLES_OK != status && PGRES_COMMAND_OK != status)
    {
        std::cerr << "PQprepare() failed: " << PQresultErrorMessage(result);
        PQclear(result);
        return false;
    }
    PostgresStatementImpl *postgresStatement = reinterpret_cast<PostgresStatementImpl *>(statement);
    postgresStatement->setPrepared();
    postgresStatement->setResult(result, idString);
    return true;
}

bool PostgresTransactionImpl::execStatement(SqlStatementImpl *statement, int *numRowsAffected)
{
    if (!statement->prepared())
        throw std::runtime_error("PostgresTransactionImpl::execStatement(): should be prepared first");
    PostgresStatementImpl *postgresStatement = reinterpret_cast<PostgresStatementImpl *>(statement);
    PGresult *result = PQexecPrepared(m_dbConn, postgresStatement->getIdString().c_str(), 0, NULL, NULL, NULL, 0 /* text format */);
    ExecStatusType status = PQresultStatus(result);
    if (PGRES_TUPLES_OK != status && PGRES_COMMAND_OK != status)
    {
        std::cerr << "PQexecPrepared() failed: " << PQresultErrorMessage(result);
        return false;
    }
    if (numRowsAffected) *numRowsAffected = String(PQcmdTuples(result)).toValue<int>();
    return true;
}

#define _ASSIGN_FIELD(field, type, isNull, val) \
    if (field->nullable() && isNull) {  \
        field->access<Nullable<type> >(storable->record()).reset(); \
    } else { \
        if (field->nullable()) \
            field->access<Nullable<type> >(storable->record()) = val; \
        else \
            field->access<type>(storable->record()) = val; \
    }

template<typename T>
void assignField(const MetaFieldBase *field, bool isNull, const T& val, Object *obj)
{
    if (field->nullable() && !isNull) {
        field->access<Nullable<T> >(obj).reset();
    } else {
        if (field->nullable())
            field->access<Nullable<T> >(obj) = val;
        else
            field->access<T>(obj) = val;
    }
}

bool PostgresTransactionImpl::fetchNext(SqlStatementImpl *statement, SqlStorable *storable)
{
    if (!statement->prepared())
        throw std::runtime_error("PostgresTransactionImpl::execStatement(): should be prepared first");
    if (statement->done())
        return false;
    PostgresStatementImpl *postgresStatement = reinterpret_cast<PostgresStatementImpl *>(statement);
    // increment row
    int currentRow =  postgresStatement->currentRow() + 1;
    postgresStatement->setCurrentRow(currentRow);
    int rowCount = PQntuples(postgresStatement->getResult());
    if (currentRow >= rowCount)
    {
        postgresStatement->setDone();
        return false;
    }
    const int nFields = PQnfields(postgresStatement->getResult());
    for (int i = 0; i < nFields; ++i)
    {
        const char *fName = PQfname(postgresStatement->getResult(), i);
        auto field = storable->record()->metaObject()->fieldByName(fName);
        if (!field)
        {
            std::cerr << "Cannot bind sql result to an object field " << fName << std::endl;
            continue;
        }
        const char *pVal = PQgetvalue(postgresStatement->getResult(), currentRow, i);

        switch (field->type())
        {
        case eFieldBool:
            assignField<bool>(field, !pVal, *pVal == 't', storable->record());
            break;
        case eFieldInt:
            assignField<int32_t>(field, !pVal, String(pVal).toValue<int32_t>(), storable->record());
            break;
        case eFieldEnum:
        case eFieldUint:
            assignField<uint32_t>(field, !pVal, String(pVal).toValue<uint32_t>(), storable->record());
            break;
        case eFieldInt64:
            assignField<int64_t>(field, !pVal, String(pVal).toValue<int64_t>(), storable->record());
            break;
        case eFieldUint64:
            assignField<uint64_t>(field, !pVal, String(pVal).toValue<uint64_t>(), storable->record());
            break;
        case eFieldFloat:
            assignField<float>(field, !pVal, String(pVal).toValue<float>(), storable->record());
            break;
        case eFieldDouble:
            assignField<double>(field, !pVal, String(pVal).toValue<double>(), storable->record());
            break;
        case eFieldString:
            assignField<String>(field, !pVal, String(pVal, PQgetlength(postgresStatement->getResult(), currentRow, i)), storable->record());
            break;
        case eFieldDateTime:
            assignField<DateTime>(field, !pVal, DateTime::fromString(pVal), storable->record());
            break;
        case eFieldObject:
        case eFieldArray:
            throw std::runtime_error("Cannot handle non-plain objects");
        default:
            throw std::runtime_error("Unknown field type");
        }
    }
    return true;
}

bool PostgresTransactionImpl::getLastInsertId(SqlStatementImpl *statement, SqlStorable *storable)
{
    (void)statement;
    auto pkey = storable->primaryKey();
    if (!pkey) return false;
    String query = String("SELECT currval(pg_get_serial_sequence(\'") + storable->record()->metaObject()->name()
            + "\', \'" + pkey->name() + "\'))";
    PGresult *result = PQexec(m_dbConn, query.c_str());
    ExecStatusType status = PQresultStatus(result);
    if (PGRES_TUPLES_OK != status)
    {
        std::cerr << "PostgresTransactionImpl::getLastInsertId(): PQexec() failed: " << PQresultErrorMessage(result);
        PQclear(result);
        return false;
    }
    if (1 != PQntuples(result) || 1 != PQnfields(result))
    {
        std::cerr << "PostgresTransactionImpl::getLastInsertId(): Unexpected number of columns or rows";
        PQclear(result);
        return false;
    }
    const char *res = PQgetvalue(result, 0, 0);
    if (!res)
    {
        std::cerr << "PostgresTransactionImpl::getLastInsertId(): Result is null";
        PQclear(result);
        return false;
    }
    uint64_t lastId = String(res).toValue<uint64_t>();
    switch (pkey->type())
    {
    case eFieldInt:
        pkey->access<int32_t>(storable->record()) = (int32_t)lastId;
        break;
    case eFieldUint:
        pkey->access<uint32_t>(storable->record()) = (uint32_t)lastId;
        break;
    case eFieldInt64:
        pkey->access<int64_t>(storable->record()) = lastId;
        break;
    case eFieldUint64:
        pkey->access<uint64_t>(storable->record()) = lastId;
        break;
    default:
        std::cerr << "PostgresTransactionImpl::getLastInsertId(): Non-integer primary keys are not supported";
        PQclear(result);
        return false;
    }

    PQclear(result);
    return true;
}

bool PostgresTransactionImpl::closeStatement(SqlStatementImpl *statement)
{
    std::lock_guard<std::mutex> _guard(m_statementsMutex);
    PostgresStatementImpl *postgresStatement = reinterpret_cast<PostgresStatementImpl *>(statement);
    auto it = std::find(m_statements.begin(), m_statements.end(), postgresStatement);
    if (it == m_statements.end())
    {
        std::cerr << "PostgresTransactionImpl::closeStatement(): there's no such statement" << std::endl;
        return false;
    }
    m_statements.erase(it);
    delete postgresStatement;
    return true;
}

bool PostgresTransactionImpl::execCommand(const char *query, const char *invokeContext)
{
    PGresult *result = PQexec(m_dbConn, query);
    ExecStatusType status = PQresultStatus(result);
    if (PGRES_TUPLES_OK != status && PGRES_COMMAND_OK != status)
    {
        std::cerr << invokeContext << ": PQexec() failed: " << PQresultErrorMessage(result);
        PQclear(result);
        return false;
    }
    PQclear(result);
    return true;
}

} // namespace postgres
} // namespace connectors
} // namespace sql
} // namespace metacpp
