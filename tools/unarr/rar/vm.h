/* Copyright 2014 the unarr project authors (see AUTHORS file).
   License: GPLv3 */

// adapted from https://code.google.com/p/theunarchiver/source/browse/XADMaster/RARVirtualMachine.h

#ifndef rar_vm_h
#define rar_vm_h

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

typedef struct RARVirtualMachine_s RARVirtualMachine;
typedef struct RAROpcode_s RAROpcode;

// Setup

void InitializeRARVirtualMachine(RARVirtualMachine *self);

// Program building

enum {
    RARMovInstruction = 0,
    RARCmpInstruction = 1,
    RARAddInstruction = 2,
    RARSubInstruction = 3,
    RARJzInstruction = 4,
    RARJnzInstruction = 5,
    RARIncInstruction = 6,
    RARDecInstruction = 7,
    RARJmpInstruction = 8,
    RARXorInstruction = 9,
    RARAndInstruction = 10,
    RAROrInstruction = 11,
    RARTestInstruction = 12,
    RARJsInstruction = 13,
    RARJnsInstruction = 14,
    RARJbInstruction = 15,
    RARJbeInstruction = 16,
    RARJaInstruction = 17,
    RARJaeInstruction = 18,
    RARPushInstruction = 19,
    RARPopInstruction = 20,
    RARCallInstruction = 21,
    RARRetInstruction = 22,
    RARNotInstruction = 23,
    RARShlInstruction = 24,
    RARShrInstruction = 25,
    RARSarInstruction = 26,
    RARNegInstruction = 27,
    RARPushaInstruction = 28,
    RARPopaInstruction = 29,
    RARPushfInstruction = 30,
    RARPopfInstruction = 31,
    RARMovzxInstruction = 32,
    RARMovsxInstruction = 33,
    RARXchgInstruction = 34,
    RARMulInstruction = 35,
    RARDivInstruction = 36,
    RARAdcInstruction = 37,
    RARSbbInstruction = 38,
    RARPrintInstruction = 39,
    RARNumberOfInstructions = 40,
};

void SetRAROpcodeInstruction(RAROpcode *opcode, uint8_t instruction, bool bytemode);
void SetRAROpcodeOperand1(RAROpcode *opcode, uint8_t addressingmode, uint32_t value);
void SetRAROpcodeOperand2(RAROpcode *opcode, uint8_t addressingmode, uint32_t value);
bool IsProgramTerminated(RAROpcode *opcodes, uint32_t numopcodes);
bool PrepareRAROpcodes(RAROpcode *opcodes, uint32_t numopcodes);

// Execution

bool ExecuteRARCode(RARVirtualMachine *self, RAROpcode *opcodes, uint32_t numopcodes);

// Instruction properties

int NumberOfRARInstructionOperands(uint8_t instruction);
bool RARInstructionHasByteMode(uint8_t instruction);
bool RARInstructionIsUnconditionalJump(uint8_t instruction);
bool RARInstructionIsRelativeJump(uint8_t instruction);
bool RARInstructionWritesFirstOperand(uint8_t instruction);
bool RARInstructionWritesSecondOperand(uint8_t instruction);

// Memory and register access

void SetRARVirtualMachineRegisters(RARVirtualMachine *self, uint32_t registers[8]);
uint32_t RARVirtualMachineRead32(RARVirtualMachine *self, uint32_t address);
void RARVirtualMachineWrite32(RARVirtualMachine *self, uint32_t address, uint32_t val);
uint8_t RARVirtualMachineRead8(RARVirtualMachine *self, uint32_t address);
void RARVirtualMachineWrite8(RARVirtualMachine *self, uint32_t address, uint8_t val);

#endif
