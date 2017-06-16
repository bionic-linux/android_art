/** TODO: http://go/java-style#javadoc */

package dexfuzz.program.mutators;

import dexfuzz.Log;
import dexfuzz.MutationStats;
import dexfuzz.program.MInsn;
import dexfuzz.program.MutatableCode;
import dexfuzz.program.Mutation;
import dexfuzz.rawdex.Opcode;
import java.util.List;
import java.util.Random;

public class InstructionDuplicator1 extends CodeMutator {
  /**
   * Every CodeMutator has an AssociatedMutation, representing the
   * mutation that this CodeMutator can perform, to allow separate
   * generateMutation() and applyMutation() phases, allowing serialization.
   */
  public static class AssociatedMutation extends Mutation {
    public int insnToDuplicateIdx;
    @Override
    public String getString() {
      return Integer.toString(insnToDuplicateIdx);
    }
    @Override
    public void parseString(String[] elements) {
      insnToDuplicateIdx = Integer.parseInt(elements[2]);
    }
  }
  
  @Override
  public Mutation getNewMutation() {
    return new AssociatedMutation();
  }
  InstructionDuplicator1() {}
  
  public InstructionDuplicator1(Random rng, MutationStats stats, List<Mutation> mutations) {
    super(rng, stats, mutations);
    likelihood = 70;
  }
  @Override
  protected Mutation generateMutation(MutatableCode mutatableCode) {
    boolean foundInsn= false;
    int insnIdx = 0;
    while (!foundInsn) {
      insnIdx = rng.nextInt(mutatableCode.getInstructionCount());
      MInsn oldInsn = mutatableCode.getInstructionAt(insnIdx);
      Opcode opcode = oldInsn.insn.info.opcode;
      if (opcode == Opcode.PACKED_SWITCH || opcode == Opcode.SPARSE_SWITCH || 
          opcode == Opcode.FILL_ARRAY_DATA || oldInsn.insn.justRaw) {
        continue;
      }
      foundInsn = true;
    }
      AssociatedMutation Mutation = new AssociatedMutation();
      Mutation.setup(this.getClass(),mutatableCode);
      Mutation.insnToDuplicateIdx = insnIdx;
    return Mutation;
  }
  @Override
  protected void applyMutation(Mutation uncastMutation) {
    // TODO(sumnima): Auto-generated method stub
    AssociatedMutation Mutation= (AssociatedMutation) uncastMutation;
    MutatableCode mutatableCode = Mutation.mutatableCode;
    MInsn oldInsn = mutatableCode.getInstructionAt(Mutation.insnToDuplicateIdx);
    MInsn newInsn = oldInsn.clone();
    Log.info("duplicating instruction" + oldInsn);
    
    stats.incrementStat("the instruction was duplicated");
    mutatableCode.insertInstructionAt(newInsn, Mutation.insnToDuplicateIdx);
  }
}
