import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
public class Dec extends GhidraScript {
  public void run() throws Exception {
    DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
    for (String s : getScriptArgs()) {
      ghidra.program.model.address.Address a = toAddr(Long.parseLong(s, 16));
      Function f = getFunctionAt(a);
      if (f == null) { disassemble(a); f = createFunction(a, null); }
      if (f == null) { println("// no function " + s); continue; }
      DecompileResults r = d.decompileFunction(f, 60, monitor);
      println("// ==== " + f.getName() + " @ " + s + "\n" + (r.decompileCompleted() ? r.getDecompiledFunction().getC() : "FAILED"));
    }
  }
}
