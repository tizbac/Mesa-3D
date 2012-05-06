
#include "nv50_ir.h"
#include "nv50_ir_target.h"

#include <vector>
#include <limits>

using std::vector;

namespace nv50_ir {

#define BIT_MATRIX_SQRT_SIZE_LIMIT 512

class InstrScheduling : public Pass
{
public:
   InstrScheduling();
   ~InstrScheduling();

   virtual bool visit(Function *);
   virtual bool visit(BasicBlock *);

private:
   struct SchedInfo
   {
      SchedInfo() : insn(NULL), dep(NULL) { }

      Instruction *insn;
      Graph::Node dep;
      int cycle;
      int depCycle; // cycle at which all dependencies are available
   };
   vector<SchedInfo> annot;

   // uses left in BB for each lvalue (infinite if live-out)
   vector<int> usesLeft;

   Graph deps;       // dependency graph
   Graph::Node root; // all eligible instructions are attached to the root
   BitSet depMatrix; // matrix of dependencies (to avoid duplicate edges)

   unsigned int insnCount; // Function::allInsns.getSize()

   Instruction *retired;
   int cycle;

   Instruction *lds[DATA_FILE_COUNT];
   Instruction *sts[DATA_FILE_COUNT];

   int regPressure;
   int regPressureLimit;
   int regUnitLog2;

   struct {
      int sfu;
      int imul;
      int tex;
      int ld[DATA_FILE_COUNT];
      int st[DATA_FILE_COUNT];
   } res;
   bool groupTex;

   const Target *targ;

private:
   void recordMemoryAccess(Instruction *);

   inline SchedInfo& getInfo(const Instruction *);

   void countLValueUses(BasicBlock *);
   void buildDependencyGraph(Instruction *);
   void addDependency(const Instruction *dependency, const Instruction *);
   void dumpDependencyGraph();

   void cleanup();

   void checkRAW(const Instruction *);
   void checkWAR(const Instruction *);
   void checkWAW(const Instruction *);

