// ============================================================================
// DecompileToDir.java -- batch-decompile every function to C source
// ============================================================================
// Project: ASA-EMulatR -- Alpha AXP / EV6 Architecture Emulator (tooling)
// A Ghidra headless post-script: decompiles all functions in the current
// program and writes them to one combined .c file (easy to grep) plus a
// per-function index.  Use with analyzeHeadless -process ... -postScript.
//
// Usage (headless):
//   analyzeHeadless <projDir> <projName> -process "<programName>" -noanalysis \
//     -scriptPath "<dir containing this file>" \
//     -postScript DecompileToDir.java "<outputDir>"
//
// args[0] = output directory (optional; defaults to <user.home>/ghidra_decompiled).
// @category EmulatR
// ============================================================================

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileToDir extends GhidraScript {

    @Override
    public void run() throws Exception {
        // Default to a FIXED location so the run does not depend on shell/.bat
        // argument passing (git-bash MSYS can drop or rewrite path args, which
        // is why the first run wrote nothing where expected).  An explicit
        // arg still overrides.
        String[] args = getScriptArgs();
        String defaultOut =
            "D:/EmulatR/EmulatRAppUniV4/Emulatr/tools/host_decompressor/out/decompiled_src";
        File outDir = (args.length > 0 && args[0] != null && !args[0].isEmpty())
                ? new File(args[0])
                : new File(defaultOut);
        outDir.mkdirs();
        println("DecompileToDir: writing to " + outDir.getAbsolutePath()
                + " (argc=" + args.length + ")");

        String base = currentProgram.getName();
        File combined = new File(outDir, base + ".c");
        File index    = new File(outDir, base + ".index.txt");

        DecompInterface ifc = new DecompInterface();
        ifc.setOptions(new DecompileOptions());
        if (!ifc.openProgram(currentProgram)) {
            println("DecompileToDir: openProgram FAILED: " + ifc.getLastMessage());
            return;
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();

        PrintWriter cout = new PrintWriter(new BufferedWriter(new FileWriter(combined)));
        PrintWriter iout = new PrintWriter(new BufferedWriter(new FileWriter(index)));

        cout.println("/* Decompiled source for " + base + " */");
        cout.println("/* Program image base / language: " +
                     currentProgram.getImageBase() + " " +
                     currentProgram.getLanguageID() + " */");
        cout.println();

        int ok = 0, fail = 0;
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            String tag = f.getName() + " @ " + f.getEntryPoint();
            DecompileResults res = ifc.decompileFunction(f, 60, mon);
            if (res != null && res.decompileCompleted()
                    && res.getDecompiledFunction() != null) {
                cout.println("/* ==================== " + tag + " ==================== */");
                cout.println(res.getDecompiledFunction().getC());
                cout.println();
                iout.println(f.getEntryPoint() + "\t" + f.getName());
                ok++;
            } else {
                String msg = (res != null) ? res.getErrorMessage() : "null result";
                cout.println("/* FAILED: " + tag + " : " + msg + " */");
                cout.println();
                iout.println(f.getEntryPoint() + "\t" + f.getName() + "\t[FAILED]");
                fail++;
            }
            if (((ok + fail) % 200) == 0) {
                println("DecompileToDir: " + (ok + fail) + " functions processed...");
            }
        }

        cout.flush();
        cout.close();
        iout.flush();
        iout.close();
        ifc.dispose();

        println("DecompileToDir: DONE. " + ok + " ok, " + fail + " failed.");
        println("  source: " + combined.getAbsolutePath());
        println("  index : " + index.getAbsolutePath());
    }
}
