/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


#ifndef _PASSLIST_H_
#define _PASSLIST_H_

#include "passes.h"
#include "log.h"    // For LOG_<passname> #defines.

#define FIRST {NULL, NULL, NUL}
#define LAST {NULL, NULL, NUL}
#define RUN(x) {x, #x, LOG_ ## x}
//
// passlist: contains passes in the order that they are called
//
PassInfo passlist[] = {
  FIRST,
  // Chapel to AST
  RUN(parse),               // parse files and create AST
  RUN(checkParsed),         // checks semantics of parsed AST

  // Read in runtime and included C header file types/prototypes
  RUN(readCTypes),

  // Scope resolution and normalization
  RUN(cleanup),             // post parsing transformations
  RUN(scopeResolve),        // resolve symbols by scope
  RUN(flattenClasses),      // denest nested classes
  RUN(docs),                // generates documentation instead of executable
                            // if chpldoc is used instead of chpl, otherwise
                            // generates both documentation and an executable

  RUN(normalize),           // normalization transformations
  RUN(checkNormalized),     // check semantics of normalized AST

  // Creation of default functions
  RUN(buildDefaultFunctions), // build default functions

  // Function resolution and shallow type inference
  RUN(resolve),             // resolves function calls and types
  RUN(checkResolved),       // checks semantics of resolved AST

  // Post-resolution cleanup
  RUN(flattenFunctions),    // denest nested functions
  RUN(cullOverReferences),  // remove excess references
  RUN(callDestructors),
  RUN(lowerIterators),      // lowers iterators into functions/classes
  RUN(parallel),            // parallel transforms
  RUN(prune),               // prune AST of dead functions and types

  // Optimizations
  RUN(complex2record),      // change complex numbers into records
  RUN(removeUnnecessaryAutoCopyCalls),
  RUN(inlineFunctions),     // function inlining
  RUN(scalarReplace),       // scalar replace all tuples
  RUN(refPropagation),      // reference propagation
  RUN(copyPropagation),     // copy propagation
  RUN(deadCodeElimination), // eliminate dead code
  RUN(removeWrapRecords),   // remove _array, _domain, and _distribution records
  RUN(removeEmptyRecords),  // remove empty records
  RUN(localizeGlobals),     // pull out global constants from loop runs
  RUN(prune2),              // prune AST of dead functions and types again

  RUN(returnStarTuplesByRefArgs),
  RUN(gpuFlattenArgs),      // Flatten out arguments used to call in gpu kernel

  RUN(insertWideReferences),// inserts wide references for on clauses
  RUN(optimizeOnClauses),   // Optimize on clauses
  RUN(addInitCalls),        // Add module initialization calls and guards.  
  // AST to C or LLVM
  RUN(insertLineNumbers),   // insert line numbers for error messages
  RUN(repositionDefExpressions), // put defPoints just before first usage
  RUN(codegen),             // generate C code
  RUN(makeBinary),          // invoke underlying C compiler
  LAST
};

#endif
