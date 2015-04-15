#ifndef RMC_SMT_H
#define RMC_SMT_H

// Some amount of generic machinery around Z3. A relic from when Z3
// wasn't free software and I wanted to support something else as
// well.

#include <z3++.h>

typedef __uint64 smt_uint;

#if USE_Z3_OPTIMIZER
typedef z3::optimize SmtSolver;
#else
typedef z3::solver SmtSolver;
#endif
typedef z3::expr SmtExpr;
typedef z3::context SmtContext;
typedef z3::sort SmtSort;
typedef z3::model SmtModel;

static bool extractBool(SmtExpr const &e) {
  auto b = Z3_get_bool_value(e.ctx(), e);
  assert(b != Z3_L_UNDEF);
  return b == Z3_L_TRUE;
}
static int extractInt(SmtExpr const &e) {
  int i;
  auto b = Z3_get_numeral_int(e.ctx(), e, &i);
  assert_(b == Z3_TRUE);
  return i;
}

static void dumpSolver(SmtSolver &solver) {
  std::cout << "Built a thing: \n" << solver << "\n\n";
}

static void dumpModel(SmtModel &model) {
  // traversing the model
  for (unsigned i = 0; i < model.size(); ++i) {
    z3::func_decl v = model[i];
    // this problem contains only constants
    assert(v.arity() == 0);
    std::cout << v.name() << " = " << model.get_const_interp(v) << "\n";
  }
}

static bool doCheck(SmtSolver &s) {
  z3::check_result result = s.check();
  assert(result != z3::unknown);
  return result == z3::sat;
}

#endif
