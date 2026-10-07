// SPDX-License-Identifier: GPL-2.0-or-later
// Ghidra script (GUI or headless): a local working draft for replacing a function.
//
//   DraftExport.java OUT_DIR ADDRESS [ADDRESS ...]
//
// Writes OUT_DIR/<address>.txt per function: the decompiler's C, the disassembly, the
// functions it calls (with library names) and the data addresses it reads or writes, its
// callers, and every reference to its entry (function pointers in tables, address loads). A draft contains decompiler output from your dump: it stays outside the repository
// (CONTRIBUTING.md, "Drafts"), and only the verified, renamed, readable replacement is
// committed.
//@category Port
import java.io.PrintWriter;
import java.nio.file.Paths;
import java.util.TreeSet;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

public class DraftExport extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException("usage: DraftExport.java OUT_DIR ADDRESS [ADDRESS ...]");
        }
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        for (int i = 1; i < args.length; i++) {
            Function f = getFunctionAt(toAddr(Long.decode(args[i])));
            if (f == null) {
                println("NO FUNCTION at " + args[i]);
                continue;
            }
            String name = String.format("0x%08x", f.getEntryPoint().getOffset());
            try (PrintWriter out = new PrintWriter(Paths.get(args[0], name + ".txt").toFile(), "UTF-8")) {
                out.printf("# draft %s  size %d  (local only: never commit)%n%n", name, f.getBody().getNumAddresses());
                out.println("## callers");
                for (Function c : f.getCallingFunctions(monitor)) {
                    out.printf("  0x%08x %s%n", c.getEntryPoint().getOffset(), c.getName());
                }
                out.println("\n## references to the entry (pointers, tables, address loads)");
                for (Reference r : getReferencesTo(f.getEntryPoint())) {
                    Function from = getFunctionContaining(r.getFromAddress());
                    out.printf("  0x%08x %s %s%n", r.getFromAddress().getOffset(), r.getReferenceType(),
                        from != null ? "in " + from.getName() : "data");
                    if (from == null) {
                        // A pointer stored in data (a table or vtable slot): who refers to that slot.
                        // Code refers to a table by its start, so walk back to the nearest referenced
                        // address (at most 512 slots) and report the slot index from there.
                        for (int back = 0; back <= 512 * 8; back += 8) {
                            ghidra.program.model.address.Address slot = r.getFromAddress().subtract(back);
                            Reference[] to = getReferencesTo(slot);
                            if (to.length == 0) continue;
                            out.printf("      table at 0x%08x, slot %d (+0x%x)%n", slot.getOffset(), back / 8, back);
                            for (Reference r2 : to) {
                                Function f2 = getFunctionContaining(r2.getFromAddress());
                                out.printf("      <- 0x%08x %s %s%n", r2.getFromAddress().getOffset(), r2.getReferenceType(),
                                    f2 != null ? "in " + f2.getName() + String.format(" (0x%08x)", f2.getEntryPoint().getOffset()) : "data");
                            }
                            break;
                        }
                    }
                }
                out.println("\n## calls");
                for (Function c : f.getCalledFunctions(monitor)) {
                    Function t = c.isThunk() ? c.getThunkedFunction(true) : c;
                    out.printf("  0x%08x %s%s%n", c.getEntryPoint().getOffset(), t.getName(), t.isExternal() ? " (library)" : "");
                }
                out.println("\n## data references");
                TreeSet<String> data = new TreeSet<>();
                InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    for (Reference r : ins.getReferencesFrom()) {
                        if (r.getReferenceType().isData()) {
                            data.add(String.format("  0x%08x %s from 0x%08x", r.getToAddress().getOffset(),
                                r.getReferenceType(), ins.getAddress().getOffset()));
                        }
                    }
                }
                for (String d : data) out.println(d);
                out.println("\n## decompiler");
                DecompileResults res = decomp.decompileFunction(f, 120, monitor);
                out.println(res.getDecompiledFunction() == null ? "(no output)" : res.getDecompiledFunction().getC());
                out.println("## disassembly");
                it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    out.printf("  %s  %s%n", ins.getAddress(), ins);
                }
            }
            println("DRAFT " + name);
        }
    }
}
