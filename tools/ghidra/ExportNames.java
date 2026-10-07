// SPDX-License-Identifier: GPL-2.0-or-later
// Ghidra script (GUI or headless): your names, as rows for symbols/functions.csv.
//
//   ExportNames.java REPO_ROOT OUT.csv
//
// Writes one row per function in .text whose name starts with a system of the repo (a
// directory under game/) followed by "_", and that is not already in symbols/functions.csv
// under the same name: address,size,name,system,original, with empty notes. Size comes from
// symbols/ghidra_functions.csv, not from your project: without full analysis Ghidra's bodies
// differ by a few bytes (padding), and the export is the shared reference. Functions that are
// not in the export are listed and skipped. Names only: no bytes, no decompiler output. Merge
// the result with tools/merge_functions.py, which validates it.
//@category Port
import java.io.BufferedReader;
import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class ExportNames extends GhidraScript {
    private static final long IMAGE_BASE = 0x400000L;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0 && !isRunningHeadless()) {
            args = new String[] { askDirectory("repository checkout", "Use").getAbsolutePath(),
                askFile("Write rows to", "Save").getAbsolutePath() };
        }
        if (args.length != 2) {
            throw new IllegalArgumentException("usage: ExportNames.java REPO_ROOT OUT.csv");
        }
        if (currentProgram.getImageBase().getOffset() != IMAGE_BASE) {
            throw new IllegalStateException("image base must be 0x400000");
        }
        Path root = Paths.get(args[0]);
        TreeSet<String> systems = new TreeSet<>();
        File[] dirs = root.resolve("game").toFile().listFiles(File::isDirectory);
        if (dirs != null) for (File d : dirs) systems.add(d.getName());

        Map<Long, String> known = new HashMap<>();
        try (BufferedReader r = Files.newBufferedReader(root.resolve("symbols/functions.csv"), StandardCharsets.UTF_8)) {
            r.readLine();
            for (String line; (line = r.readLine()) != null;) {
                if (line.isBlank()) continue;
                String[] c = line.split(",", 6);
                known.put(Long.decode(c[0]), c[2]);
            }
        }
        Map<Long, Long> exportSize = new HashMap<>();
        try (BufferedReader r = Files.newBufferedReader(root.resolve("symbols/ghidra_functions.csv"), StandardCharsets.UTF_8)) {
            r.readLine();
            for (String line; (line = r.readLine()) != null;) {
                if (line.isBlank()) continue;
                int comma = line.indexOf(',');
                exportSize.put(Long.decode(line.substring(0, comma)), Long.parseLong(line.substring(comma + 1).trim()));
            }
        }
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        List<String> rows = new ArrayList<>();
        int conflicts = 0, notInExport = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.isThunk() || f.isExternal() || text == null || !text.contains(f.getEntryPoint())) continue;
            String name = f.getName();
            String system = null;
            for (String s : systems) if (name.startsWith(s + "_")) { system = s; break; }
            if (system == null || !name.matches("[a-z][a-z0-9]*(_[a-z0-9]+)+")) continue;
            long a = f.getEntryPoint().getOffset();
            String had = known.get(a);
            if (name.equals(had)) continue;
            if (had != null) { conflicts++; println("CONFLICT " + String.format("0x%08x", a) + " repo=" + had + " yours=" + name); }
            Long size = exportSize.get(a);
            if (size == null) { notInExport++; println("NOT IN EXPORT " + String.format("0x%08x", a) + " " + name); continue; }
            rows.add(String.format("0x%08x,%d,%s,%s,original,", a, size, name, system));
        }
        try (PrintWriter out = new PrintWriter(args[1], "UTF-8")) {
            out.print("address,size,name,system,status,notes\n");
            for (String row : rows) out.print(row + "\n");
        }
        println("EXPORTNAMES rows=" + rows.size() + " conflicts=" + conflicts + " not_in_export=" + notInExport);
    }
}
