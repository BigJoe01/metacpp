#ifndef JSONSERIALIZERVISITOR_H
#define JSONSERIALIZERVISITOR_H
#include "VisitorBase.h"
#include <json/json.h>

namespace metacpp
{

class JsonSerializerVisitor :
    public VisitorBase
{
    friend class Object;
public:
    JsonSerializerVisitor(void);
    ~JsonSerializerVisitor(void);

protected:
    void visitField(Object *obj, const MetaField *field) override;
private:
    void appendSubValue(Json::Value& parent, EFieldType type, const void *pValue,
                        const MetaField *desc = nullptr, Json::ArrayIndex = 0);
	const Json::Value& rootValue() const;
private:
	Json::Value m_value;
};

} // namespace metacpp
#endif // JSONSERIALIZERVISITOR_H
