/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


/*** normalize
 ***
 *** This pass and function normalizes parsed and scope-resolved AST.
 ***/

#include "astutil.h"
#include "build.h"
#include "expr.h"
#include "passes.h"
#include "stmt.h"
#include "stringutil.h"
#include "symbol.h"
#include <cctype>

bool normalized = false;

//
// Static functions: forward declaration
// 
static void checkUseBeforeDefs();
static void flattenGlobalFunctions();
static void insertUseForExplicitModuleCalls(void);
static void processSyntacticDistributions(CallExpr* call);
static bool is_void_return(CallExpr* call);
static void normalize_returns(FnSymbol* fn);
static void call_constructor_for_class(CallExpr* call);
static void applyGetterTransform(CallExpr* call);
static void insert_call_temps(CallExpr* call);
static void fix_user_assign(CallExpr* call);
static void fix_def_expr(VarSymbol* var);
static void hack_resolve_types(ArgSymbol* arg);
static void fixup_array_formals(FnSymbol* fn);
static void clone_parameterized_primitive_methods(FnSymbol* fn);
static void clone_for_parameterized_primitive_formals(FnSymbol* fn,
                                                      DefExpr* def,
                                                      int width);
static void replace_query_uses(ArgSymbol* formal, DefExpr* def, CallExpr* query,
                               Vec<SymExpr*>& symExprs);
static void add_to_where_clause(ArgSymbol* formal, Expr* expr, CallExpr* query);
static void fixup_query_formals(FnSymbol* fn);
static void change_method_into_constructor(FnSymbol* fn);

void normalize(void) {
  // tag iterators and replace delete statements with calls to ~chpl_destroy
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->isPrimitive(PRIM_YIELD)) {
      FnSymbol* fn = toFnSymbol(call->parentSymbol);
      // violations should have caused USR_FATAL in semanticChecks.cpp
      INT_ASSERT(fn && fn->hasFlag(FLAG_ITERATOR_FN));
    }
    if (call->isPrimitive(PRIM_DELETE)) {
      VarSymbol* tmp = newTemp();
      SET_LINENO(call);
      call->insertBefore(new DefExpr(tmp));
      call->insertBefore(new CallExpr(PRIM_MOVE, tmp, call->get(1)->remove()));
      call->insertBefore(new CallExpr("~chpl_destroy", gMethodToken, tmp));

      CallExpr* freeExpr = (call->numActuals() > 0) ?
        new CallExpr(PRIM_CHPL_FREE, tmp, call->get(1)->remove()) :
        new CallExpr(PRIM_CHPL_FREE, tmp);
      if (fLocal) {
        call->insertBefore(freeExpr);
      } else {
        //
        // if compiling for multiple locales, we need to be sure that the
        // delete is executed on the locale on which the object lives for
        // correctness sake.
        //
        BlockStmt* onStmt = buildOnStmt(new SymExpr(tmp), freeExpr);
        call->insertBefore(onStmt);
      }
      call->remove();
    }
  }

  forv_Vec(FnSymbol, fn, gFnSymbols) {
    SET_LINENO(fn);
    if (!fn->hasFlag(FLAG_TYPE_CONSTRUCTOR) &&
        !fn->hasFlag(FLAG_DEFAULT_CONSTRUCTOR))
      fixup_array_formals(fn);
    clone_parameterized_primitive_methods(fn);
    fixup_query_formals(fn);
    change_method_into_constructor(fn);
  }

  normalize(theProgram);
  normalized = true;
  checkUseBeforeDefs();
  flattenGlobalFunctions();
  insertUseForExplicitModuleCalls();

  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->parentSymbol && call->isPrimitive(PRIM_NEW)) {
      if (CallExpr* classCall = toCallExpr(call->get(1)))
        if (UnresolvedSymExpr* use = toUnresolvedSymExpr(classCall->baseExpr))
          if (isalpha(use->unresolved[0])) {
            USR_FATAL_CONT(call, "invalid use of 'new' on %s", use->unresolved);
            continue;
          }
      USR_FATAL_CONT(call, "invalid use of 'new'");
    }
  }
  USR_STOP();

  // handle side effects on sync/single variables
  forv_Vec(SymExpr, se, gSymExprs) {
    if (isFnSymbol(se->parentSymbol) && se == se->getStmtExpr()) {
      SET_LINENO(se);
      CallExpr* call = new CallExpr("_statementLevelSymbol");
      se->insertBefore(call);
      call->insertAtTail(se->remove());
    }
  }

  forv_Vec(ArgSymbol, arg, gArgSymbols) {
    if (arg->defPoint->parentSymbol)
      hack_resolve_types(arg);
  }

  // perform some checks on destructors
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->hasFlag(FLAG_DESTRUCTOR)) {
      if (fn->formals.length < 2
          || toDefExpr(fn->formals.get(1))->sym->typeInfo() != gMethodToken->typeInfo()) {
        USR_FATAL(fn, "destructors must be methods");
      } else if (fn->formals.length > 2) {
        USR_FATAL(fn, "destructors must not have arguments");
      } else {
        DefExpr* thisDef = toDefExpr(fn->formals.get(2));
        INT_ASSERT(fn->name[0] == '~' && thisDef);
        // make sure the name of the destructor matches the name of the class
        if (strcmp(fn->name + 1, thisDef->sym->type->symbol->name)) {
          USR_FATAL(fn, "destructor name must match class name");
        } else {
          fn->name = astr("~chpl_destroy");
        }
      }
    }
    // make sure methods don't attempt to overload operators
    else if (!isalpha(*fn->name) && *fn->name != '_'
             && fn->formals.length > 1
             && toDefExpr(fn->formals.get(1))->sym->typeInfo() == gMethodToken->typeInfo()) {
      USR_FATAL(fn, "invalid method name");
    }
  }
}

