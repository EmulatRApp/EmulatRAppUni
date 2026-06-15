@echo off
REM ============================================================================
REM GrainFactory codegen pipeline.  Run from grainFactoryLib/codegen/.
REM ============================================================================

REM Step 1: generate the distinct FP leaf bodies (fBoxLib/grains/FloatVariants.cpp)
REM         and refresh the generated section of handwritten.tsv.  Idempotent
REM         (rewrites the generated block, never appends).  MUST run BEFORE
REM         genGrains.py so the dispatch codegen sees the current leaf manifest.
py gen_fp_leaves.py --master ../GrainMasterV4.tsv --handwritten handwritten.tsv --out ../../fBoxLib/grains/FloatVariants.cpp
if errorlevel 1 exit /b 1

REM Step 2: generate the dispatch tables, forward declarations, and stubs.
py genGrains.py --flags ../SemanticFlags.tsv --master ../GrainMasterV4.tsv --out ../generated
if errorlevel 1 exit /b 1
