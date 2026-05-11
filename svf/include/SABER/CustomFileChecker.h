#ifndef CustomFILECHECK_H_
#define CustomFILECHECK_H_

#include "SABER/LeakChecker.h"

namespace SVF
{

/*!
 * File open/close checker to check consistency of file operations
 */

class CustomFileChecker : public LeakChecker
{

public:

    /// Constructor
    CustomFileChecker(): LeakChecker()
    {
    }

    /// Destructor
    virtual ~CustomFileChecker()
    {
    }

    virtual void initSrcs() override;
    virtual void initSnks() override;
    virtual void analyze() override;

    /// We start from here
    virtual bool runOnModule(SVFIR* pag)
    {
        /// start analysis
        analyze();
        return false;
    }

    inline bool isSourceLikeFun(const FunObjVar* fun)
    {
        return SaberCheckerAPI::getCheckerAPI()->isFOpen(fun);
    }
    /// Whether the function is a heap deallocator (free/release memory)
    inline bool isSinkLikeFun(const FunObjVar* fun)
    {
        return SaberCheckerAPI::getCheckerAPI()->isFClose(fun);
    }
    /// Report file/close bugs
    void reportBug(ProgSlice* slice);
};

} // End namespace SVF

#endif /* CustomFILECHECK_H_ */