// the following function is called from multiple places,
// e.g., after generating default or wrapper functions
void normalize(BaseAST* base) {
  Vec<CallExpr*> calls;
  collectCallExprs(base, calls);
  forv_Vec(CallExpr, call, calls) {
    processSyntacticDistributions(call);
  }

  Vec<Symbol*> symbols;
  collectSymbols(base, symbols);
  forv_Vec(Symbol, symbol, symbols) {
    if (FnSymbol* fn = toFnSymbol(symbol))
      normalize_returns(fn);
  }

  forv_Vec(Symbol, symbol, symbols) {
    if (VarSymbol* var = toVarSymbol(symbol))
      if (isFnSymbol(var->defPoint->parentSymbol))
        fix_def_expr(var);
  }

  calls.clear();
  collectCallExprs(base, calls);
  forv_Vec(CallExpr, call, calls) {
    applyGetterTransform(call);
    insert_call_temps(call);
    fix_user_assign(call);
  }
  forv_Vec(CallExpr, call, calls) {
    call_constructor_for_class(call);
  }
}

static void
checkUseBeforeDefs() {
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->defPoint->parentSymbol) {
      ModuleSymbol* mod = fn->getModule();
      Vec<const char*> undeclared;
      Vec<Symbol*> undefined;
      Vec<BaseAST*> asts;
      Vec<Symbol*> defined;
      collect_asts_postorder(fn, asts);
      forv_Vec(BaseAST, ast, asts) {
        if (CallExpr* call = toCallExpr(ast)) {
          if (call->isPrimitive(PRIM_MOVE))
            if (SymExpr* se = toSymExpr(call->get(1)))
              defined.set_add(se->var);
        } else if (DefExpr* def = toDefExpr(ast)) {
          if (isArgSymbol(def->sym))
            defined.set_add(def->sym);
        } else if (SymExpr* sym = toSymExpr(ast)) {
          CallExpr* call = toCallExpr(sym->parentExpr);
          if (call && call->isPrimitive(PRIM_MOVE) && call->get(1) == sym)
            continue;
          if (toModuleSymbol(sym->var)) {
            if (!toFnSymbol(fn->defPoint->parentSymbol)) {
              if (!call || !call->isPrimitive(PRIM_USED_MODULES_LIST)) {
                SymExpr* prev = toSymExpr(sym->prev);
                if (!prev || prev->var != gModuleToken)
                  USR_FATAL_CONT(sym, "illegal use of module '%s'", sym->var->name);
              }
            }
          }
          if (isVarSymbol(sym->var) || isArgSymbol(sym->var)) {
            if (sym->var->defPoint->parentExpr != rootModule->block &&
                (sym->var->defPoint->parentSymbol == fn ||
                 (sym->var->defPoint->parentSymbol == mod && mod->initFn == fn))) {
              if (!defined.set_in(sym->var) && !undefined.set_in(sym->var)) {
                if (!sym->var->hasFlag(FLAG_ARG_THIS)) {
                  USR_FATAL_CONT(sym, "'%s' used before defined (first used here)", sym->var->name);
                  undefined.set_add(sym->var);
                }
              }
            }
          }
        } else if (UnresolvedSymExpr* sym = toUnresolvedSymExpr(ast)) {
          CallExpr* call = toCallExpr(sym->parentExpr);
          if (call && call->isPrimitive(PRIM_MOVE) && call->get(1) == sym)
            continue;
          if ((!call || (call->baseExpr != sym && !call->isPrimitive(PRIM_CAPTURE_FN))) && sym->unresolved) {
            if (!undeclared.set_in(sym->unresolved)) {
              if (!toFnSymbol(fn->defPoint->parentSymbol)) {
                USR_FATAL_CONT(sym, "'%s' undeclared (first use this function)",
                               sym->unresolved);
                undeclared.set_add(sym->unresolved);
              }
            }
          }
        }
      }
    }
  }
}

static void
flattenGlobalFunctions() {
  forv_Vec(ModuleSymbol, mod, allModules) {
    for_alist(expr, mod->initFn->body->body) {
      if (DefExpr* def = toDefExpr(expr)) {
        if ((toVarSymbol(def->sym) && !def->sym->hasFlag(FLAG_TEMP)) ||
            toTypeSymbol(def->sym) ||
            toFnSymbol(def->sym)) {
          FnSymbol* fn = toFnSymbol(def->sym);
          if (!fn ||                    // always flatten non-functions
              fn->numFormals() != 0 || // always flatten methods
              !((!strncmp("_forallexpr", def->sym->name, 11)) ||
                def->sym->hasFlag(FLAG_COMPILER_NESTED_FUNCTION))) {
            mod->block->insertAtTail(def->remove());
          }
        }
      }
    }
  }
}

static void
insertUseForExplicitModuleCalls(void) {
  forv_Vec(SymExpr, se, gSymExprs) {
    if (se->parentSymbol && se->var == gModuleToken) {
      CallExpr* call = toCallExpr(se->parentExpr);
      INT_ASSERT(call);
      SymExpr* mse = toSymExpr(call->get(2));
      INT_ASSERT(mse);
      ModuleSymbol* mod = toModuleSymbol(mse->var);
      INT_ASSERT(mod);
      Expr* stmt = se->getStmtExpr();
      BlockStmt* block = new BlockStmt();
      stmt->insertBefore(block);
      block->insertAtHead(stmt->remove());
      block->addUse(mod);
    }
  }
}

