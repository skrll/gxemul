/*
 *  Copyright (C) 2008  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 */

#include <assert.h>
#include "components/MIPS_CPUComponent.h"

#include "mips_cpu_types.h"
#include "opcodes_mips.h"

static const char* hi6_names[] = HI6_NAMES;
static const char* regnames[] = MIPS_REGISTER_NAMES;
static const char* special_names[] = SPECIAL_NAMES;
static const char* regimm_names[] = REGIMM_NAMES;
static mips_cpu_type_def cpu_type_defs[] = MIPS_CPU_TYPE_DEFS;


MIPS_CPUComponent::MIPS_CPUComponent()
	: CPUComponent("mips_cpu", "MIPS")
	, m_mips_type("5KE")	// defaults to a MIPS64 rev 2 cpu
	, m_hi(0)
	, m_lo(0)
{
	AddVariableString("mips_type", &m_mips_type);

	AddVariableUInt64("hi", &m_hi);
	AddVariableUInt64("lo", &m_lo);

	// TODO: GPR 0 (ZERO) is NOT writable!
	for (size_t i=0; i<N_MIPS_GPRS; i++)
		AddVariableUInt64(regnames[i], &m_gpr[i]);


	// MIPS CPUs are hardwired to start at 0xffffffffbfc00000:
	m_pc = MIPS_INITIAL_PC;

	// Reasonable initial stack pointer.
	m_gpr[MIPS_GPR_SP] = MIPS_INITIAL_STACK_POINTER;

	// Most MIPS CPUs use 4 KB native page size.
	// TODO: A few use 1 KB pages; this should be supported as well.
	m_pageSize = 4096;

	m_frequency = 100e6;

	// Find (and cache) the cpu type in m_type:
	memset((void*) &m_type, 0, sizeof(m_type));
	for (size_t j=0; cpu_type_defs[j].name != NULL; j++) {
		if (m_mips_type == cpu_type_defs[j].name) {
			m_type = cpu_type_defs[j];
			break;
		}
	}

	if (m_type.name == NULL) {
		std::cerr << "Internal error: Unimplemented MIPS type?\n";
		throw std::exception();
	}
}


refcount_ptr<Component> MIPS_CPUComponent::Create()
{
	return new MIPS_CPUComponent();
}


int MIPS_CPUComponent::Run(int nrOfCycles)
{
	int nrOfCyclesExecuted = 0;

	// Check for interrupts. TODO
	// (This may cause an exception, i.e. a change of PC and other state.)

	while (nrOfCycles-- > 0) {
		bool mips16 = (m_pc & 1) != 0;

		// Read an instruction from emulated memory and execute it:
		if (mips16) {
			uint16_t iword;
			if (!ReadInstructionWord(iword, m_pc & ~1)) {
				std::cerr << "TODO: MIPS: no instruction?\n";
				throw std::exception();
			}
			ExecuteMIPS16Instruction(iword);
		} else {
			uint32_t iword;
			if (!ReadInstructionWord(iword, m_pc)) {
				std::cerr << "TODO: MIPS: no instruction?\n";
				throw std::exception();
			}
			ExecuteInstruction(iword);
		}

		++ nrOfCyclesExecuted;
	}

	return nrOfCyclesExecuted;
}


void MIPS_CPUComponent::ExecuteMIPS16Instruction(uint16_t iword)
{
	// TODO: switch/case for all instructions
	std::cerr.flags(std::ios::hex | std::ios::showbase);	
	std::cerr << "EXECUTE iword16 " << iword << " at pc " << m_pc << "\n";
	std::cerr << "TODO: MIPS: unimplemented instruction\n";
	throw std::exception();
}


void MIPS_CPUComponent::ExecuteInstruction(uint32_t iword)
{
	// TODO: switch/case for all instructions
	std::cerr.flags(std::ios::hex | std::ios::showbase);	
	std::cerr << "EXECUTE iword32 " << iword << " at pc " << m_pc << "\n";
	std::cerr << "TODO: MIPS: unimplemented instruction\n";
	throw std::exception();
}


bool MIPS_CPUComponent::VirtualToPhysical(uint64_t vaddr, uint64_t& paddr,
	bool& writable)
{
	// TODO. For now, just return the lowest 30 bits.

	paddr = vaddr & 0x3fffffff;
	writable = true;
	return true;
}


size_t MIPS_CPUComponent::DisassembleInstructionMIPS16(uint64_t vaddr,
	unsigned char *instruction, vector<string>& result)
{
	// Read the instruction word:
	uint16_t iword = *((uint16_t *) instruction);
	if (m_endianness == BigEndian)
		iword = BE16_TO_HOST(iword);
	else
		iword = LE16_TO_HOST(iword);

	// ... and add it to the result:
	char tmp[5];
	snprintf(tmp, sizeof(tmp), "%04x", iword);
	result.push_back(tmp);

	// TODO
	result.push_back("unimplemented instruction");

	return sizeof(uint16_t);
}


