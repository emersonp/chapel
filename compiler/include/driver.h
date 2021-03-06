/**************************************************************************
  Copyright (c) 2004-2012, Cray Inc.  (See LICENSE file for more details)
**************************************************************************/


#ifndef _driver_H_
#define _driver_H_

#include <cstdio>
#include "map.h"
#include "chpl.h"

// For versions of bison beyond 2.3, this struct and the following
// two defines should be moved back into chapel.ypp into a %code requires
// block so that YYLTYPE is near the YYLLOC_DEFAULT definition.
typedef struct YYLTYPE {    
       int first_line;      
       int first_column;    
       int last_line;       
       int last_column;     
       char *comment;       
} YYLTYPE;
#define YYLTYPE_IS_DECLARED 1
#define YYLTYPE_IS_TRIVIAL 1




extern const char *chplBinaryName;

extern FILE* html_index_file;
extern FILE* deletedIdHandle;
extern char deletedIdFilename[FILENAME_MAX+1];
#define deletedIdON (deletedIdFilename[0] != '\0')

extern char log_dir[FILENAME_MAX+1];
extern char log_module[FILENAME_MAX+1];
extern char log_symbol[FILENAME_MAX+1];
extern bool fLogIds;

extern int instantiation_limit;

extern int fdump_html;
extern bool fdump_html_incude_system_modules;

extern int trace_level;

extern int currentPassNo;
extern const char* currentPassName;

// optimization control flags
extern bool fFastFlag;
extern int fConditionalDynamicDispatchLimit;
extern bool fNoBoundsChecks;
extern bool fNoCopyPropagation;
extern bool fNoDeadCodeElimination;
extern bool fNoGlobalConstOpt;
extern bool fNoFastFollowers;
extern bool fNoInlineIterators;
extern bool fNoInline;
extern bool fNoLiveAnalysis;
extern bool fNoLocalChecks;
extern bool fNoNilChecks;

//Disable flag for transitional warning of 1.5 domain literal syntax.
//This option should be removed after 1.6 is released!
extern bool fNoWarnDomainLiteral; 

extern bool fNoRemoteValueForwarding;
extern bool fNoRemoveCopyCalls;
extern bool fNoRepositionDefExpr;
extern bool fNoScalarReplacement;
extern bool fNoTupleCopyOpt;
extern bool fNoOptimizeLoopIterators;
extern bool fNoPrivatization;
extern bool fNoOptimizeOnClauses;
extern bool fNoRemoveEmptyRecords;
extern int optimize_on_clause_limit;
extern int scalar_replace_limit;
extern int tuple_copy_limit;

extern bool report_inlining;
extern char CHPL_HOME[FILENAME_MAX];

extern const char* CHPL_HOST_PLATFORM;
extern const char* CHPL_TARGET_PLATFORM;
extern const char* CHPL_HOST_COMPILER;
extern const char* CHPL_TARGET_COMPILER;
extern const char* CHPL_TASKS;
extern const char* CHPL_THREADS;
extern const char* CHPL_COMM;
extern const char* CHPL_MAKE;

extern char fExplainCall[256];
extern char fExplainInstantiation[256];
extern bool fPrintCallStackOnError;
extern bool fPrintIDonError;
extern bool fCLineNumbers;
extern char fPrintStatistics[256];
extern bool fPrintDispatch;
extern bool fGenIDS;
extern bool fSerialForall;
extern bool fSerial;
extern bool fLocal;
extern bool fGPU;
extern bool fHeterogeneous;
extern bool fieeefloat;
extern int  fMaxCIdentLen;

extern bool llvmCodegen;
extern char breakOnCodegenCname[256];

enum { LS_DEFAULT=0, LS_STATIC, LS_DYNAMIC };
extern int fLinkStyle;

extern int debugParserLevel;
extern bool fLibraryCompile;
extern bool no_codegen;
extern bool genExternPrototypes;
extern bool developer;
extern int num_constants_per_variable;
extern bool printCppLineno;

extern bool fDocs;
extern bool fDocsAlphabetize;
extern char fDocsCommentLabel[256];
extern char fDocsFolder[256];
extern bool fDocsTextOnly;
extern char defaultDist[256];
extern char mainModuleName[256];
extern bool printSearchDirs;
extern bool printModuleFiles;
extern bool ignore_warnings;
extern bool ignore_errors;
extern int squelch_header_errors;

extern bool fWarnPromotion;
extern bool fReportOptimizedOn;
extern bool fReportScalarReplace;
extern bool fReportDeadBlocks;

extern bool debugCCode, optimizeCCode;

extern bool fEnableTimers;
extern Timer timer1;
extern Timer timer2;
extern Timer timer3;
extern Timer timer4;
extern Timer timer5;

extern bool fNoMemoryFrees;
extern int numGlobalsOnHeap;
extern bool preserveInlinedLineNumbers;

extern int breakOnID;
extern int breakOnDeleteID;

// code generation strings
extern const char* compileCommand;
extern char compileVersion[64];

// This flag is useful for testing
// the compiler but breaks the language!
extern bool fNoInternalModules;

#endif