static void
processSyntacticDistributions(CallExpr* call) {
  if (call->isPrimitive(PRIM_NEW))
    if (CallExpr* type = toCallExpr(call->get(1)))
      if (SymExpr* base = toSymExpr(type->baseExpr))
        if (base->var->hasFlag(FLAG_SYNTACTIC_DISTRIBUTION)) {
          type->baseExpr->replace(new UnresolvedSymExpr("chpl__buildDistValue"));
          call->replace(type->remove());
        }
  if (call->isNamed("chpl__distributed"))
    if (CallExpr* distCall = toCallExpr(call->get(1)))
      if (SymExpr* distClass = toSymExpr(distCall->baseExpr))
        if (TypeSymbol* ts = toTypeSymbol(distClass->var))
          if (isDistClass(ts->type))
            call->insertAtHead(
              new CallExpr("chpl__buildDistValue",
                new CallExpr(PRIM_NEW, distCall->remove())));
}

static bool is_void_return(CallExpr* call) {
  if (call->isPrimitive(PRIM_RETURN)) {
    SymExpr* arg = toSymExpr(call->argList.first());
    if (arg)
      // NB false for 'return void' in type functions, as it should be
      if (arg->var == gVoid)
        return true;
  }
  return false;
}

static void insertRetMove(FnSymbol* fn, VarSymbol* retval, CallExpr* ret) {
  Expr* ret_expr = ret->get(1);
  ret_expr->remove();
  if (fn->retTag == RET_VAR)
    ret->insertBefore(new CallExpr(PRIM_MOVE, retval, new CallExpr(PRIM_ADDR_OF, ret_expr)));
  else if (fn->retExprType)
    ret->insertBefore(new CallExpr(PRIM_MOVE, retval, new CallExpr("=", retval, ret_expr)));
  else if (!fn->hasFlag(FLAG_WRAPPER) && strcmp(fn->name, "iteratorIndex") &&
           strcmp(fn->name, "iteratorIndexHelp"))
    ret->insertBefore(new CallExpr(PRIM_MOVE, retval, new CallExpr(PRIM_DEREF, ret_expr)));
  else
    ret->insertBefore(new CallExpr(PRIM_MOVE, retval, ret_expr));
}

// Following normalization, each function contains only one return statement
// preceded by a label.  The first half of the function counts the 
// total number of returns and the number of void returns.
// The big IF beginning with if (rets.n == 1) determines if the function
// is already normal.
// The last half of the function performs the normalization steps.
static void normalize_returns(FnSymbol* fn) {
  SET_LINENO(fn);

  CallExpr* theRet = NULL; // Contains the return if it is unique.
  Vec<CallExpr*> rets;
  Vec<CallExpr*> calls;
  int numVoidReturns = 0;
  int numYields = 0;
  bool isIterator = fn->hasFlag(FLAG_ITERATOR_FN);
  collectMyCallExprs(fn, calls, fn); // ones not in a nested function
  forv_Vec(CallExpr, call, calls) {
    if (call->isPrimitive(PRIM_RETURN)) {
      rets.add(call);
      theRet = call;
      if (is_void_return(call))
          numVoidReturns++;
    }
    else if (call->isPrimitive(PRIM_YIELD)) {
      rets.add(call);
      ++numYields;
    }
  }

  // If an iterator, then there is at least one nonvoid return-or-yield.
  INT_ASSERT(!isIterator || rets.n > numVoidReturns); // done in semanticChecks

  // Add a void return if needed.
  // Note this is a bit heavy-handed in view of the code below,
  // marked "Handle declared return type".
  if (rets.n == 0) {
    fn->insertAtTail(new CallExpr(PRIM_RETURN, gVoid));
    return;
  }

  // Check if this function's returns are already normal.
  if (rets.n - numYields == 1) {
    if (theRet == fn->body->body.last()) {
      if (SymExpr* se = toSymExpr(theRet->get(1))) {
        if (fn->hasFlag(FLAG_CONSTRUCTOR) ||
            fn->hasFlag(FLAG_TYPE_CONSTRUCTOR) ||
            !strncmp("_if_fn", fn->name, 6) ||
            !strcmp("=", fn->name) ||
            !strcmp("_init", fn->name) ||
            !strcmp("_ret", se->var->name)) {
          return;   // Yup.
        }
      }
    }
  }

  LabelSymbol* label = new LabelSymbol(astr("_end_", fn->name));
  fn->insertAtTail(new DefExpr(label));
  VarSymbol* retval = NULL;
  // If a proc has a void return, do not return any values ever.
  // (Types are not resolved yet, so we judge by presence of "void returns"
  // i.e. returns with no expr. See also a related check in semanticChecks.)
  if (!isIterator && (numVoidReturns != 0)) {
    fn->insertAtTail(new CallExpr(PRIM_RETURN, gVoid));
  } else {
    // Handle declared return type.
    retval = newTemp("_ret", fn->retType);
    if (fn->retTag == RET_PARAM)
      retval->addFlag(FLAG_PARAM);
    if (fn->retTag == RET_TYPE)
      retval->addFlag(FLAG_TYPE_VARIABLE);
    if (fn->hasFlag(FLAG_MAYBE_TYPE))
      retval->addFlag(FLAG_MAYBE_TYPE);
    // If the function has a specified return type (and is not a var function),
    // declare and initialize the return value up front,
    // and set the specified_return_type flag.
    if (fn->retExprType && fn->retTag != RET_VAR) {
      BlockStmt* retExprType = fn->retExprType->copy();
      if (isIterator)
        if (SymExpr* lastRTE = toSymExpr(retExprType->body.tail))
          if (TypeSymbol* retSym = toTypeSymbol(lastRTE->var))
            if (retSym->type == dtVoid)
              USR_FATAL_CONT(fn, "an iterator's return type cannot be 'void'; if specified, it must be the type of the expressions the iterator yields");
      fn->insertAtHead(new CallExpr(PRIM_MOVE, retval, new CallExpr(PRIM_INIT, retExprType->body.tail->remove())));
      fn->insertAtHead(retExprType);
      fn->addFlag(FLAG_SPECIFIED_RETURN_TYPE);
    }
    fn->insertAtHead(new DefExpr(retval));
    fn->insertAtTail(new CallExpr(PRIM_RETURN, retval));
  }

  // Now, for each return statement appearing in the function body,
  // move the value of its body into the declared return value.
  bool label_is_used = false;
  forv_Vec(CallExpr, ret, rets) {
    SET_LINENO(ret);
    if (isIterator) {
      INT_ASSERT(!!retval);

      // Three cases: 
      // (1) yield expr; => mov _ret expr; yield _ret;
      // (2) return; => goto end_label;
      // (3) return expr; -> mov _ret expr; yield _ret; goto end_label;
      // Notice how (3) is the composition of (1) and (2).
      if (!is_void_return(ret)) { // Cases 1 and 3
        // insert MOVE(retval,ret_expr)
        insertRetMove(fn, retval, ret);
        // insert YIELD(retval)
        ret->insertBefore(new CallExpr(PRIM_YIELD, retval));
      }
      if (ret->isPrimitive(PRIM_YIELD)) // Case 1 only.
          // it's a yield => no goto; need to remove the original node
          ret->remove();
      else {    // Cases 2 and 3.
        if (ret->next != label->defPoint) {
          ret->replace(new GotoStmt(GOTO_RETURN, label));
          label_is_used = true;
        } else {
          ret->remove();
        }
      }
    } else {
      // Not an iterator
      if (retval) {
        // insert MOVE(retval,ret_expr)
        insertRetMove(fn, retval, ret);
      }
      // replace with GOTO(label)
      if (ret->next != label->defPoint) {
        ret->replace(new GotoStmt(GOTO_RETURN, label));
        label_is_used = true;
      } else {
        ret->remove();
      }
    }
  }
  if (!label_is_used)
    label->defPoint->remove();
}

