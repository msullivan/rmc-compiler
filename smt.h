#ifndef RMC_SMT_H
#define RMC_SMT_H

// Should define in Makefile probs
#define USE_Z3 1
// Should we use Z3's optimizer; this is a #define because it tunes
// what interface we use. Maybe should be defined by Makefile or
// whatever.
#define USE_Z3_OPTIMIZER 1


#if USE_Z3

#include <z3++.h>

#if USE_Z3_OPTIMIZER
typedef z3::optimize SmtSolver;
#else
typedef z3::solver SmtSolver;
#endif
typedef z3::expr SmtExpr;
typedef z3::context SmtContext;
typedef z3::sort SmtSort;

static bool extractBool(SmtExpr const &e) {
  auto b = Z3_get_bool_value(e.ctx(), e);
  assert(b != Z3_L_UNDEF);
  return b == Z3_L_TRUE;
}
static int extractInt(SmtExpr const &e) {
  int i;
  auto b = Z3_get_numeral_int(e.ctx(), e, &i);
  assert(b == Z3_TRUE);
  return i;
}

#else

#error no smt solver!

#endif



#endif