   void retire(Instruction *);
   Instruction *selectNext();   
};

InstrScheduling::InstrScheduling() : root(NULL)
{
   targ = NULL;
}

InstrScheduling::~InstrScheduling()
{
   // nothing to do
}

void
InstrScheduling::recordMemoryAccess(Instruction *ldst)
{
   DataFile f = ldst->src(0).getFile();
   assert(isMemoryFile(f));

   switch (ldst->op) {
   case OP_LOAD:
      ldst->next = lds[f];
      lds[f] = ldst;
      break;
   case OP_STORE:
      ldst->next = sts[f];
      sts[f] = ldst;
      break;
   case OP_VFETCH: // not writable
   case OP_EXPORT: // not readable
      break;
   default:
      assert(0);
      break;
   }
}

static bool
isBarrier(const Instruction *insn)
{
   switch (insn->op) {
   case OP_BRA:
   case OP_JOIN:
   case OP_CALL:
   case OP_RET:
   case OP_BREAK:
   case OP_CONT:
   case OP_EMIT:
   case OP_RESTART:
   case OP_QUADON:
   case OP_QUADPOP:
   case OP_EXIT:
      return true;
   default:
      if (insn->fixed)
         return true;
      // instructions touching fixed regs should also act as barriers
      for (int s = 0; insn->srcExists(s); ++s)
         if (insn->getSrc(s)->asLValue() && insn->getSrc(s)->reg.data.id >= 0)
            return true;
      for (int d = 0; insn->defExists(d); ++d)
         if (insn->getDef(d)->reg.data.id >= 0)
            return true;
      // TODO: when scheduling after reg alloc, need a way distinguish
      return false;
   }
}

InstrScheduling::SchedInfo&
InstrScheduling::getInfo(const Instruction *insn)
{
   assert(insn);
   return annot.at(insn->id);
}

void
InstrScheduling::addDependency(const Instruction *dep, const Instruction *b)
{
#if 0
   if (dep) {
      INFO("# adding dependency of "); b->print();
      INFO("# on "); dep->print();
   } else {
      INFO("# instruction is free: "); b->print();
   }
#endif
   SchedInfo& ib = getInfo(b);
   if (dep) {
      SchedInfo& ia = getInfo(dep);

      if (insnCount <= BIT_MATRIX_SQRT_SIZE_LIMIT) {
         if (depMatrix.test(b->id * insnCount + dep->id))
            return;
         depMatrix.set(b->id * insnCount + dep->id);
      }

      if (ia.dep.incidentCount()) {
         // not already handled before last barrier
         for (Graph::EdgeIterator ei = ia.dep.outgoing(); !ei.end(); ei.next())
            if (ei.getNode() == &ib.dep)
               return;
         ia.dep.attach(&ib.dep, Graph::Edge::FORWARD);
      }
   } else {
      root.attach(&ib.dep, Graph::Edge::FORWARD);
   }
}

static bool
memoryInterference(const Instruction *a, const Instruction *b)
{
   Symbol *aSym = a->getSrc(0)->asSym();
   Symbol *bSym = b->getSrc(0)->asSym();

   if (aSym->getBase() != bSym->getBase())
      return false;
   if (a->src(0).isIndirect(0) ||
       b->src(0).isIndirect(0))
      return true;
   return aSym->interfers(bSym);
}

void
InstrScheduling::checkRAW(const Instruction *ld)
{
   for (Instruction *rst = sts[ld->src(0).getFile()]; rst; rst = rst->next)
      if (memoryInterference(rst, ld))
         addDependency(rst, ld);
}

void
InstrScheduling::checkWAR(const Instruction *st)
{
   for (Instruction *rld = lds[st->src(0).getFile()]; rld; rld = rld->next)
      if (memoryInterference(rld, st))
         addDependency(rld, st);
}

void
InstrScheduling::checkWAW(const Instruction *st)
{
   for (Instruction *rst = sts[st->src(0).getFile()]; rst; rst = rst->next)
      if (memoryInterference(rst, st))
         addDependency(rst, st);
}

bool
InstrScheduling::visit(Function *fn)
{
   retired = NULL;

   targ = fn->getProgram()->getTarget();

   insnCount = fn->allInsns.getSize();

   // must cut all nodes before reallocating
   cleanup();
   annot.resize(insnCount);

   for (int i = 0; i < fn->allInsns.getSize(); ++i) {
      annot.at(i).insn = reinterpret_cast<Instruction *>(fn->allInsns.get(i));
      annot[i].dep.data = annot[i].insn;
   }

   if (insnCount <= BIT_MATRIX_SQRT_SIZE_LIMIT)
      depMatrix.allocate(insnCount * insnCount, true);

   usesLeft.resize(fn->allLValues.getSize());
   for (size_t i = 0; i < usesLeft.size(); ++i)
      usesLeft.at(i) = -1;

   regUnitLog2 = targ->getFileUnit(FILE_GPR);
   regPressure = 0;
   regPressureLimit = 8;//targ->getMaxConcurrencyRegLimit();

   groupTex = targ->getChipset() >= 0xc0;

   fn->buildLiveSets(); // XXX: GPRs only

   return true;
}

void
InstrScheduling::cleanup()
{
   for (unsigned int f = 0; f < DATA_FILE_COUNT; ++f) {
      lds[f] = NULL;
      sts[f] = NULL;
   }
   // reset the graph (cut all edges)
   deps.~Graph();
   new(&deps) Graph();

   deps.insert(&root);
}

void
InstrScheduling::buildDependencyGraph(Instruction *insn)
{
   // INFO("building dependency graph starting with "); insn->print();

   cleanup();

   assert(insn);
   do {
      Instruction *next = insn->next;
      SchedInfo& sinfo = getInfo(insn);
      sinfo.insn = insn;
      sinfo.dep.data = insn;

      insn->bb->remove(insn);

      // INFO("removing "); insn->print();

      if (insn->op == OP_LOAD) {
         checkRAW(insn);
         recordMemoryAccess(insn);
      } else
      if (insn->op == OP_STORE || insn->op == OP_EXPORT) {
         checkWAW(insn);
         checkWAR(insn);
         recordMemoryAccess(insn);
      }

      for (int s = 0; insn->srcExists(s); ++s) {
         Instruction *srcInsn = insn->getSrc(s)->getInsn();

         // all instructions *must* have a BB assigned, so it will be NULL iff
         // the instruction is from this BB and has already been processed
         if (srcInsn && !srcInsn->bb)
            addDependency(srcInsn, insn);
      }

      if (!sinfo.dep.incidentCount()) {
         addDependency(NULL, insn);
         // neglect latency of dependencies in other BBs
         sinfo.cycle = sinfo.depCycle = 0;
      }

      if (isBarrier(insn))
         break;
      insn = next;
   } while (insn && !isBarrier(insn));

   // dumpDependencyGraph();
}

void
InstrScheduling::dumpDependencyGraph()
{
   for (IteratorRef dfs = deps.iteratorDFS(); !dfs->end(); dfs->next()) {
      Graph::Node *node = reinterpret_cast<Graph::Node *>(dfs->get());

      Instruction *insn = reinterpret_cast<Instruction *>(node->data);
      assert(insn || !node->incidentCount());
      if (!insn)
         continue;

      if (node->incidentCount() == 1 && !node->incident().getNode()->data) {
         INFO("instruction is free: "); insn->print();
         continue;
      }
      INFO("dependencies of "); insn->print();
      for (Graph::EdgeIterator ei = node->incident(); !ei.end(); ei.next()) {
         Instruction *dep = reinterpret_cast<Instruction *>(ei.getNode()->data);
         INFO("   - "); dep->print();
      }
      INFO("\n");
   }
}

void
InstrScheduling::retire(Instruction *insn)
{
   SchedInfo& sinfo = getInfo(insn);
#if 0
   INFO("retiring "); insn->print();
   INFO("=====================================\n");
#endif
   for (Graph::EdgeIterator ei = sinfo.dep.outgoing(); !ei.end(); ei.next()) {
      if (ei.getNode()->incidentCount() == 1) {
         Instruction *insn =
            reinterpret_cast<Instruction *>(ei.getNode()->data);
         root.attach(ei.getNode(), Graph::Edge::TREE);
         getInfo(insn).depCycle = cycle + targ->getLatency(insn);
      }
   }
   sinfo.dep.cut();

   for (int s = 0; insn->srcExists(s); ++s) {
      LValue *lval = insn->getSrc(s)->asLValue();
      if (lval) {
         usesLeft.at(lval->id)--;
         if (usesLeft[lval->id] == 0 && lval->reg.file == FILE_GPR)
            regPressure -= lval->reg.size;
      }
   }
   for (int d = 0; insn->defExists(d); ++d)
      if (insn->getDef(d)->reg.file == FILE_GPR)
         regPressure += insn->getDef(d)->reg.size;

   insn->next = retired;
   retired = insn;

   sinfo.cycle = cycle;

   if (targ->hasSWSched) {
      switch (Target::getOpClass(insn->op)) {
      case OPCLASS_ARITH:
         if ((insn->op == OP_MUL ||
              insn->op == OP_MAD) && !isFloatType(insn->dType))
            res.imul = cycle + 3;
         break;
      case OPCLASS_TEXTURE:
         res.tex = cycle + 3;
         break;
      case OPCLASS_SFU:
         res.sfu = cycle + 3;
         break;
      default:
         break;
      }
      cycle += 1;
   } else {
      cycle += targ->getThroughput(insn);
   }
}

static int
isGlobalDef(const Instruction *insn, int d)
{
   int count = 0;
   for (Value::DefCIterator def = insn->getDef(d)->defs.begin();
        def != insn->getDef(d)->defs.end(); ++def) {
      BasicBlock *bb = (*def)->getInsn()->bb;
      if (bb && bb != insn->bb)
         ++count;
   }
   return count;
}

Instruction *
InstrScheduling::selectNext()
{
   Instruction *best = NULL;
   int maxScore = std::numeric_limits<int>::min();
   const bool byRegPressure = (regPressure >> regUnitLog2) > regPressureLimit;

   for (Graph::EdgeIterator ei = root.outgoing(); !ei.end(); ei.next()) {
      Instruction *insn = reinterpret_cast<Instruction *>(ei.getNode()->data);
      SchedInfo& sinfo = getInfo(insn);
      int score = 0;

      if (byRegPressure) {
         for (int s = 0; insn->srcExists(s); ++s) {
            LValue *lval = insn->getSrc(s)->asLValue();
            if (!lval)
               continue;
            if (!--usesLeft.at(lval->id)) {
               score += 2;
               if (lval->reg.file == FILE_GPR)
                  score += 4 * lval->reg.size;
            }
            Instruction *gen = lval->getInsn();
            if (gen && !gen->bb)
               score += MIN2(16, cycle - getInfo(gen).cycle); // def-use spring
         }
         // reset use counts
         for (int s = 0; insn->srcExists(s); ++s) {
            LValue *lval = insn->getSrc(s)->asLValue();
            if (lval)
               usesLeft[lval->id]++;
         }
         for (int d = 0; insn->defExists(d); ++d) {
            score -= 2;
            if (insn->getDef(d)->reg.file == FILE_GPR)
               score -= 4 * insn->getDef(d)->reg.size;
            if (isGlobalDef(insn, d))
               score -= 8;
         }
      } else {
         score = MIN2((cycle - sinfo.depCycle) * 4, 16);
      }

      // 3rd criterion: many dependencies resolved
      score += sinfo.dep.outgoingCount();

      if (groupTex && isTextureOp(insn->op) && isTextureOp(retired->op))
         score += 64;

      if (score > maxScore) {
         maxScore = score;
         best = insn;
      }
   }
#if 0
   if (likely(root.outgoingCount()))
      return reinterpret_cast<Instruction *>(root.outgoing().getNode()->data);
#endif
   return best;
}

bool
InstrScheduling::visit(BasicBlock *bb)
{
   Instruction *insn, *next;

   cycle = 0;

   memset(&res, 0, sizeof(res));

   countLValueUses(bb);

   regPressure = bb->liveSet.popCount(); // XXX: non-GPRs

   // INFO("going to schedule instructions of BB:%i\n", bb->getId());

   while (bb->getEntry()) {
      buildDependencyGraph(bb->getEntry());
      while ((insn = selectNext()))
         retire(insn);
   }
   for (insn = retired; insn; insn = next) {
      // INFO("inserting "); insn->print();
      // if (insn->prev) { INFO("prev: "); insn->prev->print(); }
      next = insn->next;
      insn->next = NULL;
      bb->insertHead(insn);
   }
   retired = NULL;

   return true;
}

bool
runSchedulingPass(Program *prog)
{
   if (!prog || prog->optLevel < 4)
      return true;
   InstrScheduling sched;
   return sched.run(prog, false, true);
}

void
InstrScheduling::countLValueUses(BasicBlock *bb)
{
   Instruction *insn;
   int s;

   for (insn = bb->getEntry(); insn; insn = insn->next) {
      for (s = 0; insn->srcExists(s); ++s) {
         LValue *src = insn->getSrc(s)->asLValue();
         if (src)
            usesLeft.at(src->id) = 0;
      }
   }
   for (insn = bb->getEntry(); insn; insn = insn->next) {
      for (s = 0; insn->srcExists(s); ++s) {
         LValue *src = insn->getSrc(s)->asLValue();
         if (src && !usesLeft.at(src->id)) {
            for (Value::UseIterator u = src->uses.begin();
                 u != src->uses.end(); ++u) {
               BasicBlock *uBB = (*u)->getInsn()->bb;
               assert(uBB);
               if (uBB == bb)
                  usesLeft[src->id]++;
               else // TODO: compute and use set of live-outs, should be faster
               if (!bb->dominatedBy(uBB) &&
                   uBB->reachableBy(bb, NULL))
                  usesLeft[src->id] = std::numeric_limits<int>::max();
            }
         }
      }
   }
#if 0
   s = 0;
   for (vector<int>::iterator it = usesLeft.begin(); it != usesLeft.end(); ++it, ++s) {
      INFO("%%%i has %i local uses\n", s, *it);
   }
#endif
}

} // namespace nv50_ir