static void call_constructor_for_class(CallExpr* call) {
  if (SymExpr* se = toSymExpr(call->baseExpr)) {
    if (TypeSymbol* ts = toTypeSymbol(se->var)) {
      if (ClassType* ct = toClassType(ts->type)) {
        SET_LINENO(call);

        // These tests can be moved up to a general ClassType object verifier.
        if (!ct->initializer)
          INT_FATAL(call, "class type has no initializer");
        if (!ct->defaultTypeConstructor)
          INT_FATAL(call, "class type has no default type constructor");

        CallExpr* parent = toCallExpr(call->parentExpr);
        CallExpr* parentParent = NULL;
        if (parent)
          parentParent = toCallExpr(parent->parentExpr);
        if (parent && parent->isPrimitive(PRIM_NEW)) {
          se->replace(new UnresolvedSymExpr(ct->initializer->name));
          parent->replace(call->remove());
        } else if (parentParent && parentParent->isPrimitive(PRIM_NEW) &&
                   call->partialTag) {
          se->replace(new UnresolvedSymExpr(ct->initializer->name));
          parentParent->replace(parent->remove());
        } else {
          if (ct->symbol->hasFlag(FLAG_SYNTACTIC_DISTRIBUTION))
            se->replace(new UnresolvedSymExpr("chpl__buildDistType"));
          else
            se->replace(new UnresolvedSymExpr(ct->defaultTypeConstructor->name));
        }
      }
    }
  }
}

static void applyGetterTransform(CallExpr* call) {
  // Most generally:
  //   x.f(a) --> f(_mt, x)(a)
  // which is the same as
  //   call(call(. x "f") a) --> call(call(f _mt x) a)
  // Also:
  //   x.f --> f(_mt, x)
  // Note:
  //   call(call or )( indicates partial
  if (call->isNamed(".")) {
    SET_LINENO(call);
    SymExpr* symExpr = toSymExpr(call->get(2));
    INT_ASSERT(symExpr);
    symExpr->remove();
    if (VarSymbol* var = toVarSymbol(symExpr->var)) {
      if (var->immediate->const_kind == CONST_KIND_STRING) {
        call->baseExpr->replace(new UnresolvedSymExpr(var->immediate->v_string));
        call->insertAtHead(gMethodToken);
      } else {
        INT_FATAL(call, "unexpected case");
      }
    } else if (TypeSymbol* type = toTypeSymbol(symExpr->var)) {
      call->baseExpr->replace(new SymExpr(type));
      call->insertAtHead(gMethodToken);
    } else {
      INT_FATAL(call, "unexpected case");
    }
    call->methodTag = true;
    if (CallExpr* parent = toCallExpr(call->parentExpr))
      if (parent->baseExpr == call)
        call->partialTag = true;
  }
}

static void insert_call_temps(CallExpr* call) {
  if (!call->parentExpr || !call->getStmtExpr())
    return;

  if (call == call->getStmtExpr())
    return;
  
  if (toDefExpr(call->parentExpr))
    return;

  if (call->partialTag)
    return;

  if (call->isPrimitive(PRIM_TUPLE_EXPAND) ||
      call->isPrimitive(PRIM_GET_MEMBER_VALUE))
    return;

  CallExpr* parentCall = toCallExpr(call->parentExpr);
  if (parentCall && (parentCall->isPrimitive(PRIM_MOVE) ||
                     parentCall->isPrimitive(PRIM_NEW)))
    return;

  SET_LINENO(call);
  Expr* stmt = call->getStmtExpr();
  VarSymbol* tmp = newTemp();
  if (!parentCall || !parentCall->isNamed("chpl__initCopy"))
    tmp->addFlag(FLAG_EXPR_TEMP);
  if (call->isPrimitive(PRIM_NEW))
    tmp->addFlag(FLAG_INSERT_AUTO_DESTROY_FOR_EXPLICIT_NEW);
  tmp->addFlag(FLAG_MAYBE_PARAM);
  tmp->addFlag(FLAG_MAYBE_TYPE);
  call->replace(new SymExpr(tmp));
  stmt->insertBefore(new DefExpr(tmp));
  stmt->insertBefore(new CallExpr(PRIM_MOVE, tmp, call));
}

