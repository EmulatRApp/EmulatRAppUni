// DumpSysvarFns.java -- EmulatR firmware RE helper
// ---------------------------------------------------------------------------
// Locate and decompile the SRM functions that decide the platform banner
// (get_sysvar / build_dsrdb) and the OCP probe, by following code references
// to the dsrdb SysType->name table and the platform decision strings.  This
// sidesteps Ghidra's poor auto-analysis of the rodata region (where clicking
// string XREFs just lands in mis-typed pointer tables).
//
// Run: Window -> Script Manager -> run DumpSysvarFns on the ds20 ROM program.
// Output: <user home>\sysvar_dump.txt  (progress also printed to the console).
//
//@category EmulatR
//@menupath Tools.EmulatR.Dump Sysvar Functions
// ---------------------------------------------------------------------------
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeMap;

public class DumpSysvarFns extends GhidraScript {

    // rodata anchors the banner / sysvar / OCP code references (confirmed from
    // the Defined Strings dump).  Adjust offsets if a build differs.
    private static final long[]   ANCHORS = {
        0x153cd8L, 0x19ad90L, 0x19adc0L, 0x19a6c8L, 0x19a6e0L,
        0x197970L, 0x197998L, 0x1979b0L, 0x1a0218L
    };
    private static final String[] LABELS  = {
        "dsrdb SysType->name table base",
        "\"Defaulting system type to AlphaPC 264DP\"",
        "\"Error determining system type, SYSVAR = %x\"",
        "\"AlphaPC 264DP %3d MHz\"",
        "\"AlphaServer DS20 %3d MHz\"",
        "\"ERROR: could not read OCP_TEXT EV\"",
        "\"AlphaServer DS20\" (OCP default text)",
        "banner format string",
        "\"iic_ocp0\""
    };

    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();

        // 1) Gather instruction addresses that reference each anchor.
        Set<Address> referrers = new HashSet<>();
        for (int i = 0; i < ANCHORS.length; i++) {
            Address a = toAddr(ANCHORS[i]);
            Reference[] refs = getReferencesTo(a);
            int n = 0;
            if (refs != null) {
                for (Reference r : refs) {
                    referrers.add(r.getFromAddress());
                    n++;
                }
            }
            println(String.format("anchor 0x%x %s -> %d ref(s)", ANCHORS[i], LABELS[i], n));
        }

        // 2) Resolve referrers to containing functions; remember orphans.
        TreeMap<Long, Function> funcs = new TreeMap<>();
        List<Address> orphans = new ArrayList<>();
        for (Address fa : referrers) {
            Function f = getFunctionContaining(fa);
            if (f != null) {
                funcs.put(f.getEntryPoint().getOffset(), f);
            } else {
                orphans.add(fa);
            }
        }

        // 3) Decompile.
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();

        String outpath = System.getProperty("user.home") + File.separator + "sysvar_dump.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outpath));

        out.println("=== EmulatR get_sysvar / build_dsrdb decompile dump ===");
        out.println("program : " + currentProgram.getName());
        out.println("referrers=" + referrers.size()
                + " functions=" + funcs.size()
                + " orphans=" + orphans.size());
        out.println();

        for (Function f : funcs.values()) {
            String hdr = "FUNCTION " + f.getName() + " @ " + f.getEntryPoint();
            out.println("################################################################");
            out.println("# " + hdr);
            out.println("################################################################");
            println("decompiling " + hdr);
            DecompileResults res = dec.decompileFunction(f, 90, monitor);
            if (res != null && res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("  <decompile failed: "
                        + (res != null ? res.getErrorMessage() : "no result") + ">");
            }
            out.println();
        }

        // 4) Orphan refs: dump raw disassembly context so the code is visible
        //    even without a defined function.
        if (!orphans.isEmpty()) {
            Collections.sort(orphans);
            out.println("################################################################");
            out.println("# ORPHAN REFS (referring code not in a defined function)");
            out.println("################################################################");
            for (Address fa : orphans) {
                out.println("---- context around " + fa + " ----");
                Address cur = fa.subtract(0x40);
                Address endA = fa.add(0x44);
                while (cur.compareTo(endA) < 0) {
                    CodeUnit cu = listing.getCodeUnitAt(cur);
                    if (cu == null) {
                        cur = cur.add(1);
                        continue;
                    }
                    String mark = cur.equals(fa) ? "  >>" : "    ";
                    out.println(mark + " " + cur + "  " + cu.toString());
                    cur = cur.add(cu.getLength());
                }
                out.println();
            }
            println(orphans.size() + " orphan ref(s) dumped as raw context (see file)");
        }

        out.close();
        dec.dispose();
        println("WROTE: " + outpath);
    }
}
