// SPDX-License-Identifier: GPL-2.0-or-later
// Ghidra headless script: write address,size of every real function in the .text block of
// the program, nothing else. No names, no bytes.
//
//   analyzeHeadless PROJECT_DIR PROJECT -process eboot.elf -noanalysis \
//       -scriptPath tools/ghidra -postScript ExportFunctions.java OUT.csv
//
// Run it on a program that was imported with image base 0x400000 (QUIRKS.md, "Address
// conventions") and fully analyzed. The script refuses any other base. The ELF has no section
// headers; ".text" is the block the Orbis loader names after the executable program header
// (it splits .rodata off the end by its own heuristic). Thunks, external functions and
// functions outside .text are not counted; the number outside .text is printed so a misplaced
// boundary is visible. size is the number of addresses in the function body. Addresses are
// written as 0x + 8 lowercase hex digits, ascending.
//@category Export
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class ExportFunctions extends GhidraScript {
    private static final long IMAGE_BASE = 0x400000L;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: ExportFunctions.java OUT_CSV");
        }
        long base = currentProgram.getImageBase().getOffset();
        if (base != IMAGE_BASE) {
            throw new IllegalStateException(String.format(
                "image base is 0x%x, must be 0x%x: re-import with -loader-imageBase 0x%x",
                base, IMAGE_BASE, IMAGE_BASE));
        }
        Memory mem = currentProgram.getMemory();
        MemoryBlock text = mem.getBlock(".text");
        if (text == null) {
            throw new IllegalStateException("no .text block: was this imported with the Orbis loader?");
        }
        List<long[]> rows = new ArrayList<>();
        int outside = 0, thunks = 0, external = 0, empty = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.isExternal()) { external++; continue; }
            if (f.isThunk()) { thunks++; continue; }
            Address entry = f.getEntryPoint();
            if (!text.contains(entry)) { outside++; continue; }
            long size = f.getBody().getNumAddresses();
            if (size < 1) { empty++; continue; }
            rows.add(new long[] { entry.getOffset(), size });
        }
        rows.sort((a, b) -> Long.compareUnsigned(a[0], b[0]));
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            out.print("address,size\n");
            for (long[] r : rows) {
                out.print(String.format("0x%08x,%d\n", r[0], r[1]));
            }
        }
        println(String.format(".text %s-%s", text.getStart(), text.getEnd()));
        println("EXPORT functions=" + rows.size() + " thunks_skipped=" + thunks
            + " external_skipped=" + external + " outside_text_skipped=" + outside
            + " empty_skipped=" + empty);
    }
}