static void fix_user_assign(CallExpr* call) {
  if (!call->parentExpr ||
      call->getStmtExpr() == call->parentExpr ||
      !call->isNamed("="))
    return;
  SET_LINENO(call);
  CallExpr* move = new CallExpr(PRIM_MOVE, call->get(1)->copy());
  call->replace(move);
  move->insertAtTail(call);
}

//
// fix_def_expr removes DefExpr::exprType and DefExpr::init from a
//   variable's def expression, normalizing the AST with primitive
//   moves, calls to chpl__initCopy, _init, and _cast, and assignments.
//
static void
fix_def_expr(VarSymbol* var) {
  SET_LINENO(var);

  Expr* type = var->defPoint->exprType;
  Expr* init = var->defPoint->init;
  Expr* stmt = var->defPoint; // insertion point
  VarSymbol* constTemp = var; // temp for constants

  if (!type && !init)
    return; // already fixed

  //
  // add "insert auto destroy" pragma to user variables that should be
  // auto destroyed
  //
  FnSymbol* fn = toFnSymbol(var->defPoint->parentSymbol);
  INT_ASSERT(fn);
  if (!var->hasFlag(FLAG_NO_AUTO_DESTROY) &&
      !var->hasFlag(FLAG_PARAM) &&
      var->defPoint->parentExpr != fn->getModule()->initFn->body &&
      strcmp(fn->name, "chpl__initCopy") &&
      fn->_this != var &&
      !fn->hasFlag(FLAG_TYPE_CONSTRUCTOR))
    var->addFlag(FLAG_INSERT_AUTO_DESTROY);

  //
  // handle "no copy" variables
  //
  if (var->hasFlag(FLAG_NO_COPY)) {
    INT_ASSERT(init);
    INT_ASSERT(!type);
    stmt->insertAfter(new CallExpr(PRIM_MOVE, var, init->remove()));
    return;
  }

  //
  // handle type aliases
  //
  if (var->hasFlag(FLAG_TYPE_VARIABLE)) {
    INT_ASSERT(init);
    INT_ASSERT(!type);
    stmt->insertAfter(new CallExpr(PRIM_MOVE, var, new CallExpr("chpl__typeAliasInit", init->remove())));
    return;
  }

  //
  // handle var ... : ... => ...;
  //
  if (var->hasFlag(FLAG_ARRAY_ALIAS)) {
    CallExpr* partial;
    if (!type) {
      partial = new CallExpr("newAlias", gMethodToken, init->remove());
      //      partial->partialTag = true;
      //      partial->methodTag = true;
      stmt->insertAfter(new CallExpr(PRIM_MOVE, var, new CallExpr("chpl__autoCopy", partial)));
    } else {
      partial = new CallExpr("reindex", gMethodToken, init->remove());
      partial->partialTag = true;
      partial->methodTag = true;
      stmt->insertAfter(new CallExpr(PRIM_MOVE, var, new CallExpr("chpl__autoCopy", new CallExpr(partial, type->remove()))));
    }
    return;
  }

  //
  // insert temporary for constants to assist constant checking
  //
  if (var->hasFlag(FLAG_CONST) && !var->hasFlag(FLAG_EXTERN)) {
    constTemp = newTemp();
    stmt->insertBefore(new DefExpr(constTemp));
    stmt->insertAfter(new CallExpr(PRIM_MOVE, var, constTemp));
  }

  //
  // insert code to initialize config variable from the command line
  //
  if (var->hasFlag(FLAG_CONFIG)) {
    if (!var->hasFlag(FLAG_PARAM)) {
      Expr* noop = new CallExpr(PRIM_NOOP);
      Symbol* module_name = (var->getModule()->modTag != MOD_INTERNAL ?
                             new_StringSymbol(var->getModule()->name) :
                             new_StringSymbol("Built-in"));
      CallExpr* strToValExpr =
        new CallExpr("_command_line_cast",
                     new SymExpr(new_StringSymbol(var->name)),
                     new CallExpr(PRIM_TYPEOF, constTemp),
                     new CallExpr("chpl_config_get_value",
                                  new_StringSymbol(var->name),
                                  module_name));
      stmt->insertAfter(
        new CondStmt(
          new CallExpr("!",
                       new CallExpr("chpl_config_has_value",
                                    new_StringSymbol(var->name),
                                    module_name)),
          noop,
          new CallExpr(PRIM_MOVE, constTemp, strToValExpr)));

      stmt = noop; // insert regular definition code in then block
    }
  }

  if (type) {

    //
    // use cast for parameters to avoid multiple parameter assignments
    //
    if (init && var->hasFlag(FLAG_PARAM)) {
      stmt->insertAfter(
        new CallExpr(PRIM_MOVE, var,
          new CallExpr("_cast", type->remove(), init->remove())));
      return;
    }

    //
    // initialize variable based on specified type and then assign it
    // the initialization expression if it exists
    //
    VarSymbol* typeTemp = newTemp();
    stmt->insertBefore(new DefExpr(typeTemp));
    stmt->insertBefore(
      new CallExpr(PRIM_MOVE, typeTemp,
        new CallExpr(PRIM_INIT, type->remove())));
    if (init) {
      stmt->insertAfter(new CallExpr(PRIM_MOVE, constTemp, typeTemp));
      stmt->insertAfter(
        new CallExpr(PRIM_MOVE, typeTemp,
          new CallExpr("=", typeTemp, init->remove())));
    } else {
      if (constTemp->hasFlag(FLAG_TYPE_VARIABLE))
        stmt->insertAfter(new CallExpr(PRIM_MOVE, constTemp, new CallExpr(PRIM_TYPEOF, typeTemp)));
      else {
        CallExpr* moveToConst = new CallExpr(PRIM_MOVE, constTemp, typeTemp);
        Expr* newExpr = moveToConst;
        if (var->hasFlag(FLAG_EXTERN)) {
          newExpr = new BlockStmt(moveToConst, BLOCK_TYPE);
        }
        stmt->insertAfter(newExpr);
      }
    }

  } else {

    //
    // initialize untyped variable with initialization expression
    //
    // sjd: this new specialization of PRIM_NEW addresses the test
    //         test/classes/diten/test_destructor.chpl
    //      in which we call an explicit record destructor and avoid
    //      calling the default constructor.  However, if written with
    //      an explicit type, this would happen.  The record in this
    //      test is an issue since its destructor deletes field c, but
    //      the default constructor does not 'new' it.  Thus if we
    //      pass the record to a function and it is copied, we have an
    //      issue since we will do a double free.
    //
    CallExpr* initCall = toCallExpr(init);
    if (initCall && initCall->isPrimitive(PRIM_NEW))
      stmt->insertAfter(new CallExpr(PRIM_MOVE, constTemp, init->remove()));
    else
      stmt->insertAfter(
        new CallExpr(PRIM_MOVE, constTemp,
          new CallExpr("chpl__initCopy", init->remove())));

  }
}

