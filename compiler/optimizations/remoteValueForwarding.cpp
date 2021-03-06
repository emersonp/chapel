/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


#include "astutil.h"
#include "expr.h"
#include "optimizations.h"
#include "stmt.h"


//#define DEBUG_SYNC_ACCESS_FUNCTION_SET


//
// Compute set of functions that access sync variables.
//
static void
buildSyncAccessFunctionSet(Vec<FnSymbol*>& syncAccessFunctionSet) {
  Vec<FnSymbol*> syncAccessFunctionVec;

  //
  // Find all functions that directly call sync access primitives.
  //
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->parentSymbol) {
      if (call->isPrimitive(PRIM_SYNC_INIT) ||
          call->isPrimitive(PRIM_SYNC_LOCK) ||
          call->isPrimitive(PRIM_SYNC_UNLOCK) ||
          call->isPrimitive(PRIM_SYNC_WAIT_FULL) ||
          call->isPrimitive(PRIM_SYNC_WAIT_EMPTY) ||
          call->isPrimitive(PRIM_SYNC_SIGNAL_FULL) ||
          call->isPrimitive(PRIM_SYNC_SIGNAL_EMPTY) ||
          call->isPrimitive(PRIM_SINGLE_INIT) ||
          call->isPrimitive(PRIM_SINGLE_LOCK) ||
          call->isPrimitive(PRIM_SINGLE_UNLOCK) ||
          call->isPrimitive(PRIM_SINGLE_WAIT_FULL) ||
          call->isPrimitive(PRIM_SINGLE_SIGNAL_FULL) ||
          call->isPrimitive(PRIM_WRITEEF) ||
          call->isPrimitive(PRIM_WRITEFF) ||
          call->isPrimitive(PRIM_WRITEXF) ||
          call->isPrimitive(PRIM_SYNC_RESET) ||
          call->isPrimitive(PRIM_READFE) ||
          call->isPrimitive(PRIM_READFF) ||
          call->isPrimitive(PRIM_READXX) ||
          call->isPrimitive(PRIM_SYNC_ISFULL) ||
          call->isPrimitive(PRIM_SINGLE_WRITEEF) ||
          call->isPrimitive(PRIM_SINGLE_RESET) ||
          call->isPrimitive(PRIM_SINGLE_READFF) ||
          call->isPrimitive(PRIM_SINGLE_READXX) ||
          call->isPrimitive(PRIM_SINGLE_ISFULL)) {
        FnSymbol* parent = toFnSymbol(call->parentSymbol);
        INT_ASSERT(parent);
        if (!parent->hasFlag(FLAG_DONT_DISABLE_REMOTE_VALUE_FORWARDING) &&
            !syncAccessFunctionSet.set_in(parent)) {
          syncAccessFunctionSet.set_add(parent);
          syncAccessFunctionVec.add(parent);
#ifdef DEBUG_SYNC_ACCESS_FUNCTION_SET
          printf("%s:%d %s\n", parent->getModule()->name, parent->linenum(), parent->name);
#endif
        }
      }
    }
  }

  //
  // Find all functions that indirectly call sync access primitives.
  //
  forv_Vec(FnSymbol, fn, syncAccessFunctionVec) {
    forv_Vec(CallExpr, caller, *fn->calledBy) {
      FnSymbol* parent = toFnSymbol(caller->parentSymbol);
      INT_ASSERT(parent);
      if (!parent->hasFlag(FLAG_DONT_DISABLE_REMOTE_VALUE_FORWARDING) &&
          !syncAccessFunctionSet.set_in(parent)) {
        syncAccessFunctionSet.set_add(parent);
        syncAccessFunctionVec.add(parent);
#ifdef DEBUG_SYNC_ACCESS_FUNCTION_SET
        printf("%s:%d %s\n", parent->getModule()->name, parent->linenum(), parent->name);
        printf("  %s:%d %s\n", fn->getModule()->name, fn->linenum(), fn->name);
#endif
      }
    }
  }
}


