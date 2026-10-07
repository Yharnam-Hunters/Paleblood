// SPDX-License-Identifier: GPL-2.0-or-later
// Ghidra script (GUI or headless): bring the shared names into your own project.
//
//   ImportNames.java REPO_ROOT [--create]
//
// - REPO_ROOT is your checkout of this repository. The script reads symbols/functions.csv and,
//   with --create, symbols/ghidra_functions.csv. It never reads or writes game bytes.
// - --create: make a function at every address of the export that has none yet (disassembling
//   from the entry). This replaces the hours-long full auto-analysis for a new contributor:
//   import with -noanalysis, run this with --create, and the boundaries match everyone else's.
// - Every row of functions.csv names the function at its address (creating it if needed) and
//   sets a plate comment "system: S, status: T". Existing names that differ are replaced;
//   the old name is kept in the comment. functions.csv has no type column, so no types are set.
// The program must use image base 0x400000 (QUIRKS.md, "Address conventions").
//@category Port
import java.io.BufferedReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;

public class ImportNames extends GhidraScript {
    private static final long IMAGE_BASE = 0x400000L;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0 && !isRunningHeadless()) {
            args = new String[] { askDirectory("repository checkout", "Use").getAbsolutePath(),
                askYesNo("ImportNames", "Create functions from the export first (--create)?") ? "--create" : "" };
        }
        if (args.length < 1) {
            throw new IllegalArgumentException("usage: ImportNames.java REPO_ROOT [--create]");
        }
        long base = currentProgram.getImageBase().getOffset();
        if (base != IMAGE_BASE) {
            throw new IllegalStateException(String.format(
                "image base is 0x%x, must be 0x%x: re-import with image base 0x%x", base, IMAGE_BASE, IMAGE_BASE));
        }
        Path root = Paths.get(args[0]);
        boolean create = args.length > 1 && args[1].equals("--create");
        FunctionManager fm = currentProgram.getFunctionManager();

        int created = 0, existing = 0, failed = 0;
        if (create) {
            try (BufferedReader r = Files.newBufferedReader(root.resolve("symbols/ghidra_functions.csv"), StandardCharsets.UTF_8)) {
                r.readLine();
                for (String line; (line = r.readLine()) != null;) {
                    if (line.isBlank()) continue;
                    monitor.checkCancelled();
                    Address a = toAddr(Long.decode(line.substring(0, line.indexOf(','))));
                    if (fm.getFunctionAt(a) != null) { existing++; continue; }
                    if (makeFunction(a) != null) created++; else failed++;
                    if ((created + failed) % 10000 == 0) monitor.setMessage("ImportNames: " + (created + failed) + " functions");
                }
            }
            println("CREATE created=" + created + " existing=" + existing + " failed=" + failed);
        }

        int named = 0, renamed = 0, unchanged = 0, missing = 0;
        try (BufferedReader r = Files.newBufferedReader(root.resolve("symbols/functions.csv"), StandardCharsets.UTF_8)) {
            r.readLine();
            for (String line; (line = r.readLine()) != null;) {
                if (line.isBlank()) continue;
                String[] c = line.split(",", 6);
                Address a = toAddr(Long.decode(c[0]));
                String name = c[2], system = c[3], status = c[4];
                Function f = fm.getFunctionAt(a);
                if (f == null) f = makeFunction(a);
                if (f == null) { missing++; println("MISSING no function at " + c[0] + " for " + name); continue; }
                String old = f.getName();
                String plate = "system: " + system + ", status: " + status;
                if (old.equals(name)) {
                    unchanged++;
                } else {
                    if (!old.startsWith("FUN_")) plate += "\nprevious name: " + old;
                    f.setName(name, SourceType.IMPORTED);
                    if (old.startsWith("FUN_")) named++; else renamed++;
                }
                currentProgram.getListing().setComment(a, CodeUnit.PLATE_COMMENT, plate);
            }
        }
        println("NAMES named=" + named + " renamed=" + renamed + " unchanged=" + unchanged + " missing=" + missing);
    }

    private Function makeFunction(Address a) {
        if (currentProgram.getListing().getInstructionAt(a) == null) {
            DisassembleCommand cmd = new DisassembleCommand(a, null, true);
            if (!cmd.applyTo(currentProgram, monitor)) return null;
        }
        return createFunction(a, null);
    }
}