static void hack_resolve_types(ArgSymbol* arg) {
  // Look only at unknown or arbitrary types.
  if (arg->type == dtUnknown || arg->type == dtAny) {
    if (!arg->typeExpr) {
      if (!arg->hasFlag(FLAG_TYPE_VARIABLE) && arg->defaultExpr) {
        SymExpr* se = NULL;
        if (arg->defaultExpr->body.length == 1)
          se = toSymExpr(arg->defaultExpr->body.tail);
        if (!se || se->var != gTypeDefaultToken) {
          arg->typeExpr = arg->defaultExpr->copy();
          insert_help(arg->typeExpr, NULL, arg);
        }
      }
    } else {
      INT_ASSERT(arg->typeExpr);

      // If there is a simple type expression, and its type is something more specific than
      // dtUnknown or dtAny, then replace the type expression with that type.
      // hilde sez: don't we lose information here?
      if (arg->typeExpr->body.length == 1) {
        Type* type = arg->typeExpr->body.only()->typeInfo();
        if (type != dtUnknown && type != dtAny) {
          // This test ensures that we are making progress.
          arg->type = type;
          arg->typeExpr->remove();
        }
      }
    }
  }
}

// Replaces formals whose type is computed by chpl__buildArrayRuntimeType
// with the generic _array type.
// I think this prepares the function to be instantiated with various argument types.
// That is, it reaches through one level in the type hierarchy -- treating all
// arrays equally and then resolving using the element type.
// But this is something of a kludge.  The expansion of arrays 
// w.r.t. generic argument types should be done during expansion and resolution,
// not up front like this. <hilde>
static void fixup_array_formals(FnSymbol* fn) {
  for_formals(arg, fn) {
    INT_ASSERT(toArgSymbol(arg));
    if (arg->typeExpr) {
      // The argument has a type expression
      CallExpr* call = toCallExpr(arg->typeExpr->body.tail);
      // Not sure why we select the tail here....

      //if (call && call->isNamed("chpl__buildDomainExpr")) {
        //CallExpr* arrayTypeCall = new CallExpr("chpl__buildArrayRuntimeType");
        //call->insertBefore(arrayTypeCall);
        //arrayTypeCall->insertAtTail(call->remove());
        //call = arrayTypeCall;
      //}
      if (call && call->isNamed("chpl__buildArrayRuntimeType")) {
        // We are building an array type.
        bool noDomain = (isSymExpr(call->get(1))) ? toSymExpr(call->get(1))->var == gNil : false;
        DefExpr* queryDomain = toDefExpr(call->get(1));
        bool noEltType = (call->numActuals() == 1);
        DefExpr* queryEltType = (!noEltType) ? toDefExpr(call->get(2)) : NULL;

        // Replace the type expression with "_array" to make it generic.
        arg->typeExpr->replace(new BlockStmt(new SymExpr(dtArray->symbol), BLOCK_TYPE));

        Vec<SymExpr*> symExprs;
        collectSymExprs(fn, symExprs);

        // If we have an element type, replace reference to its symbol with
        // "arg.eltType", so we use the instantiated element type.
        if (queryEltType) {
          forv_Vec(SymExpr, se, symExprs) {
            if (se->var == queryEltType->sym)
              se->replace(new CallExpr(".", arg, new_StringSymbol("eltType")));
          }
        } else if (!noEltType) {
          // The element type is supplied, but it is null.
          // Add a new where clause "eltType == arg.eltType".
          INT_ASSERT(queryEltType == NULL);
          if (!fn->where) {
            fn->where = new BlockStmt(new SymExpr(gTrue));
            insert_help(fn->where, NULL, fn);
          }
          Expr* oldWhere = fn->where->body.tail;
          CallExpr* newWhere = new CallExpr("&");
          oldWhere->replace(newWhere);
          newWhere->insertAtTail(oldWhere);
          newWhere->insertAtTail(
            new CallExpr("==", call->get(2)->remove(),
                         new CallExpr(".", arg, new_StringSymbol("eltType"))));
        }

        if (queryDomain) {
          // Array type is built using a domain.
          // If we match the domain symbol, replace it with arg._dom.
          forv_Vec(SymExpr, se, symExprs) {
            if (se->var == queryDomain->sym)
              se->replace(new CallExpr(".", arg, new_StringSymbol("_dom")));
          }
        } else if (!noDomain) {
          // The domain argument is supplied but NULL.
          INT_ASSERT(queryDomain == NULL);

          VarSymbol* tmp = newTemp("_reindex");
          tmp->addFlag(FLAG_EXPR_TEMP);
          forv_Vec(SymExpr, se, symExprs) {
            if (se->var == arg)
              se->var = tmp;
          }
          // tmp <- arg.reindex(arg->typeExpr)
          fn->insertAtHead(new CallExpr(PRIM_MOVE, tmp,
                                        new CallExpr(
                                          new CallExpr(".", arg,
                                                       new_StringSymbol("reindex")),
                                          call->get(1)->copy())));
          fn->insertAtHead(new DefExpr(tmp));
        }
      }
    }
  }
}

