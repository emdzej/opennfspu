// Lists, for every imported function, the functions that reference it (directly or through its IAT slot):
//   dll<TAB>import<TAB>function addresses (comma list)
// Usage: ghidra script run tools/ghidra/ImportUsers.java --project nfspu --program porsche.exe -- /abs/path/re/imports.tsv
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;
public class ImportUsers extends GhidraScript {
  public void run() throws Exception {
    PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]));
    SymbolTable st = currentProgram.getSymbolTable();
    ReferenceManager rm = currentProgram.getReferenceManager();
    FunctionManager fm = currentProgram.getFunctionManager();
    for (Symbol s : st.getExternalSymbols()) {
      Set<String> users = new TreeSet<>();
      List<Address> targets = new ArrayList<>();
      targets.add(s.getAddress());
      for (Reference r : rm.getReferencesTo(s.getAddress())) targets.add(r.getFromAddress());   // IAT slots, thunks
      for (Address t : targets)
        for (Reference r : rm.getReferencesTo(t)) {
          Function f = fm.getFunctionContaining(r.getFromAddress());
          if (f != null && !f.isThunk()) users.add(f.getEntryPoint().toString());
          else if (f != null) for (Reference r2 : rm.getReferencesTo(f.getEntryPoint())) {
            Function g = fm.getFunctionContaining(r2.getFromAddress());
            if (g != null) users.add(g.getEntryPoint().toString());
          }
        }
      String lib = s.getParentNamespace() != null ? s.getParentNamespace().getName() : "";
      w.println(lib + "\t" + s.getName() + "\t" + String.join(",", users));
    }
    w.close();
  }
}
