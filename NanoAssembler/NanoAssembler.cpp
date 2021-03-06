#include "pch.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <regex>
#include <algorithm>
#include <sstream>
#include "Mapper.h"
#include "NanoAssembler.h"

bool readLines(std::string file, std::vector<Instruction> &lines, std::unordered_map<std::string, unsigned int> &labelMap) {
	std::string line;
	std::ifstream f(file);
	unsigned int lineNumber = 1;
	if (f.is_open()) {
		while (std::getline(f, line)) {
			Instruction instruction;
			instruction.assembled = false;
			instruction.length = 0;
			instruction.lineNumber = lineNumber;
			lineNumber++;
			// remove comments
			size_t index = line.find(";");
			if (index != -1) {
				if (index == 0) {
					continue;
				}
				line = line.substr(0, index);
			}
			line = std::regex_replace(line, std::regex("^\\s+|\\s+$"), ""); // trim leading and trailing whitespaces
			if (line.empty()) // Skip empty lines and comments (prefix ";")
			{
				continue;
			}
			if (line[0] == ':' && line.length() > 1) {
				// label
				labelMap[line.substr(1)] = lines.size();
				std::cout << "Label: " << line << std::endl;
				continue;
			}
			instruction.line = std::regex_replace(line, std::regex("\\s{2,}"), " "); // replace all consecutive whitespaces with single space
			instruction.line.erase(std::remove(instruction.line.begin(), instruction.line.end(), ','), instruction.line.end()); // Remove ','
			std::transform(instruction.line.begin(), instruction.line.end(), instruction.line.begin(), ::tolower); // to lowercase
			lines.push_back(instruction);
		}
		if (!lines.empty() && lines.at(lines.size() - 1).line != "halt") {
			std::cout << "Adding line \"halt\" to the end of file!" << std::endl;
			Instruction instruction;
			instruction.assembled = false;
			instruction.length = 0;
			instruction.lineNumber = lineNumber;
			instruction.line = "halt";
			lines.push_back(instruction);
		}
		return true;
	}
	return false;
}

