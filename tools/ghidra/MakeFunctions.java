// Disassembles and creates functions at the given addresses (code the auto-analysis left undefined, such
// as virtual methods reached only through vtables). Prints each result.
// Usage: ghidra script run tools/ghidra/MakeFunctions.java --project nfspu --program porsche.exe -- 0x418510 ...
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
public class MakeFunctions extends GhidraScript {
  public void run() throws Exception {
    for (String s : getScriptArgs()) {
      Address a = toAddr(Long.decode(s));
      new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
      Function f = getFunctionAt(a);
      if (f == null) f = createFunction(a, null);
      println(s + " -> " + (f == null ? "failed" : f.getName() + " " + f.getBody().getNumAddresses() + " bytes"));
    }
  }
}