static void clone_parameterized_primitive_methods(FnSymbol* fn) {
  if (toArgSymbol(fn->_this)) {
    /* The following works but is not currently necessary:
    if (fn->_this->type == dtIntegral) {
      for (int i=INT_SIZE_1; i<INT_SIZE_NUM; i++) {
        if (dtInt[i]) { // Need this because of our bogus dtInt sizes
          FnSymbol* nfn = fn->copy();
          nfn->_this->type = dtInt[i];
          fn->defPoint->insertBefore(new DefExpr(nfn));
        }
      }
      for (int i=INT_SIZE_1; i<INT_SIZE_NUM; i++) {
        if (dtUInt[i]) { // Need this because of our bogus dtUint sizes
          FnSymbol* nfn = fn->copy();
          nfn->_this->type = dtUInt[i];
          fn->defPoint->insertBefore(new DefExpr(nfn));
        }
      }
      fn->defPoint->remove();
    }
    */
    if (fn->_this->type == dtAnyComplex) {
      for (int i=COMPLEX_SIZE_32; i<COMPLEX_SIZE_NUM; i++) {
        if (dtComplex[i]) { // Need this because of our bogus dtComplex sizes
          FnSymbol* nfn = fn->copy();
          nfn->_this->type = dtComplex[i];
          fn->defPoint->insertBefore(new DefExpr(nfn));
        }
      }
      fn->defPoint->remove();
    }
  }
}

static void
clone_for_parameterized_primitive_formals(FnSymbol* fn,
                                          DefExpr* def,
                                          int width) {
  SymbolMap map;
  FnSymbol* newfn = fn->copy(&map);
  Symbol* newsym = map.get(def->sym);
  newsym->defPoint->replace(new SymExpr(new_IntSymbol(width)));
  Vec<SymExpr*> symExprs;
  collectSymExprs(newfn, symExprs);
  forv_Vec(SymExpr, se, symExprs) {
    if (se->var == newsym)
      se->var = new_IntSymbol(width);
  }
  fn->defPoint->insertAfter(new DefExpr(newfn));
  fixup_query_formals(newfn);
}

static void
replace_query_uses(ArgSymbol* formal, DefExpr* def, CallExpr* query,
                   Vec<SymExpr*>& symExprs) {
  forv_Vec(SymExpr, se, symExprs) {
    if (se->var == def->sym) {
      if (formal->variableExpr) {
        CallExpr* parent = toCallExpr(se->parentExpr);
        if (!parent || parent->numActuals() != 1)
          USR_FATAL(se, "illegal access to query type or parameter");
        se->replace(new SymExpr(formal));
        parent->replace(se);
        CallExpr* myQuery = query->copy();
        myQuery->insertAtHead(parent);
        se->replace(myQuery);
      } else {
        CallExpr* myQuery = query->copy();
        myQuery->insertAtHead(formal);
        se->replace(myQuery);
      }
    }
  }
}

static void
add_to_where_clause(ArgSymbol* formal, Expr* expr, CallExpr* query) {
  FnSymbol* fn = formal->defPoint->getFunction();
  if (!fn->where) {
    fn->where = new BlockStmt(new SymExpr(gTrue));
    insert_help(fn->where, NULL, fn);
  }
  Expr* where = fn->where->body.tail;
  CallExpr* clause;
  query->insertAtHead(formal);
  if (formal->variableExpr) {
    clause = new CallExpr(PRIM_TUPLE_AND_EXPAND);
    while (query->numActuals())
      clause->insertAtTail(query->get(1)->remove());
    clause->insertAtTail(expr->copy());
  } else {
    clause = new CallExpr("==", expr->copy(), query);
  }
  where->replace(new CallExpr("&", where->copy(), clause));
}