int assembleInstruction(Mapper &mapper, int i, std::vector<Instruction> &instructionBytes, std::unordered_map<std::string, unsigned int> labelMap, bool initial) {
	// Skip already assembled instructions
	if (instructionBytes[i].assembled)
		return 1;
	std::istringstream iss(instructionBytes[i].line);
	std::vector<std::string> parts(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>());
	Instruction &instruction = instructionBytes[i];
	// Check that the instruction is valid e.g. 'mov'
	if (!mapper.mapOpcode(parts[0], instruction)) {
		std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
		std::cout << "Unknown instruction \"" << parts[0] << std::endl;
		return 0;
	}
	// Check that there are required amount of parameters for the instruction e.g. 'mov reg0,reg1' requires 2
	if (instruction.operands != parts.size() - 1) {
		std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
		std::cout << "Invalid amount of parameters for instruction \"" << parts[0] << "\" expected: " << instruction.operands
			<< " but received: " << (parts.size() - 1) << std::endl;
		return 0;
	}
	// assemble instruction with two operands
	if (instruction.operands == 2) {
		unsigned char dstReg;
		bool isDstMem = false, isSrcMem = false;
		// set flags whether the operands refer to memory address (operands are to be treated as pointers)
		if (parts[1][0] == '@') {
			parts[1] = parts[1].substr(1);
			isDstMem = true;
		}
		if (parts[2][0] == '@') {
			parts[2] = parts[2].substr(1);
			isSrcMem = true;
		}
		// parse destination register
		if (!mapper.mapRegister(parts[1], dstReg)) {
			std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
			std::cout << "Invalid register name: \"" << parts[1] << "\"" << std::endl;
			return 0;
		}
		// add first instruction byte
		instruction.bytecode[0]  = ((dstReg << 5) | (instruction.opcode));
		unsigned char srcReg;
		// parse source register if it exists (optional parameter)
		if (mapper.mapRegister(parts[2], srcReg)) {
			// second operand was register. add final instruction byte
			instruction.bytecode[1] = ((Type::Reg | SRC_SIZE | (isSrcMem ? SRC_MEM : 0) | (isDstMem ? DST_MEM : 0)) | srcReg);
			instruction.length = 2;
			instruction.assembled = true;
		}
		else {
			//Second parameter is immediate value
			unsigned int length = 0;
			int size = mapper.mapImmediate(parts[2], instruction.bytecode + 2, length);
			if (size == -1) {
				// parameter was not integer or register
				if (labelMap.find(parts[2]) == labelMap.end()) {
					std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
					std::cout << "Unknown parameter: \"" << parts[2] << "\"";
					return 0;
				}
				
			}
			else if (size == -2) {
				// immediate value couldn't fit in 64bit unsinged integer...
				std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
				std::cout << "Integer too large: " << parts[2] << std::endl;
				return 0;
			}
			// we now have the size of instruction. Update to the previous byte
			instruction.bytecode[1] = ((Type::Immediate | size | (isSrcMem ? SRC_MEM : 0) | (isDstMem ? DST_MEM : 0)));
			instruction.length = 2 + length;
			instruction.assembled = true;
		}
	}
	else if (instruction.operands == 0) {
		// Instructions w/o operands or with known to have one register can be pushed by the opcode (e.g. halt, inc reg0)
		instruction.bytecode[0] = instruction.opcode;
		instruction.length = 1;
		instruction.assembled = true;
		// Check if the instruction has register parameter
		if (parts.size() == 2) {
			unsigned char dstReg;
			if (!mapper.mapRegister(parts[1], dstReg)) {
				std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
				std::cout << "Invalid register name: \"" << parts[1] << "\"" << std::endl;
				return 0;
			}
			instruction.bytecode[0] |= (dstReg << 5);
		}
	}
	else if (instruction.operands == 1) {
		// Instruction has only one operand (e.g. jz, push, pop, ...)
		unsigned char srcReg;
		bool isSrcMem = false;
		// set flags whether the operands refer to memory address (operands are to be treated as pointers)
		if (parts[1][0] == '@') {
			parts[1] = parts[1].substr(1);
			isSrcMem = true;
		}
		// Check if the single operand is register
		if (mapper.mapRegister(parts[1], srcReg)) {
			// Operand is register
			instruction.bytecode[0] = instruction.opcode;
			instruction.bytecode[1] = ((Type::Reg | SRC_SIZE | (isSrcMem ? SRC_MEM : 0) | srcReg));
			instruction.length = 2;
			instruction.assembled = true;
		}
		else {
			// The single operand is immediate value
			instruction.bytecode[0] = instruction.opcode;
			unsigned int length = 0;
			// parse the immediate value
			int size = mapper.mapImmediate(parts[1], instruction.bytecode + 2, length);
			if (size == -1) {
				// parameter was not integer or register
				if (labelMap.find(parts[1]) == labelMap.end()) {
					std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
					std::cout << "Unknown parameter: \"" << parts[1] << "\"" << std::endl;
					return 0;
				}
				// Check if the label can already be mapped to an immediate value
				if (mapper.canMapLabel(parts[1], i, labelMap, instructionBytes)) {
					// Assemble the instruction
					int64_t value;
					length = mapper.mapLabel(parts[1], i, labelMap, instructionBytes, value);
					if (length == 0) {
						std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
						std::cout << "Failed to map label: \"" << parts[1] << "\"" << std::endl;
						return 0;
					}
					std::cout << "Mapped to " << value << std::endl;
					size = mapper.mapInteger(value, instruction.bytecode + 2, length);
				}
				else if (initial) {
					return -1;
				}
				else {
					size = mapper.calculateSizeRequirement(parts[1], i, labelMap, instructionBytes);
					if (size == 0) {
						std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
						std::cout << "Failed to map label: \"" << parts[1] << "\"" << std::endl;
						return 0;
					}
					instruction.length = size;
					std::cout << parts[1] << " require " << size << " bytes" << std::endl;
					std::cout << "Did not map label but defined size requirement" << std::endl;
					return -1;
				}
			}
			else if (size == -2) {
				// immediate value couldn't fit in 64bit unsinged integer...
				std::cout << "Error on line (" << i << "): " << instructionBytes[i].line << std::endl;
				std::cout << "Integer too large: " << parts[1] << std::endl;
				return 0;
			}
			// we now have the size of instruction. Update to the previous byte
			instruction.bytecode[1] = ((Type::Immediate | size | (isSrcMem ? SRC_MEM : 0)));
			instruction.length = 2 + length;
			instruction.assembled = true;
		}
	}
	return 1;
}

bool assemble(std::vector<Instruction> &instruction, std::unordered_map<std::string, unsigned int> &labelMap) {
	Mapper mapper;
	int rounds = 3;
	// Iterate over all instructions 3 times if needed because instructions with labels need other instructions to be assembled to calculate
	// relative distance from itself to the label
	bool reiterate = true;
	while (rounds-- && reiterate) {
		reiterate = false;
		bool ready = true;
		for (int i = 0; i < instruction.size(); i++) {
			int success = assembleInstruction(mapper, i, instruction, labelMap, rounds == 2);
			if (instruction[i].assembled)
				continue;
			ready &= success == 1;
			if (success == 0)
				return false;
			if (success == -1) {
				reiterate = true;
				std::cout << "require reiteration for mapping label" << std::endl;
			}
		}
		if (ready)
			return true;
	}
	return false;
}

int main(int argc, char *argv[])
{
	if (argc <= 1) {
		std::cout << "Usage NanoAssembler.exe [FILE]" << std::endl;
		system("pause");
		return 0;
	}
	std::string input = argv[1];
	std::string output = input.substr(0, input.find_last_of('.')) + ".nanoc";
	std::vector<Instruction> lines;
	std::unordered_map<std::string, unsigned int> labelMap;
	//std::vector<unsigned char> bytecode;
	readLines(argv[1], lines, labelMap);
	if (assemble(lines, labelMap)) {
		std::ofstream file(output, std::ios::out | std::ios::binary);
		for(Instruction inst:lines)
			file.write((const char*)&inst.bytecode[0], inst.length);
		file.close();
		std::cout << "Bytecode assembled!" << std::endl;
	}
	else {
		std::cout << "Assembler error" << std::endl;
	}
	system("pause");
	return 0;
}
