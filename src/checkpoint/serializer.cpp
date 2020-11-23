//
// Created by zyy on 2020/11/16.
//

#include "checkpoint/path_manager.h"
#include "checkpoint/serializer.h"
#include "../isa/riscv64/local-include/csr.h"

#include <cinttypes>
#include <iostream>
#include <zlib.h>
#include <limits>

#include <isa.h>

#include <fstream>
#include <memory/paddr.h>
#include <monitor/monitor.h>
#include <gcpt_restore/src/restore_rom_addr.h>

using namespace std;

Serializer::Serializer() :
    IntRegStartAddr(INT_REG_CPT_ADDR - BOOT_CODE),
    FloatRegStartAddr(FLOAT_REG_CPT_ADDR - BOOT_CODE),
    CSRStartAddr(CSR_CPT_ADDR - BOOT_CODE),
    PCAddr(PC_CPT_ADDR - BOOT_CODE),
    CptFlagAddr(BOOT_FLAGS - BOOT_CODE)
{

}


void Serializer::serializePMem() {
  // We must dump registers before memory to store them in the Generic Arch CPT
  assert(regDumped);

  uint8_t *pmem = getPmem();
  string filepath = pathManager.getOutputPath() + "_" + to_string(simpoint2Weights.begin()->second) + "_.gz";

  gzFile compressed_mem = gzopen(filepath.c_str(), "wb");
  if (compressed_mem == nullptr) {
    cerr << "Failed to open " << filepath << endl;
    panic("Can't open physical memory checkpoint file!\n");
  } else {
    cout << "Opening " << filepath << " as checkpoint output file" << endl;
  }

  uint64_t pass_size = 0;

  for (uint64_t written = 0; written < PMEM_SIZE; written += pass_size) {
    pass_size = numeric_limits<int>::max() < ((int64_t) PMEM_SIZE - (int64_t) written) ?
                numeric_limits<int>::max() : ((int64_t) PMEM_SIZE - (int64_t) written);

    if (gzwrite(compressed_mem, pmem + written, (uint32_t) pass_size) != (int) pass_size) {
      panic("Write failed on physical memory checkpoint file\n");
    }
    Log("Written 0x%x bytes\n", pass_size);
  }

  if (gzclose(compressed_mem)){
    panic("Close failed on physical memory checkpoint file\n");
  }
  Log("Checkpoint done!\n");
  regDumped = false;
}

extern void csr_writeback();

void Serializer::serializeRegs() {
  auto *intRegCpt = (uint64_t *) (getPmem() + IntRegStartAddr);
  for (unsigned i = 0; i < 32; i++) {
    *(intRegCpt + i) = cpu.gpr[i]._64;
  }
  Log("Writing int registers to checkpoint memory @[0x%x, 0x%x) [0x%x, 0x%x)",
      INT_REG_CPT_ADDR, INT_REG_CPT_ADDR + 32 * 8,
      IntRegStartAddr, IntRegStartAddr + 32 * 8
      );


  auto *floatRegCpt = (uint64_t *) (getPmem() + FloatRegStartAddr);
  for (unsigned i = 0; i < 32; i++) {
    *(floatRegCpt + i) = cpu.fpr[i]._64;
  }
  Log("Writing float registers to checkpoint memory @[0x%x, 0x%x) [0x%x, 0x%x)",
      FLOAT_REG_CPT_ADDR, FLOAT_REG_CPT_ADDR + 32 * 8,
      FloatRegStartAddr, FloatRegStartAddr + 32 * 8
      );


  auto *pc = (uint64_t *) (getPmem() + PCAddr);
  *pc = cpu.pc;
  Log("Writing PC: 0x%lx at addr 0x%x", cpu.pc, PC_CPT_ADDR);


//  csr_writeback();
  auto *csrCpt = (uint64_t *) (getPmem() + CSRStartAddr);
//  Log("csrCpt: %p\n",csrCpt);
//  Log("Mstatus: 0x%x", mstatus->val);
//  Log("CSR array mstatus: 0x%x", csr_array[0x300]);
  for (unsigned i = 0; i < 4096; i++) {
    *(csrCpt + i) = csr_array[i];
//    Log("csrCpt + %i: %p\n", i, csrCpt + i);
//    if (csr_array[i] != 0) {
//      Log("CSR 0x%x: 0x%x", i, csr_array[i]);
//    }
  }
  Log("Writing CSR to checkpoint memory @[0x%x, 0x%x) [0x%x, 0x%x)",
      CSR_CPT_ADDR, CSR_CPT_ADDR + 4096 * 8,
      CSRStartAddr, CSRStartAddr + 4096 * 8
      );


  auto *flag = (uint64_t *) (getPmem() + CptFlagAddr);
  *flag = CPT_MAGIC_BUMBER;
  Log("Touching Flag: 0x%x at addr 0x%x", CPT_MAGIC_BUMBER, BOOT_FLAGS);

  regDumped = true;
}

void Serializer::serialize() {
//  isa_reg_display();
  serializeRegs();
  serializePMem();

  simpoint2Weights.erase(simpoint2Weights.begin());

  if (!simpoint2Weights.empty()) {
    pathManager.incCptID();
  }
//  isa_reg_display();
}

void Serializer::deserialize(const char *file) {

  FILE *fp = fopen(file, "rb");
  if (fp == NULL) Log("Checkpoint not found\n");
  Assert(fp, "Can not open '%s'", file);

  Log("Opening restorer file: %s", file);
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  if (size < PMEM_SIZE) {
    Log("Cpt size = %ld is too large, only load 0xlu", size, PMEM_SIZE);
    size = PMEM_SIZE;
  }

  fseek(fp, 0, SEEK_SET);
  int ret = fread(guest_to_host(RESTORER_START), size, 1, fp);
  assert(ret == 1);
}

void Serializer::init() {
  if  (simpoint_state == SimpointCheckpointing) {
    assert(simpoint_interval);
    intervalSize = simpoint_interval;
    Log("Taking simpoint checkpionts with interval %lu", intervalSize);

    auto simpoints_file = fstream(pathManager.getSimpointPath() + "simpoints0");
    auto weights_file = fstream(pathManager.getSimpointPath() + "weights0");
    assert(!simpoints_file.bad());
    assert(!weights_file.bad());

    uint64_t simpoint_location, simpoint_id, weight_id;
    double weight;

    while (simpoints_file >> simpoint_location >> simpoint_id) {
      assert(weights_file >> weight >> weight_id);
      assert(weight_id == simpoint_id);
      simpoint2Weights[simpoint_location] = weight;

      Log("Simpoint %i: @ %lu, weight: %f", simpoint_id, simpoint_location, weight);
    }
  }
}

bool Serializer::shouldTakeCpt(uint64_t num_insts) {
  if (simpoint_state != SimpointCheckpointing ||
      simpoint2Weights.empty()) {
    return false;
  }
  uint64_t next_point = simpoint2Weights.begin()->first * intervalSize + 100000;
  if (num_insts == next_point) {
    Log("Should take cpt now: %lu", num_insts);
    return true;
  } else if (num_insts % intervalSize == 0) {
    Log("First cpt @ %lu, now: %lu",
        next_point, num_insts);
  }
  return false;
}

void Serializer::notify_taken(uint64_t i) {

}

Serializer serializer;

void init_serializer() {
  serializer.init();
}