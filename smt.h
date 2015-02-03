#ifndef RMC_SMT_H
#define RMC_SMT_H

// Some amount of generic machinery around the SMT solvers.
// We do some violence to make CVC4 have an interface similar to
// z3.

#if USE_Z3

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
  assert(b == Z3_TRUE);
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

#define doCheck(s) s.check()

#elif defined USE_CVC4

typedef __uint64_t smt_uint;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-undefined-compare"
#pragma clang diagnostic ignored "-W#warnings"
#include <cvc4/cvc4.h>
#pragma clang diagnostic pop


class SmtExpr;
class SmtContext;
class SmtSolver;
class SmtSort;
class SmtModel;

static SmtContext &fixCtx(CVC4::ExprManager *e);
static SmtExpr fixExpr(CVC4::Expr e);
static SmtSort fixSort(CVC4::Type e);

class SmtExpr : public CVC4::Expr {
public:
  friend SmtExpr mkExpr(CVC4::Kind kind, SmtExpr const &t1) {
    return fixExpr(t1.getExprManager()->mkExpr(kind, t1));
  }
  friend SmtExpr mkExpr(CVC4::Kind kind, SmtExpr const &t1, SmtExpr const &t2) {
    return fixExpr(t1.getExprManager()->mkExpr(kind, t1, t2));
  }

  SmtContext &ctx() const { return fixCtx(getExprManager()); }
  // TODO: CVC4 needs the engine to simplify, whereas Z3 doesn't, so we
  // don't implement simplify in CVC4. We should probably change how we
  // do simplifying.
  SmtExpr simplify() { return *this; }

  friend SmtExpr ite(SmtExpr const &c, SmtExpr const &t, SmtExpr const &f) {
    return fixExpr(c.iteExpr(t, f));
  }

  friend SmtExpr operator==(SmtExpr const &a, SmtExpr const &b) {
    if (a.getType().isBoolean()) {
      return mkExpr(CVC4::kind::IFF, a, b);
    } else {
      return mkExpr(CVC4::kind::EQUAL, a, b);
    }
  }
  friend SmtExpr operator&&(SmtExpr const &a, SmtExpr const &b) {
    return mkExpr(CVC4::kind::AND, a, b);
  }
  friend SmtExpr operator||(SmtExpr const &a, SmtExpr const &b) {
    return mkExpr(CVC4::kind::OR, a, b);
  }
  friend SmtExpr operator>(SmtExpr const &a, SmtExpr const &b) {
    return mkExpr(CVC4::kind::GT, a, b);
  }
  friend SmtExpr operator<=(SmtExpr const &a, SmtExpr const &b) {
    return mkExpr(CVC4::kind::LEQ, a, b);
  }
  friend SmtExpr operator+(SmtExpr const &a, SmtExpr const &b) {
    return mkExpr(CVC4::kind::PLUS, a, b);
  }
  friend SmtExpr operator*(SmtExpr const &a, SmtExpr const &b) {
    return mkExpr(CVC4::kind::MULT, a, b);
  }
  friend SmtExpr operator!(SmtExpr const &a) {
    return mkExpr(CVC4::kind::NOT, a);
  }


};

class SmtSort : public CVC4::Type {
public:
  SmtContext &ctx() const { return fixCtx(getExprManager()); }
  bool is_bool() const { return isBoolean(); }


};


class SmtContext : public CVC4::ExprManager {
public:
  SmtExpr int_val(int n) { return fixExpr(mkConst(CVC4::Rational(n)));  }
  SmtExpr bool_val(bool b) { return fixExpr(mkConst(b));  }

  SmtSort int_sort() { return fixSort(integerType()); }
  SmtSort bool_sort() { return fixSort(booleanType()); }

  SmtExpr constant(char const *name, SmtSort const &s) {
    return fixExpr(mkVar(name, s));
  }
  SmtExpr int_const(char const *name) { return constant(name, int_sort()); }

};

class SmtModel {
private:
  CVC4::SmtEngine *smtSolver_;
public:
  SmtModel(CVC4::SmtEngine *smtSolver) : smtSolver_(smtSolver) {}
  SmtExpr eval(const SmtExpr &e) { return fixExpr(smtSolver_->getValue(e)); }
};

class SmtSolver : public CVC4::SmtEngine {
public:
  SmtSolver(SmtContext &ctx) : CVC4::SmtEngine(&ctx) {
    setOption("produce-models", true);
  }

  SmtContext &ctx() const { return fixCtx(getExprManager()); }
  SmtModel get_model() { return SmtModel(this); }

  void add(const SmtExpr &e) { assertFormula(e); }
};

static SmtContext &fixCtx(CVC4::ExprManager *e) {
  return static_cast<SmtContext&>(*e);
}
static SmtExpr fixExpr(CVC4::Expr e) { return static_cast<SmtExpr&>(e); }
static SmtSort fixSort(CVC4::Type e) { return static_cast<SmtSort&>(e); }

static bool extractBool(SmtExpr const &e) { return e.getConst<bool>(); }
static int extractInt(SmtExpr const &e) { return e.getConst<CVC4::Rational>().getDouble(); }


static void dumpSolver(SmtSolver &solver) {
  // Don't do anything yet!
}
static void dumpModel(SmtModel &model) {
  // Don't do anything yet!
}


static bool doCheck(SmtSolver &s) {
  CVC4::Result result = s.checkSat();
  assert(!result.isUnknown());
  return result.isSat();
}


#else

#error no smt solver!

#endif



#endif
