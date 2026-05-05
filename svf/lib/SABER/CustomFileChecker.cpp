#include "SABER/CustomFileChecker.h"

using namespace SVF;
using namespace SVFUtil;

void CustomFileChecker::initSrcs()
{
   SVFIR* pag = getPAG();
    for(SVFIR::CSToRetMap::iterator it = pag->getCallSiteRets().begin(),
            eit = pag->getCallSiteRets().end(); it!=eit; ++it)
    {
        const RetICFGNode* cs = it->first;
        /// if this callsite return reside in a dead function then we do not care about its leaks
        /// for example instruction `int* p = malloc(size)` is in a dead function, then program won't allocate this memory
        if(cs->getFun()->isUncalledFunction())
            continue;

        CallGraph::FunctionSet callees;
        getCallgraph()->getCallees(cs->getCallICFGNode(),callees);
        for(CallGraph::FunctionSet::const_iterator cit = callees.begin(), ecit = callees.end(); cit!=ecit; cit++)
        {
            const FunObjVar* fun = *cit;
            if (isSourceLikeFun(fun))
            {
                CSWorkList worklist;
                SVFGNodeBS visited;
                worklist.push(it->first->getCallICFGNode());
                while (!worklist.empty())
                {
                    const CallICFGNode* cs = worklist.pop();
                    const RetICFGNode* retBlockNode = cs->getRetICFGNode();
                    const ValVar* svfVar = pag->getCallSiteRet(retBlockNode);
                    const SVFGNode* node = getSVFG()->getDefSVFGNode(svfVar);
                    if (visited.test(node->getId()) == 0)
                        visited.set(node->getId());
                    else
                        continue;

                    CallSiteSet csSet;
                    // if this node is in an allocation wrapper, find all its call nodes
                    if (isInAWrapper(node, csSet))
                    {
                        for (CallSiteSet::iterator it = csSet.begin(), eit =
                                    csSet.end(); it != eit; ++it)
                        {
                            worklist.push(*it);
                        }
                    }
                    // otherwise, this is the source we are interested
                    else
                    {
                        // exclude sources in dead functions or sources in functions that have summary
                        if (!cs->getFun()->isUncalledFunction() && !isExtCall(cs->getBB()->getParent()))
                        {
                            addToSources(node);
                            addSrcToCSID(node, cs);
                        }
                    }
                }
            }
        }
    }
}

void CustomFileChecker::initSnks()
{
     SVFIR* pag = getPAG();

    for(SVFIR::CSToArgsListMap::iterator it = pag->getCallSiteArgsMap().begin(),
            eit = pag->getCallSiteArgsMap().end(); it!=eit; ++it)
    {

        CallGraph::FunctionSet callees;
        getCallgraph()->getCallees(it->first,callees);
        for(CallGraph::FunctionSet::const_iterator cit = callees.begin(), ecit = callees.end(); cit!=ecit; cit++)
        {
            const FunObjVar* fun = *cit;
            if (isSinkLikeFun(fun))
            {
                SVFIR::ValVarList &arglist = it->second;
                assert(!arglist.empty()	&& "no actual parameter at deallocation site?");
                /// we only choose pointer parameters among all the actual parameters
                for (SVFIR::ValVarList::const_iterator ait = arglist.begin(),
                        aeit = arglist.end(); ait != aeit; ++ait)
                {
                    const SVFVar *svfVar = *ait;

                    const SVFGNode *snk = getSVFG()->getActualParmVFGNode(svfVar, it->first);
                    addToSinks(snk);

                    // For any multi-level pointer e.g., XFree(void** svfVar) that passed into a ExtAPI::EFT_FREE_MULTILEVEL function (e.g., XFree),
                    // we will add the DstNode of a load edge, i.e., dummy = *svfVar
                    SVFStmt::SVFStmtSetTy& loads = const_cast<SVFVar*>(svfVar)->getOutgoingEdges(SVFStmt::Load);
                    for(const SVFStmt* ld : loads)
                    {
                        if(SVFUtil::isa<DummyValVar>(ld->getDstNode()))
                            addToSinks(getSVFG()->getStmtVFGNode(ld));
                    }
                }
            }
        }
    }
}

void CustomFileChecker::analyze()
{

    initialize();

    ContextCond::setMaxCxtLen(Options::CxtLimit());

    for (SVFGNodeSetIter iter = sourcesBegin(), eiter = sourcesEnd();
            iter != eiter; ++iter)
    {
        setCurSlice(*iter);
        DBOUT(DGENERAL, outs() << "Analysing slice:" << (*iter)->getId() << ")\n");
        ContextCond cxt;
        DPIm item((*iter)->getId(),cxt);
        forwardTraverse(item);

        DBOUT(DSaber, outs() << "Forward process for slice:" << (*iter)->getId() << " (size = " << getCurSlice()->getForwardSliceSize() << ")\n");

        for (SVFGNodeSetIter sit = getCurSlice()->sinksBegin(), esit =
                    getCurSlice()->sinksEnd(); sit != esit; ++sit)
        {
            ContextCond cxt;
            DPIm item((*sit)->getId(),cxt);
            backwardTraverse(item);
        }

        DBOUT(DSaber, outs() << "Backward process for slice:" << (*iter)->getId() << " (size = " << getCurSlice()->getBackwardSliceSize() << ")\n");

        if(Options::DumpSlice())
            annotateSlice(_curSlice);

        if(_curSlice->AllPathReachableSolve())
            _curSlice->setAllReachable();

        DBOUT(DSaber, outs() << "Guard computation for slice:" << (*iter)->getId() << ")\n");
        

        reportBug(getCurSlice());
    }
    finalize();
}


void CustomFileChecker::reportBug(ProgSlice* slice)
{

    if(isAllPathReachable() == false && isSomePathReachable() == false)
    {
        // full leakage
        GenericBug::EventStack eventStack = { SVFBugEvent(SVFBugEvent::SourceInst, getSrcCSID(slice->getSource())) };
        report.addSaberBug(GenericBug::FILENEVERCLOSE, eventStack);
    }
    else if (isAllPathReachable() == false && isSomePathReachable() == true)
    {
        GenericBug::EventStack eventStack;
        slice->evalFinalCond2Event(eventStack);
        eventStack.push_back(
            SVFBugEvent(SVFBugEvent::SourceInst, getSrcCSID(slice->getSource())));
        report.addSaberBug(GenericBug::FILEPARTIALCLOSE, eventStack);
    }
}
