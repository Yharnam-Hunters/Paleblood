// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only, bounded structural RTTI/vtable evidence. No bytes or decompiler output exported.
// analyzeHeadless DIR PROJECT -process eboot.elf -readOnly -noanalysis \
//   -scriptPath tools/ghidra -postScript ExportTypeIntelligence.java OUT.json [SCAN_BYTES]
//@category Export
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.*;
import com.google.gson.GsonBuilder;
import ghidra.framework.Application;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;
import ghidra.program.model.symbol.SymbolIterator;

public class ExportTypeIntelligence extends GhidraScript {
    private static final int SLOT_LIMIT = 256;
    private static final int REFERENCE_LIMIT = 64;
    private static final int CHUNK = 1 << 20;
    private final Map<Long, Function> functions = new HashMap<>();
    private final Map<Long, Map<String, Object>> types = new TreeMap<>();
    private final Set<Long> rejectedTypes = new HashSet<>();
    private final Map<Long, Map<String, Object>> tables = new TreeMap<>();
    private final List<Map<String, Object>> relations = new ArrayList<>();

    private static Map<String, Object> record(Object... pairs) {
        Map<String, Object> result = new LinkedHashMap<>();
        for (int i = 0; i < pairs.length; i += 2) result.put((String)pairs[i], pairs[i + 1]);
        return result;
    }
    private static String hex(long value) { return String.format("0x%08x", value); }
    private Address addr(long value) { return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value); }
    private boolean dataPointer(long value) {
        if (value <= 0) return false;
        MemoryBlock block = currentProgram.getMemory().getBlock(addr(value));
        // Constants can share an RX segment; execute permission does not invalidate data.
        return block != null && block.isInitialized() && block.isRead();
    }
    private long word(long value) throws Exception { return currentProgram.getMemory().getLong(addr(value)); }
    private String metaSymbolName(long addressPoint) {
        // Imported vtables live in Ghidra's uninitialized EXTERNAL block. Their
        // address point is symbol + 16; no imported object body is read here.
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(addr(addressPoint - 16))) {
            String name = symbol.getName(true);
            if (name.contains("__si_class_type_info") || name.contains("__vmi_class_type_info") ||
                name.contains("__class_type_info")) return name;
        }
        return "";
    }
    private String encodedName(long pointer) throws Exception {
        if (!dataPointer(pointer)) return null;
        StringBuilder name = new StringBuilder();
        for (int i = 0; i < 512; i++) {
            int c = currentProgram.getMemory().getByte(addr(pointer + i)) & 255;
            if (c == 0) return name.toString().matches("\\*?(?:N|St|[0-9])[A-Za-z0-9_$.*]+") ? name.toString() : null;
            if (c < 33 || c > 126) return null;
            name.append((char)c);
        }
        return null;
    }
    private Map<String, Object> typeAt(long pointer) {
        if (types.containsKey(pointer)) return types.get(pointer);
        if (rejectedTypes.contains(pointer) || !dataPointer(pointer)) return null;
        rejectedTypes.add(pointer);
        try {
            long metaTable = word(pointer);
            String encoded = encodedName(word(pointer + 8));
            String metaName = metaSymbolName(metaTable);
            if ((!dataPointer(metaTable) && metaName.isEmpty()) || encoded == null) return null;
            List<Map<String, Object>> bases = new ArrayList<>();
            String baseStatus = "unknown";
            if (metaName.contains("__si_class_type_info")) {
                long base = word(pointer + 16);
                if (dataPointer(base)) {
                    bases.add(record("type_id", "rtti:" + hex(base), "offset_flags", null,
                                     "source", "Itanium single-inheritance RTTI layout with identified meta-vtable symbol",
                                     "confidence", "structural_candidate"));
                    baseStatus = "bounded_abi_layout_candidate";
                }
            } else if (metaName.contains("__vmi_class_type_info")) {
                int count = currentProgram.getMemory().getInt(addr(pointer + 20));
                if (count >= 0 && count <= 64) {
                    for (int i = 0; i < count; i++) {
                        long base = word(pointer + 24 + 16L * i);
                        if (!dataPointer(base)) throw new IllegalArgumentException("unmapped RTTI base");
                        bases.add(record("type_id", "rtti:" + hex(base), "offset_flags", word(pointer + 32 + 16L * i),
                                         "source", "Itanium multiple-inheritance RTTI layout with identified meta-vtable symbol",
                                         "confidence", "structural_candidate"));
                    }
                    baseStatus = "bounded_abi_layout_candidate";
                }
            } else if (metaName.contains("__class_type_info")) {
                baseStatus = "bounded_abi_layout_candidate";
            }
            Map<String, Object> type = record("id", "rtti:" + hex(pointer), "address", hex(pointer),
                "name", null, "encoded_name", encoded, "bases", bases, "bases_status", baseStatus,
                "source", metaName.isEmpty() ? "mapped data vptr/name-pointer pair referenced by candidate vtable header" :
                    "typeinfo vptr at identified RTTI meta-vtable symbol + 16, with mapped encoded name",
                "confidence", "structural_candidate");
            types.put(pointer, type);
            return type;
        } catch (Exception error) { return null; }
    }
    private void inspectTable(long header, long top, long typePointer) throws Exception {
        long addressPoint = header + 16;
        if (tables.containsKey(addressPoint)) return;
        Map<String, Object> type = typePointer == 0 ? null : typeAt(typePointer);
        if (typePointer != 0 && type == null) return;
        List<Map<String, Object>> slots = new ArrayList<>();
        for (int i = 0; i < SLOT_LIMIT; i++) {
            long target;
            try { target = word(addressPoint + 8L * i); } catch (Exception error) { break; }
            Function function = functions.get(target);
            if (function == null) break;
            Function thunkTarget = function.isThunk() ? function.getThunkedFunction(true) : null;
            slots.add(record("index", i, "function", hex(target), "thunk_target",
                             thunkTarget == null ? null : hex(thunkTarget.getEntryPoint().getOffset())));
        }
        if (slots.size() < (type == null ? 2 : 1)) return;
        String typeId = type == null ? null : (String)type.get("id");
        String source = type == null ? "offset/zero-typeinfo header and contiguous Ghidra function pointers; may be another pointer table"
                                    : "offset/typeinfo header and contiguous Ghidra function pointers";
        tables.put(addressPoint, record("address", hex(addressPoint), "type_id", typeId,
            "offset_to_top", top, "slots", slots, "extent", "bounded_prefix_unknown_full_extent",
            "source", source, "confidence", "structural_candidate"));
        int count = 0;
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(addr(addressPoint));
        while (references.hasNext()) {
            Reference ref = references.next();
            if (count++ == REFERENCE_LIMIT) break;
            Function function = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
            if (function != null) relations.add(record("function", hex(function.getEntryPoint().getOffset()),
                "type_id", typeId, "vtable", hex(addressPoint), "role", "vtable_reference_candidate",
                "reference", hex(ref.getFromAddress().getOffset()), "source", "Ghidra reference to candidate vtable address point; not proof of constructor",
                "confidence", "candidate"));
        }
    }
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1 || args.length > 2) throw new IllegalArgumentException("OUT.json [SCAN_BYTES]");
        if (currentProgram.getImageBase().getOffset() != 0x400000L || currentProgram.getLanguage().isBigEndian())
            throw new IllegalStateException("requires the little-endian target at image base 0x400000");
        long limit = args.length == 2 ? Long.parseLong(args[1]) : 256L << 20;
        if (limit <= 0 || limit > (1L << 30)) throw new IllegalArgumentException("scan budget must be 1..1073741824 bytes");
        List<Map<String, Object>> symbols = new ArrayList<>();
        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            if (function.isExternal()) continue;
            functions.put(function.getEntryPoint().getOffset(), function);
            if (function.isThunk() || !function.getName().startsWith("FUN_")) {
                symbols.add(record("address", hex(function.getEntryPoint().getOffset()), "name", function.getName(true),
                    "thunk", function.isThunk(), "source", "Ghidra function symbol and thunk analysis",
                    "confidence", function.isThunk() ? "analysis_supported" : "symbol_supported"));
            }
            Namespace parent = function.getParentNamespace();
            if (parent != null && !parent.isGlobal() && parent.getSymbol().getSymbolType() == SymbolType.CLASS) {
                String id = "namespace:" + parent.getName(true);
                String role = function.getName().startsWith("~") ? "destructor_symbol_candidate" :
                              function.getName().equals(parent.getName()) ? "constructor_symbol_candidate" : "class_namespace";
                relations.add(record("function", hex(function.getEntryPoint().getOffset()), "type_id", id,
                    "vtable", null, "role", role, "reference", null, "source", "Ghidra class-namespace symbol",
                    "confidence", "symbol_supported"));
            }
        }
        long scanned = 0;
        boolean truncated = false;
        // Offset references to imported RTTI meta-vtables identify standalone typeinfo
        // records even when the imported vtable body is not present in this program.
        SymbolIterator allSymbols = currentProgram.getSymbolTable().getAllSymbols(true);
        while (allSymbols.hasNext()) {
            Symbol symbol = allSymbols.next();
            if (!symbol.getName(true).contains("class_type_info")) continue;
            ReferenceIterator uses = currentProgram.getReferenceManager().getReferencesTo(symbol.getAddress());
            while (uses.hasNext()) {
                Reference use = uses.next();
                if (!use.getFromAddress().isMemoryAddress()) continue;
                long pointer = use.getFromAddress().getOffset();
                if ((pointer & 7) == 0) typeAt(pointer);
            }
        }
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized() || !block.isRead() || block.isExecute()) continue;
            long start = (block.getStart().getOffset() + 7) & ~7L;
            long end = block.getEnd().getOffset() + 1;
            for (long cursor = start; cursor + 24 <= end; cursor += CHUNK) {
                monitor.checkCancelled();
                if (scanned >= limit) { truncated = true; break; }
                int length = (int)Math.min(Math.min(CHUNK + 16L, end - cursor), limit - scanned);
                byte[] bytes = new byte[length];
                currentProgram.getMemory().getBytes(addr(cursor), bytes);
                ByteBuffer buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
                for (int offset = 0; offset + 24 <= length && offset < CHUNK; offset += 8) {
                    long top = buffer.getLong(offset);
                    if (top > 0 || top < -(1L << 24) || !functions.containsKey(buffer.getLong(offset + 16))) continue;
                    inspectTable(cursor + offset, top, buffer.getLong(offset + 8));
                }
                scanned += Math.min(CHUNK, length);
            }
            if (truncated) break;
        }
        List<Map<String, Object>> typeRecords = new ArrayList<>(types.values());
        Set<String> namespaces = new TreeSet<>();
        for (Map<String, Object> relation : relations) {
            String id = (String)relation.get("type_id");
            if (id != null && id.startsWith("namespace:") && namespaces.add(id)) typeRecords.add(record(
                "id", id, "address", null, "name", id.substring(10).matches("class_[0-9a-fA-F]+") ? null : id.substring(10),
                "encoded_name", null, "bases", new ArrayList<>(), "bases_status", "unknown",
                "source", "Ghidra class namespace; address-derived labels remain unnamed", "confidence", "symbol_supported"));
        }
        Map<String, Object> output = record("schema", 1, "executable_sha256", currentProgram.getExecutableSHA256(),
            "analysis_version", "Ghidra " + Application.getApplicationVersion() + " / ExportTypeIntelligence 2",
            "types", typeRecords, "vtables", new ArrayList<>(tables.values()), "relations", relations, "functions", symbols,
            "scan", record("bytes_scanned", scanned, "limit_bytes", limit, "truncated", truncated, "slot_limit", SLOT_LIMIT,
                "limitations", Arrays.asList("Itanium-shaped structural candidates, not independently confirmed C++ layouts",
                    "Only existing Ghidra function entry targets; zero/import/unknown slots end each bounded prefix",
                    "At most 64 references per table; references alone do not identify constructors or destructors",
                    "Inheritance unknown unless the RTTI meta-vtable symbol identifies an ABI layout",
                    "No function behavior, calling convention or verification evidence inferred")));
        try (java.io.Writer writer = Files.newBufferedWriter(Paths.get(args[0]), StandardCharsets.UTF_8)) {
            new GsonBuilder().serializeNulls().setPrettyPrinting().create().toJson(output, writer);
        }
        println("EXPORT types=" + typeRecords.size() + " candidate_tables=" + tables.size() + " scanned_bytes=" + scanned + " truncated=" + truncated);
    }
}
