#ifndef OBJECT_H
#define OBJECT_H
#include "MetaObject.h"

namespace metacpp
{

/**
	A basic class for representing objects with metainfo
*/
class Object
{
public:
	virtual ~Object();

	/**
		initializes object with default values from metainfo
		\throws std::invalid_argument
	*/
    void init();
	
	/**
		performs json object serialization
		\throws std::invalid_argument
	*/
    String toString(bool prettyFormatted = true) const;

	/**
		performs json object deserialization
		\throws std::invalid_argument
	*/
    void fromString(const String &s);

	virtual const MetaObject *metaObject() const = 0;
};

#define META_INFO_DECLARE(structName) \
        const MetaObject *metaObject() const override { return &ms_metaObject; } \
        static const MetaObject *staticMetaObject() { return &ms_metaObject; } \
    private: \
        static MetaObject ms_metaObject;

#define META_INFO(structName) \
    MetaObject structName::ms_metaObject(&STRUCT_INFO(structName));

STRUCT_INFO_DECLARE(Object)


/** \brief Usage: getMetaField(&Object::field); */
template<typename TObj, typename TField>
static const MetaField *getMetaField(const TField TObj::*member)
{
    return TObj::staticMetaObject()->fieldByOffset
        (reinterpret_cast<ptrdiff_t>(&(reinterpret_cast<const TObj *>(NULL)->*member)));
}

} // namespace metacpp
#endif // OBJECT_H
