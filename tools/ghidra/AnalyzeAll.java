// Runs Ghidra's auto-analysis on the current program (for programs the bridge imported without analysing).
// Usage: ghidra script run tools/ghidra/AnalyzeAll.java --project nfspu --program dx7z.dll
import ghidra.app.script.GhidraScript;
public class AnalyzeAll extends GhidraScript {
  public void run() throws Exception {
    analyzeAll(currentProgram);
    println("functions: " + currentProgram.getFunctionManager().getFunctionCount());
  }
}
