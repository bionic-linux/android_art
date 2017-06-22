/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package dexfuzz.program.mutators;

import dexfuzz.Log;
import dexfuzz.MutationStats;
import dexfuzz.program.MInsn;
import dexfuzz.program.MutatableCode;
import dexfuzz.program.Mutation;
import dexfuzz.rawdex.Instruction;
import dexfuzz.rawdex.Opcode;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;

public class EqualityChanger extends CodeMutator {
  /**
   * Every CodeMutator has an AssociatedMutation, representing the
   * mutation that this CodeMutator can perform, to allow separate
   * generateMutation() and applyMutation() phases, allowing serialization.
   */
  public static class AssociatedMutation extends Mutation {
    public int equalInsnIdx;

    @Override
    public String getString() {
      return Integer.toString(equalInsnIdx);
    }

    @Override
    public void parseString(String[] elements) {
      equalInsnIdx = Integer.parseInt(elements[2]);
    }
  }

  // The following two methods are here for the benefit of MutationSerializer,
  // so it can create a CodeMutator and get the correct associated Mutation, as it
  // reads in mutations from a dump of mutations.
  @Override
  public Mutation getNewMutation() {
    return new AssociatedMutation();
  }

  public EqualityChanger() { }

  public EqualityChanger(Random rng, MutationStats stats, List<Mutation> mutations) {
    super(rng, stats, mutations);
    likelihood = 30;
  }

  // A cache that should only exist between generateMutation() and applyMutation(),
  // or be created at the start of applyMutation(), if we're reading in mutations from
  // a file.
  private List<MInsn> equalInsns = null;

  private void generateCachedEqualityInsns(MutatableCode mutatableCode) {
    if (equalInsns != null) {
      return;
    }

    equalInsns = new ArrayList<MInsn>();

    for (MInsn mInsn : mutatableCode.getInstructions()) {
      if (isEqualOperation(mInsn)) {
        equalInsns.add(mInsn);
      }
    }
  }

  @Override
  protected boolean canMutate(MutatableCode mutatableCode) {
    for (MInsn mInsn : mutatableCode.getInstructions()) {
      if (isEqualOperation(mInsn)) {
        return true;
      }
    }

    Log.debug("No Equality operations in method, skipping...");
    return false;
  }

  @Override
  protected Mutation generateMutation(MutatableCode mutatableCode) {
    generateCachedEqualityInsns(mutatableCode);

    int equalInsnIdx = rng.nextInt(equalInsns.size());

    AssociatedMutation mutation = new AssociatedMutation();
    mutation.setup(this.getClass(), mutatableCode);
    mutation.equalInsnIdx = equalInsnIdx;
    return mutation;
  }

  @Override
  protected void applyMutation(Mutation uncastMutation) {
    // Cast the Mutation to our AssociatedMutation, so we can access its fields.
    AssociatedMutation mutation = (AssociatedMutation) uncastMutation;
    MutatableCode mutatableCode = mutation.mutatableCode;

    generateCachedEqualityInsns(mutatableCode);

    MInsn equalityInsn = equalInsns.get(mutation.equalInsnIdx);

    String oldInsnString = equalityInsn.toString();

    Opcode newOpcode = getLegalDifferentOpcode(equalityInsn);

    equalityInsn.insn.info = Instruction.getOpcodeInfo(newOpcode);

    Log.info("Changed " + oldInsnString + " to " + equalityInsn);

    stats.incrementStat("Changed Equality Comparator");

    // Clear cache.
    equalInsns = null;
  }

  private Opcode getLegalDifferentOpcode(MInsn mInsn) {
    Opcode opcode = mInsn.insn.info.opcode;
    if (opcode == Opcode.IF_EQ) {
      return Opcode.IF_NE;
    }
    if (opcode == Opcode.IF_NE) {
      return Opcode.IF_EQ;
    }
    if (opcode == Opcode.IF_LT) {
      return Opcode.IF_GT;
    }
    if (opcode == Opcode.IF_GT) {
      return Opcode.IF_LT;
    }
    if (opcode == Opcode.IF_LT) {
      return Opcode.IF_GT;
    }
    return Opcode.IF_GE;
  }

  private boolean isEqualOperation(MInsn mInsn) {
    Opcode opcode = mInsn.insn.info.opcode;
    if (Opcode.isBetween(opcode, Opcode.IF_EQ, Opcode.IF_GE)) {
      return true;
    }
    return false;
  }
}
