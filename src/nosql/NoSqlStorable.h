#ifndef NOSQLSTORABLE_H
#define NOSQLSTORABLE_H
#include "Variant.h"

namespace metacpp
{
namespace nosql
{

class NoSqlStorable
{
public:
    NoSqlStorable();
    ~NoSqlStorable();
private:
    Variant m_objectId; // _id implicit field
};

} // namespace nosql
} // namespace metacpp

#endif // NOSQLSTORABLE_H
