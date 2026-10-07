// SPDX-License-Identifier: GPL-2.0-or-later
// Ghidra script (headless): every reference to some addresses, with the containing function.
//
//   RefsTo.java ADDRESS [ADDRESS ...]
//
// Prints "REF <to> <from> <type> <function entry> <function name>" per reference, for finding
// who constructs an object (code loads its vtable) or uses a table. Output only; changes nothing.
//@category Port
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class RefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            long to = Long.decode(arg);
            int n = 0;
            for (Reference r : getReferencesTo(toAddr(to))) {
                Function f = getFunctionContaining(r.getFromAddress());
                println(String.format("REF 0x%08x 0x%08x %s %s %s", to, r.getFromAddress().getOffset(), r.getReferenceType(),
                    f != null ? String.format("0x%08x", f.getEntryPoint().getOffset()) : "-", f != null ? f.getName() : "data"));
                n++;
            }
            if (n == 0) println(String.format("REF 0x%08x none", to));
        }
    }
}
