// SPDX-License-Identifier: GPL-2.0-or-later
// Export analysis facts only; no executable bytes or decompiler output are written.
//
//   analyzeHeadless PROJECT_DIR PROJECT -process eboot.elf \
//       -scriptPath tools/ghidra -postScript ExportFunctionAnalysis.java EDGES.csv METRICS.csv
//
// Run against the same fully analyzed program used for ExportFunctions. Pass the exact program
// SHA separately to `tools/function_catalog.py build --executable FILE --edges EDGES.csv
// --metrics METRICS.csv`. Ghidra's listing supplies evidence, not a guarantee of game behavior.
//@category Export
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.ExternalLocation;

public class ExportFunctionAnalysis extends GhidraScript {
    private static String csv(String value) {
        String text = value == null ? "" : value;
        return "\"" + text.replace("\"", "\"\"") + "\"";
    }

    private static String hex(Address address) {
        return String.format("0x%08x", address.getOffset());
    }

    private static String jsonString(String value) {
        return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            throw new IllegalArgumentException("usage: ExportFunctionAnalysis.java EDGES.csv METRICS.csv");
        }
        if (currentProgram.getImageBase().getOffset() != 0x400000L) {
            throw new IllegalStateException("program image base must be 0x400000; see docs/QUIRKS.md");
        }
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) throw new IllegalStateException("no .text block: import with the Orbis loader");
        FunctionManager manager = currentProgram.getFunctionManager();
        List<Function> functions = new ArrayList<>();
        for (Function function : manager.getFunctions(true)) {
            if (!function.isExternal() && !function.isThunk() && text.contains(function.getEntryPoint())) {
                functions.add(function);
            }
        }
        functions.sort((a, b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));
        try (PrintWriter edges = new PrintWriter(Files.newBufferedWriter(Paths.get(args[0]), StandardCharsets.UTF_8));
             PrintWriter metrics = new PrintWriter(Files.newBufferedWriter(Paths.get(args[1]), StandardCharsets.UTF_8))) {
            edges.println("caller,callee,kind,reference,source,confidence");
            metrics.println("address,instruction_count,complexity,fixture_difficulty,data_references,boundary_confidence,analysis_complete,source");
            for (Function function : functions) {
                int instructionCount = 0;
                int complexity = 1;
                TreeSet<String> dataReferences = new TreeSet<>();
                InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
                while (instructions.hasNext()) {
                    Instruction instruction = instructions.next();
                    instructionCount++;
                    if (instruction.getFlowType().isConditional()) complexity++;
                    boolean hasCallReference = false;
                    for (Reference ref : instruction.getReferencesFrom()) {
                        RefType type = ref.getReferenceType();
                        if (type.isCall()) {
                            hasCallReference = true;
                            Address targetAddress = ref.getToAddress();
                            Function target = manager.getFunctionAt(targetAddress);
                            if (target == null) target = manager.getFunctionContaining(targetAddress);
                            if (target != null && target.isThunk()) target = target.getThunkedFunction(true);
                            String kind;
                            String callee;
                            if (target != null && target.isExternal()) {
                                kind = "import";
                                ExternalLocation external = target.getExternalLocation();
                                String library = external != null ? external.getLibraryName() : "unknown-library";
                                callee = "external:" + library + "!" + target.getName();
                            } else if (type.isComputed()) {
                                kind = "indirect";
                                callee = target == null ? hex(targetAddress) : hex(target.getEntryPoint());
                            } else {
                                kind = "direct";
                                callee = target == null ? hex(targetAddress) : hex(target.getEntryPoint());
                            }
                            edges.printf("%s,%s,%s,%s,%s,%s%n", hex(function.getEntryPoint()), csv(callee),
                                kind, hex(instruction.getAddress()), csv("Ghidra instruction reference"),
                                csv(type.isComputed() ? "computed-reference" : "direct-reference"));
                        } else if (type.isData()) {
                            dataReferences.add(hex(ref.getToAddress()) + "|" + type.toString());
                        }
                    }
                    if (instruction.getFlowType().isCall() && !hasCallReference) {
                        String unresolvedKind = instruction.getFlowType().isComputed()
                            ? "unresolved_indirect" : "unresolved_direct";
                        edges.printf("%s,,%s,%s,%s,%s%n", hex(function.getEntryPoint()), unresolvedKind,
                            hex(instruction.getAddress()), csv("Ghidra call instruction without resolved target"),
                            csv("unresolved"));
                    }
                }
                StringBuilder refs = new StringBuilder("[");
                boolean first = true;
                for (String ref : dataReferences) {
                    if (!first) refs.append(',');
                    String[] parts = ref.split("\\|", 2);
                    refs.append("{\"address\":").append(jsonString(parts[0]))
                        .append(",\"reference_type\":").append(jsonString(parts[1])).append('}');
                    first = false;
                }
                refs.append(']');
                metrics.printf("%s,%d,%d,unknown,%s,%s,true,%s%n", hex(function.getEntryPoint()), instructionCount,
                    complexity, csv(refs.toString()), csv("ghidra-function-body"),
                    csv("Ghidra ExportFunctionAnalysis/1"));
            }
        }
        println("EXPORT function_analysis=" + functions.size());
    }
}
