// SPDX-License-Identifier: GPL-2.0-or-later
// Ghidra headless script: how many imports the loader resolved to a name.
//
//   ... -postScript ImportReport.java OUT.txt
//
// An import is "resolved" when its external location carries a name from the NID database;
// an unresolved one keeps the loader's NID form `AbCdEfGhIjK#lib#mod`. The `#` suffix is what
// marks it: a plain 11-character name such as `strncasecmp` is a resolved name. Prints totals,
// per-library counts of unresolved imports and a sample, and writes the same to OUT.txt.
// Output contains names only (no game bytes).
//@category Export
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Pattern;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.ExternalLocation;
import ghidra.program.model.symbol.ExternalLocationIterator;
import ghidra.program.model.symbol.ExternalManager;

public class ImportReport extends GhidraScript {
    private static final Pattern NID = Pattern.compile(
        "^[A-Za-z0-9+\\-]{11}#[A-Za-z0-9+\\-]+#[A-Za-z0-9+\\-]+(\\+0x[0-9a-f]+)?$");

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        ExternalManager em = currentProgram.getExternalManager();
        int total = 0, resolved = 0, functions = 0, data = 0;
        Map<String, int[]> perLib = new TreeMap<>();
        List<String> sample = new ArrayList<>();
        for (String lib : em.getExternalLibraryNames()) {
            ExternalLocationIterator it = em.getExternalLocations(lib);
            while (it.hasNext()) {
                ExternalLocation loc = it.next();
                total++;
                if (loc.getFunction() != null) functions++; else data++;
                String label = loc.getLabel();
                boolean unresolved = NID.matcher(label).matches();
                int[] c = perLib.computeIfAbsent(lib, k -> new int[2]);
                c[0]++;
                if (unresolved) {
                    c[1]++;
                    if (sample.size() < 15) sample.add(lib + " " + label);
                } else {
                    resolved++;
                }
            }
        }
        StringBuilder sb = new StringBuilder();
        sb.append(String.format(
            "IMPORTS total=%d resolved=%d unresolved=%d rate=%.2f%% functions=%d other=%d%n",
            total, resolved, total - resolved, total == 0 ? 0.0 : 100.0 * resolved / total,
            functions, data));
        for (Map.Entry<String, int[]> e : perLib.entrySet()) {
            sb.append(String.format("LIB %-32s imports=%d unresolved=%d%n",
                e.getKey(), e.getValue()[0], e.getValue()[1]));
        }
        for (String s : sample) sb.append("UNRESOLVED ").append(s).append('\n');
        print(sb.toString());
        if (args.length == 1) {
            try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
                out.print(sb);
            }
        }
    }
}
