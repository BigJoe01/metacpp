#ifndef SCRIPTENGINEBASE_H
#define SCRIPTENGINEBASE_H
#include "config.h"
#include "ScriptProgramBase.h"
#include <MetaObject.h>
#include <stdexcept>

#ifdef _MSC_VER
#undef _NOEXCEPT
#define _NOEXCEPT
#else
#define _NOEXCEPT noexcept
#endif

namespace metacpp {
namespace scripting {

class ScriptRuntimeError
        : public std::exception
{
public:
    ScriptRuntimeError(const char *message, const char *filename,
                       uint32_t line, uint32_t column);

    const char *what() const _NOEXCEPT override;
private:
    String m_what;
};

/** \brief Base class for all script engine VMs */
class ScriptEngineBase {
public:
    ScriptEngineBase();
    virtual ~ScriptEngineBase();

    SharedObjectPointer<ScriptProgramBase> createProgram();

    void registerClass(const MetaObject *metaObject);

    Array<const MetaObject *> registeredClasses() const;
protected:

    /** \brief Create a new instance of ScriptProgramBase corresponding to this type of VM */
    virtual ScriptProgramBase *createProgramImpl() = 0;
    /** \brief Finalize and destroy ScriptProgramBase previously created by createProgram */
    virtual void closeProgramImpl(ScriptProgramBase *program) = 0;
private:
    Array<const MetaObject *> m_registeredClasses;
};

} // namespace scripting
} // namespace metacpp

#endif // SCRIPTENGINEBASE_H