//
// Return true iff it is safe to dereference a reference arg.  It is
// safe to dereference iff the reference is not modified and any use
// of the reference is a simple dereference or is passed or moved to
// another reference that is safe to dereference.
//
// The argument safeSettableField is used to ignore SET_MEMBER in that
// case of testing whether a reference field can be replaced with a
// value; this handles the case where the reference field is
// reassigned to itself (probably of another instance)
//
static bool
isSafeToDeref(Symbol* ref,
              Map<Symbol*,Vec<SymExpr*>*>& defMap,
              Map<Symbol*,Vec<SymExpr*>*>& useMap,
              Vec<Symbol*>* visited,
              Symbol* safeSettableField) {
  if (!visited) {
    Vec<Symbol*> newVisited;
    return isSafeToDeref(ref, defMap, useMap, &newVisited, safeSettableField);
  }
  if (visited->set_in(ref))
    return true;
  visited->set_add(ref);

  int numDefs = (defMap.get(ref)) ? defMap.get(ref)->n : 0;
  if ((isArgSymbol(ref) && numDefs > 0) || numDefs > 1)
    return false;

  for_uses(use, useMap, ref) {
    if (CallExpr* call = toCallExpr(use->parentExpr)) {
      if (call->isResolved()) {
        ArgSymbol* arg = actual_to_formal(use);
        if (!isSafeToDeref(arg, defMap, useMap, visited, safeSettableField))
          return false;
      } else if (call->isPrimitive(PRIM_MOVE)) {
        SymExpr* newRef = toSymExpr(call->get(1));
        INT_ASSERT(newRef);
        if (!isSafeToDeref(newRef->var, defMap, useMap, visited, safeSettableField))
          return false;
      } else if (call->isPrimitive(PRIM_SET_MEMBER) && safeSettableField) {
        SymExpr* se = toSymExpr(call->get(2));
        INT_ASSERT(se);
        if (se->var != safeSettableField)
          return false;
      } else if (!call->isPrimitive(PRIM_DEREF))
        return false; // what cases does this preclude? can this be an assert?
    } else
      return false; // what cases does this preclude? can this be an assert?
  }

  return true;
}



static bool isSufficientlyConst(ArgSymbol* arg) {
  Type* argvaltype = arg->getValType();

  if (argvaltype->symbol->hasFlag(FLAG_ARRAY)) {
    // Arg is an array, so it's sufficiently constant (because this
    // refers to the descriptor, not the array's values\n");
    return true;
  }

  // We may want to add additional cases here as we discover them

  // otherwise, conservatively assume it varies
  return false;
}