size_t MIPS_CPUComponent::DisassembleInstruction(uint64_t vaddr, size_t maxLen,
	unsigned char *instruction, vector<string>& result)
{
	bool mips16 = vaddr & 1? true : false;
	size_t instrSize = mips16? sizeof(uint16_t) : sizeof(uint32_t);

	if (maxLen < instrSize) {
		assert(false);
		return 0;
	}

	if (mips16)
		return DisassembleInstructionMIPS16(vaddr,
		    instruction, result);

	// Read the instruction word:
	uint32_t iword = *((uint32_t *) instruction);
	if (m_endianness == BigEndian)
		iword = BE32_TO_HOST(iword);
	else
		iword = LE32_TO_HOST(iword);

	// ... and add it to the result:
	char tmp[9];
	snprintf(tmp, sizeof(tmp), "%08x", iword);
	result.push_back(tmp);

	int hi6 = iword >> 26;
	int rs = (iword >> 21) & 31;
	int rt = (iword >> 16) & 31;

	switch (hi6) {

	case HI6_SPECIAL:
		{
			int special = iword & 0x3f;
			switch (special) {

			default:
				{
					stringstream ss;
					ss << "unimplemented instruction: " <<
					    special_names[special];
					result.push_back(ss.str());
				}
				break;
			}
		}
		break;

	case HI6_BEQ:
	case HI6_BEQL:
	case HI6_BNE:
	case HI6_BNEL:
	case HI6_BGTZ:
	case HI6_BGTZL:
	case HI6_BLEZ:
	case HI6_BLEZL:
		{
			int imm = (int16_t) iword;
			uint64_t addr = vaddr + 4 + (imm << 2);

			stringstream ss;

			if (hi6 == HI6_BEQ && rt == MIPS_GPR_ZERO &&
			    rs == MIPS_GPR_ZERO) {
				result.push_back("b");
			} else {
				result.push_back(hi6_names[hi6]);

				switch (hi6) {
				case HI6_BEQ:
				case HI6_BEQL:
				case HI6_BNE:
				case HI6_BNEL:
					ss << regnames[rt] << ",";
				}

				ss << regnames[rs] << ",";
			}

			ss.flags(std::ios::hex | std::ios::showbase);
			ss << addr;
			result.push_back(ss.str());

			// TODO: symbol lookup
		}
		break;

	case HI6_ADDI:
	case HI6_ADDIU:
	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_SLTI:
	case HI6_SLTIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		{
			result.push_back(hi6_names[hi6]);
			
			stringstream ss;
			ss << regnames[rt] << "," << regnames[rs] << ",";
			if (hi6 == HI6_ANDI || hi6 == HI6_ORI ||
			    hi6 == HI6_XORI) {
				ss.flags(std::ios::hex | std::ios::showbase);
				ss << (uint16_t) iword;
			} else {
				ss << (int16_t) iword;
			}
			result.push_back(ss.str());
		}
		break;

	case HI6_LUI:
		{
			result.push_back(hi6_names[hi6]);
			
			stringstream ss;
			ss << regnames[rt] << ",";
			ss.flags(std::ios::hex | std::ios::showbase);
			ss << (uint16_t) iword;
			result.push_back(ss.str());
		}
		break;

	case HI6_LB:
	case HI6_LBU:
	case HI6_LH:
	case HI6_LHU:
	case HI6_LW:
	case HI6_LWU:
	case HI6_LD:
	case HI6_LQ_MDMX:
	case HI6_LWC1:
	case HI6_LWC2:
	case HI6_LWC3:
	case HI6_LDC1:
	case HI6_LDC2:
	case HI6_LL:
	case HI6_LLD:
	case HI6_SB:
	case HI6_SH:
	case HI6_SW:
	case HI6_SD:
	case HI6_SQ_SPECIAL3:
	case HI6_SC:
	case HI6_SCD:
	case HI6_SWC1:
	case HI6_SWC2:
	case HI6_SWC3:
	case HI6_SDC1:
	case HI6_SDC2:
	case HI6_LWL:   
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		{
		
			if (hi6 == HI6_LQ_MDMX && m_type.rev != MIPS_R5900) {
				result.push_back("mdmx (UNIMPLEMENTED)");
				break;
			}
			if (hi6 == HI6_SQ_SPECIAL3 && m_type.rev!=MIPS_R5900) {
				result.push_back("special3 (UNIMPLEMENTED)");
				break;
			}


			int imm = (int16_t) iword;
			stringstream ss;

			/*  LWC3 is PREF in the newer ISA levels:  */
			/*  TODO: Which ISAs? IV? V? 32? 64?  */
			if (m_type.isa_level >= 4 && hi6 == HI6_LWC3) {
				result.push_back("pref");
				
				ss << rt << "," << imm <<
				    "(" << regnames[rs] << ")";
				result.push_back(ss.str());
				break;
			}

			result.push_back(hi6_names[hi6]);

			if (hi6 == HI6_SWC1 || hi6 == HI6_SWC2 ||
			    hi6 == HI6_SWC3 ||
			    hi6 == HI6_SDC1 || hi6 == HI6_SDC2 ||
			    hi6 == HI6_LWC1 || hi6 == HI6_LWC2 ||
			    hi6 == HI6_LWC3 ||
			    hi6 == HI6_LDC1 || hi6 == HI6_LDC2)
				ss << "r" << rt;
			else
				ss << regnames[rt];

			ss << "," << imm << "(" << regnames[rs] << ")";

			result.push_back(ss.str());

			// TODO: Symbol lookup, if running.
		}
		break;

	case HI6_J:
	case HI6_JAL:
		{
			result.push_back(hi6_names[hi6]);
			
			int imm = (iword & 0x03ffffff) << 2;
			uint64_t addr = (vaddr + 4) & ~((1 << 28) - 1);
			addr |= imm;

			stringstream ss;
			ss.flags(std::ios::hex | std::ios::showbase);
			ss << addr;
			result.push_back(ss.str());
			
			// TODO: Symbol lookup.
		}
		break;

	// CopX here. TODO
	// Cache
	// Special2

	case HI6_REGIMM:
		{
			int regimm5 = (iword >> 16) & 0x1f;
			int imm = (int16_t) iword;
			uint64_t addr = (vaddr + 4) + (imm << 2);

			stringstream ss;
			ss.flags(std::ios::hex | std::ios::showbase);

			switch (regimm5) {

			case REGIMM_BLTZ:
			case REGIMM_BGEZ:
			case REGIMM_BLTZL:
			case REGIMM_BGEZL:
			case REGIMM_BLTZAL:
			case REGIMM_BLTZALL:
			case REGIMM_BGEZAL:
			case REGIMM_BGEZALL:
				result.push_back(regimm_names[regimm5]);

				ss << regnames[rs] << "," << addr;
				result.push_back(ss.str());
				break;

			case REGIMM_SYNCI:
				result.push_back(regimm_names[regimm5]);

				ss << imm << "(" << regnames[rs] << ")";
				result.push_back(ss.str());
				break;

			default:
				{
					ss << "unimplemented instruction: " <<
					    regimm_names[regimm5];
					result.push_back(ss.str());
				}
			}
		}
		break;

	default:
		{
			stringstream ss;
			ss << "unimplemented instruction: " << hi6_names[hi6];
			result.push_back(ss.str());
		}
		break;
	}

	return instrSize;
}