static void
fixup_query_formals(FnSymbol* fn) {
  for_formals(formal, fn) {
    if (!formal->typeExpr)
      continue;
    if (DefExpr* def = toDefExpr(formal->typeExpr->body.tail)) {
      Vec<SymExpr*> symExprs;
      collectSymExprs(fn, symExprs);
      forv_Vec(SymExpr, se, symExprs) {
        if (se->var == def->sym)
          se->replace(new CallExpr(PRIM_TYPEOF, formal));
      }
      // Consider saving as origTypeExpr instead?
      formal->typeExpr->remove();
      formal->type = dtAny;
    } else if (CallExpr* call = toCallExpr(formal->typeExpr->body.tail)) {
      // clone query primitive types
      SymExpr* callFnSymExpr = toSymExpr(call->baseExpr);
      if (callFnSymExpr && call->numActuals() == 1) {
        Symbol* callFnSym = callFnSymExpr->var;
        if (DefExpr* def = toDefExpr(call->get(1))) {
          if (callFnSym == dtBools[BOOL_SIZE_DEFAULT]->symbol) {
            for (int i=BOOL_SIZE_8; i<BOOL_SIZE_NUM; i++)
              if (dtBools[i]) {
                clone_for_parameterized_primitive_formals(fn, def,
                                                          get_width(dtBools[i]));
              }
            fn->defPoint->remove();
            return;
          } else if (callFnSym == dtInt[INT_SIZE_DEFAULT]->symbol || 
                     callFnSym == dtUInt[INT_SIZE_DEFAULT]->symbol) {
            for( int i=INT_SIZE_1; i<INT_SIZE_NUM; i++)
              if (dtInt[i])
                clone_for_parameterized_primitive_formals(fn, def,
                                                          get_width(dtInt[i]));
            fn->defPoint->remove();
            return;
          } else if (callFnSym == dtReal[FLOAT_SIZE_DEFAULT]->symbol ||
                     callFnSym == dtImag[FLOAT_SIZE_DEFAULT]->symbol) {
            for( int i=FLOAT_SIZE_16; i<FLOAT_SIZE_NUM; i++)
              if (dtReal[i])
                clone_for_parameterized_primitive_formals(fn, def,
                                                          get_width(dtReal[i]));
            fn->defPoint->remove();
            return;
          } else if (callFnSym == dtComplex[COMPLEX_SIZE_DEFAULT]->symbol) {
            for( int i=COMPLEX_SIZE_32; i<COMPLEX_SIZE_NUM; i++)
              if (dtComplex[i])
                clone_for_parameterized_primitive_formals(fn, def,
                                                          get_width(dtComplex[i]));
            fn->defPoint->remove();
            return;
          }
        }
      }
      bool queried = false;
      for_actuals(actual, call) {
        if (toDefExpr(actual))
          queried = true;
        if (NamedExpr* named = toNamedExpr(actual))
          if (toDefExpr(named->actual))
            queried = true;
      }
      if (queried) {
        bool isTupleType = false;
        Vec<SymExpr*> symExprs;
        collectSymExprs(fn, symExprs);
        if (call->isNamed("_build_tuple")) {
          add_to_where_clause(formal, new SymExpr(new_IntSymbol(call->numActuals())), new CallExpr(PRIM_QUERY, new_StringSymbol("size")));
          call->baseExpr->replace(new SymExpr(dtTuple->symbol));
          isTupleType = true;
        }
        CallExpr* positionQueryTemplate = new CallExpr(PRIM_QUERY);
        for_actuals(actual, call) {
          if (NamedExpr* named = toNamedExpr(actual)) {
            positionQueryTemplate->insertAtTail(new_StringSymbol(named->name));
            CallExpr* nameQuery = new CallExpr(PRIM_QUERY, new_StringSymbol(named->name));
            if (DefExpr* def = toDefExpr(named->actual)) {
              replace_query_uses(formal, def, nameQuery, symExprs);
            } else {
              add_to_where_clause(formal, named->actual, nameQuery);
            }
          }
        }
        int position = (isTupleType) ? 2 : 1; // size is first for tuples
        for_actuals(actual, call) {
          if (!toNamedExpr(actual)) {
            CallExpr* positionQuery = positionQueryTemplate->copy();
            positionQuery->insertAtTail(new_IntSymbol(position));
            if (DefExpr* def = toDefExpr(actual)) {
              replace_query_uses(formal, def, positionQuery, symExprs);
            } else {
              add_to_where_clause(formal, actual, positionQuery);
            }
            position++;
          }
        }
        formal->typeExpr->replace(new BlockStmt(call->baseExpr->remove()));
        formal->markedGeneric = true;
      }
    }
  }
}

static void change_method_into_constructor(FnSymbol* fn) {
  if (fn->numFormals() <= 1)
    return;

  // This function must be a method.
  if (fn->getFormal(1)->type != dtMethodToken)
    return;

  // The second argument is 'this'.
  // For starters, it needs a known type.
  if (fn->getFormal(2)->type == dtUnknown)
    INT_FATAL(fn, "'this' argument has unknown type");

  // Now check that the function name matches the name of the type
  // attached to 'this'.
  if (strcmp(fn->getFormal(2)->type->symbol->name, fn->name))
    return;

  // The type must be a class type.
  // No constructors for records? <hilde>
  ClassType* ct = toClassType(fn->getFormal(2)->type);
  if (!ct)
    INT_FATAL(fn, "constructor on non-class type");

  // Call the initializer, passing in just the generic arguments.
  // This call ensures that the object is default-initialized before the user's
  // constructor body is called.
  CallExpr* call = new CallExpr(ct->initializer);
  for_formals(defaultTypeConstructorArg, ct->defaultTypeConstructor) {
    ArgSymbol* arg = NULL;
    for_formals(methodArg, fn) {
      if (defaultTypeConstructorArg->name == methodArg->name) {
        arg = methodArg;
      }
    }
    if (!arg) {
      if (!defaultTypeConstructorArg->defaultExpr)
        USR_FATAL_CONT(fn, "constructor for class '%s' requires a generic argument called '%s'", ct->symbol->name, defaultTypeConstructorArg->name);
    } else {
      call->insertAtTail(new NamedExpr(arg->name, new SymExpr(arg)));
    }
  }

  fn->_this = new VarSymbol("this");
  fn->_this->addFlag(FLAG_ARG_THIS);
  fn->insertAtHead(new CallExpr(PRIM_MOVE, fn->_this, call));
  fn->insertAtHead(new DefExpr(fn->_this));
  fn->insertAtTail(new CallExpr(PRIM_RETURN, new SymExpr(fn->_this)));

  SymbolMap map;
  map.put(fn->getFormal(2), fn->_this);
  fn->formals.get(2)->remove();
  fn->formals.get(1)->remove();
  update_symbols(fn, &map);

  fn->name = astr("_construct_", fn->name);
  // Save a string?
  INT_ASSERT(!strcmp(fn->name, ct->initializer->name));
  fn->addFlag(FLAG_CONSTRUCTOR);
  // Hide the compiler-generated initializer 
  // which also serves as the default constructor.
  // hilde sez: Try leaving this visible, but make it inferior in case of multiple matches
  // (in disambiguateByMatch()).
//  ct->initializer->addFlag(FLAG_INVISIBLE_FN);
}