//
// Convert reference args into values if they are only read and
// reading them early does not violate program semantics.
//
// pre-condition: the call graph is computed
//
void
remoteValueForwarding(Vec<FnSymbol*>& fns) {
  if (fNoRemoteValueForwarding)
    return;

  Vec<FnSymbol*> syncAccessFunctionSet;
  buildSyncAccessFunctionSet(syncAccessFunctionSet);

  Map<Symbol*,Vec<SymExpr*>*> defMap;
  Map<Symbol*,Vec<SymExpr*>*> useMap;
  buildDefUseMaps(defMap, useMap);

  //
  // change reference type fields in loop body argument classes
  // (created when transforming recursive leader iterators into
  // recursive functions) to value type fields if safe
  //
  forv_Vec(ClassType, ct, gClassTypes) {
    if (ct->symbol->hasFlag(FLAG_LOOP_BODY_ARGUMENT_CLASS)) {
      for_fields(field, ct) {
        if (field->type->symbol->hasFlag(FLAG_REF)) {
          INT_ASSERT(!defMap.get(field));
          bool safeToDeref = true;
          for_uses(use, useMap, field) {
            CallExpr* call = toCallExpr(use->parentExpr);
            INT_ASSERT(call);
            if (call->isPrimitive(PRIM_GET_MEMBER_VALUE)) {
              CallExpr* move = toCallExpr(call->parentExpr);
              INT_ASSERT(move && move->isPrimitive(PRIM_MOVE));
              SymExpr* lhs = toSymExpr(move->get(1));
              INT_ASSERT(lhs);
              if (!isSafeToDeref(lhs->var, defMap, useMap, NULL, field)) {
                safeToDeref = false;
                break;
              }
            } else if (!call->isPrimitive(PRIM_SET_MEMBER))
              INT_FATAL(field, "unexpected case");
          }
          if (safeToDeref) {
            Type* vt = field->getValType();
            for_uses(use, useMap, field) {
              CallExpr* call = toCallExpr(use->parentExpr);
              INT_ASSERT(call);
              if (call->isPrimitive(PRIM_SET_MEMBER)) {
                Symbol* tmp = newTemp(vt);
                call->insertBefore(new DefExpr(tmp));
                call->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_DEREF, call->get(3)->remove())));
                call->insertAtTail(tmp);
              } else if (call->isPrimitive(PRIM_GET_MEMBER_VALUE)) {
                CallExpr* move = toCallExpr(call->parentExpr);
                INT_ASSERT(move && move->isPrimitive(PRIM_MOVE));
                Symbol* tmp = newTemp(vt);
                move->insertBefore(new DefExpr(tmp));
                move->insertBefore(new CallExpr(PRIM_MOVE, tmp, call->remove()));
                move->insertAtTail(new CallExpr(PRIM_ADDR_OF, tmp));
              } else
                INT_FATAL(field, "unexpected case");
            }
            field->type = vt;
          }
        }
      }
    }
  }

  forv_Vec(FnSymbol, fn, fns) {
    INT_ASSERT(fn->calledBy->n == 1);
    CallExpr* call = fn->calledBy->v[0];

    //
    // For each reference arg that is safe to dereference
    //
    for_formals(arg, fn) {

      /* if this function accesses sync vars and the argument is not
         const, then we cannot remote value forward the argument due
         to the fence implied by the sync var accesses */
      if (syncAccessFunctionSet.set_in(fn) && !isSufficientlyConst(arg)) {
        continue;
      }

      if (arg->type->symbol->hasFlag(FLAG_REF) &&
          isSafeToDeref(arg, defMap, useMap, NULL, NULL)) {

        //
        // Find actual for arg and dereference arg type.
        //
        SymExpr* actual = toSymExpr(formal_to_actual(call, arg));
        INT_ASSERT(actual && actual->var->type == arg->type);
        arg->type = arg->getValType();
        
        //
        // Insert de-reference temp of value.
        //
        VarSymbol* deref = newTemp("rvfDerefTmp", arg->type);
        call->insertBefore(new DefExpr(deref));
        call->insertBefore(new CallExpr(PRIM_MOVE, deref,
                                        new CallExpr(PRIM_DEREF, actual->var)));
        actual->replace(new SymExpr(deref));
        
        //
        // Insert re-reference temps at use points.
        //
        for_uses(use, useMap, arg) {
          CallExpr* call = toCallExpr(use->parentExpr);
          if (call && call->isPrimitive(PRIM_DEREF)) {
            call->replace(new SymExpr(arg));
          } else if (call && call->isPrimitive(PRIM_MOVE)) {
            use->replace(new CallExpr(PRIM_ADDR_OF, arg));
          } else {
            Expr* stmt = use->getStmtExpr();
            VarSymbol* reref = newTemp("rvfRerefTmp", actual->var->type);
            stmt->insertBefore(new DefExpr(reref));
            stmt->insertBefore(new CallExpr(PRIM_MOVE, reref,
                                            new CallExpr(PRIM_ADDR_OF, arg)));
            use->replace(new SymExpr(reref));
          }
        }
      }
    }
  }

  freeDefUseMaps(defMap, useMap);
}