string MIPS_CPUComponent::GetAttribute(const string& attributeName)
{
	if (attributeName == "stable")
		return "yes";

	if (attributeName == "description")
		return "MIPS processor.";

	return Component::GetAttribute(attributeName);
}


/*****************************************************************************/


#ifndef WITHOUTUNITTESTS

#include "ComponentFactory.h"

static void Test_MIPS_CPUComponent_IsStable()
{
	UnitTest::Assert("the MIPS_CPUComponent should be stable",
	    ComponentFactory::HasAttribute("mips_cpu", "stable"));
}

static void Test_MIPS_CPUComponent_Create()
{
	refcount_ptr<Component> cpu =
	    ComponentFactory::CreateComponent("mips_cpu");
	UnitTest::Assert("component was not created?", !cpu.IsNULL());

	const StateVariable * p = cpu->GetVariable("pc");
	UnitTest::Assert("cpu has no pc state variable?", p != NULL);
	UnitTest::Assert("initial pc", p->ToString(), "0xffffffffbfc00000");
}

static void Test_MIPS_CPUComponent_IsCPU()
{
	refcount_ptr<Component> mips_cpu =
	    ComponentFactory::CreateComponent("mips_cpu");
	CPUComponent* cpu = mips_cpu->AsCPUComponent();
	UnitTest::Assert("mips_cpu is not a CPUComponent?", cpu != NULL);
}

static void Test_MIPS_CPUComponent_Disassembly_Basic()
{
	refcount_ptr<Component> mips_cpu =
	    ComponentFactory::CreateComponent("mips_cpu");
	CPUComponent* cpu = mips_cpu->AsCPUComponent();

	vector<string> result;
	size_t len;
	unsigned char instruction[sizeof(uint32_t)];
	// This assumes that the default endianness is BigEndian...
	instruction[0] = 0x27;
	instruction[1] = 0xbd;
	instruction[2] = 0xff;
	instruction[3] = 0xd8;

	len = cpu->DisassembleInstruction(0x12345678, sizeof(uint32_t),
	    instruction, result);

	UnitTest::Assert("disassembled instruction was wrong length?", len, 4);
	UnitTest::Assert("disassembly result incomplete?", result.size(), 3);
	UnitTest::Assert("disassembly result[0]", result[0], "27bdffd8");
	UnitTest::Assert("disassembly result[1]", result[1], "addiu");
	UnitTest::Assert("disassembly result[2]", result[2], "sp,sp,-40");
}

UNITTESTS(MIPS_CPUComponent)
{
	UNITTEST(Test_MIPS_CPUComponent_IsStable);
	UNITTEST(Test_MIPS_CPUComponent_Create);
	UNITTEST(Test_MIPS_CPUComponent_IsCPU);

	// Disassembly:
	UNITTEST(Test_MIPS_CPUComponent_Disassembly_Basic);
}

#endif

