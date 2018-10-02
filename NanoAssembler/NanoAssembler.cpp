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
				if (index == 0)
					continue;
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
				continue;
			}
			instruction.line = std::regex_replace(line, std::regex("\\s{2,}"), " "); // replace all consecutive whitespaces with single space
			instruction.line.erase(std::remove(instruction.line.begin(), instruction.line.end(), ','), instruction.line.end()); // Remove ','
			std::transform(instruction.line.begin(), instruction.line.end(), instruction.line.begin(), ::tolower); // to lowercase
			lines.push_back(instruction);
		}
		return true;
	}
	return false;
}

int assembleInstruction(Mapper &mapper, std::string line, int i, std::vector<Instruction> &instructionBytes, std::unordered_map<std::string, unsigned int> labelMap) {
	std::istringstream iss(line);
	std::vector<std::string> parts(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>());
	//std::pair<unsigned char, unsigned int> opcode;
	Instruction &instruction = instructionBytes[i];
	// Check that the instruction is valid e.g. 'mov'
	if (!mapper.mapOpcode(parts[0], instruction)) {
		std::cout << "Error on line (" << i << "): " << line << std::endl;
		std::cout << "Unknown instruction \"" << parts[0] << std::endl;
		return -1;
	}
	// Check that there are required amount of parameters for the instruction e.g. 'mov reg0,reg1' requires 2
	if (instruction.operands != parts.size() - 1) {
		std::cout << "Error on line (" << i << "): " << line << std::endl;
		std::cout << "Invalid amount of parameters for instruction \"" << parts[0] << "\" expected: " << instruction.operands
			<< " but received: " << (parts.size() - 1) << std::endl;
		return -1;
	}
	//std::vector<unsigned char> instructionBytes;
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
			std::cout << "Error on line (" << i << "): " << line << std::endl;
			std::cout << "Invalid register name: \"" << parts[1] << "\"" << std::endl;
			return false;
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
			int size = mapper.mapImmediate(parts[2], instruction.bytecode, length);
			if (size == -1) {
				// parameter was not integer or register
				if (labelMap.find(parts[2]) == labelMap.end()) {
					std::cout << "Error on line (" << i << "): " << line << std::endl;
					std::cout << "Unknown parameter: \"" << parts[2] << "\"";
					return -1;
				}
				
			}
			else if (size == -2) {
				// immediate value couldn't fit in 64bit unsinged integer...
				std::cout << "Error on line (" << i << "): " << line << std::endl;
				std::cout << "Integer too large: " << parts[2] << std::endl;
				return -1;
			}
			// we now have the size of instruction. Update to the previous byte
			instructionBytes[1] = ((Type::Immediate | size | (isSrcMem ? SRC_MEM : 0) | (isDstMem ? DST_MEM : 0)));
		}
	}
	else if (opcode.second == 0) {
		// Instructions w/o operands can be pushed by the opcode (e.g. halt)
		instructionBytes.push_back((opcode.first));
	}
	else if (opcode.second == 1) {
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
			instructionBytes.push_back((opcode.first));
			instructionBytes.push_back((Type::Reg | SRC_SIZE | (isSrcMem ? SRC_MEM : 0) | srcReg));
		}
		else {
			// The single operand is immediate value
			instructionBytes.push_back(opcode.first);
			//Second parameter is immediate value. Add place holder byte
			instructionBytes.push_back(0);
			// parse the immediate value
			int size = mapper.mapImmediate(parts[1], instructionBytes);
			if (size == -1) {
				// parameter was not integer or register. So it has to be label
				// std::cout << "Error on line (" << i << "): " << line << std::endl;
				// std::cout << "Unknown parameter: \"" << parts[2] << "\"";

				return true;
			}
			else if (size == -2) {
				// immediate value couldn't fit in 64bit unsinged integer...
				std::cout << "Error on line (" << i << "): " << line << std::endl;
				std::cout << "Integer too large: " << parts[1] << std::endl;
				return false;
			}
			// we now have the size of instruction. Update to the previous byte
			instructionBytes[1] = ((Type::Immediate | size | (isSrcMem ? SRC_MEM : 0)));
		}
}

bool assemble(std::vector<Instruction> &instruction, std::unordered_map<std::string, unsigned int> &labelMap, std::vector<unsigned char> &bytes) {
	// iterate lines with 'i' so we have line number available
	Mapper mapper;
	std::vector<std::vector<unsigned char>> bytecode;
	std::unordered_map<std::string, unsigned int> labels;
	for (int i = 0; i < lines.size(); i++) {
		std::string line = lines[i];
		if (line.empty())
			continue;
		// Check if label
		if (line.length() > 1 && line[0] == ':') {
			// Add label to map
			labels[line.substr(1)] = bytecode.size();
			continue;
		}
		
			bytecode.push_back(instructionBytes);
		}
	}
	for (auto instruction : bytecode)
		for (auto b : instruction)
			bytes.push_back(b);
	return true;
}

int main(int argc, char *argv[])
{
	if (argc <= 1) {
		std::cout << "Usage NanoAssembler.exe [FILE]" << std::endl;
		return 0;
	}
	std::string input = argv[1];
	std::string output = input.substr(0, input.find_last_of('.')) + ".bin";
	std::vector<Instruction> lines;
	std::unordered_map<std::string, unsigned int> labelMap;
	std::vector<unsigned char> bytecode;
	readLines(argv[1], lines, labelMap);
	if (assemble(lines, labelMap, bytecode)) {
		std::ofstream file(output, std::ios::out | std::ios::binary);
		file.write((const char*)&bytecode[0], bytecode.size());
		file.close();
		std::cout << "Bytecode assembled!" << std::endl;
	}
	system("pause");
	return 0;
}
