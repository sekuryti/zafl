#ifndef _ZAFL_LAF_H
#define _ZAFL_LAF_H

#include <memory>

#include <irdb-core>
#include <irdb-transform>
#include <irdb-util>
#include <irdb-deep>
#include <libMEDSAnnotation.h>

namespace Laf
{
	using RegisterSet_t = IRDB_SDK::RegisterIDSet_t;

	class Laf_t : public IRDB_SDK::Transform
	{
		public:
			// explicitly disable default and copy constructors
			Laf_t() = delete;
			Laf_t(const Laf::Laf_t&) = delete;
			Laf_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, bool p_verbose=false);
			int execute();
			void setSplitCompare(bool);
			bool getSplitCompare() const;
			void setTraceDiv(bool);
			bool getTraceDiv() const;

		private:
			RegisterSet_t getDeadRegs(IRDB_SDK::Instruction_t* insn) const;
			RegisterSet_t getFreeRegs(const RegisterSet_t& candidates, const RegisterSet_t& allowed) const;
			int doSplitCompare();
			int doTraceDiv();
			bool isBlacklisted(IRDB_SDK::Function_t*) const;
			bool hasLeafAnnotation(IRDB_SDK::Function_t* fn) const;
			bool instrumentCompare(IRDB_SDK::Instruction_t* p_instr, bool p_honor_red_zone);
			bool instrumentDiv(IRDB_SDK::Instruction_t* p_instr, bool p_honor_red_zone);
			bool getFreeRegister(IRDB_SDK::Instruction_t* p_instr, std::string& p_freereg, RegisterSet_t);
			IRDB_SDK::Instruction_t* traceDword(IRDB_SDK::Instruction_t* p_instr, const size_t p_num_bytes, const std::vector<std::string> p_init_sequence, const uint32_t p_immediate, const std::string p_freereg);
			IRDB_SDK::Instruction_t* addInitSequence(IRDB_SDK::Instruction_t* p_instr, const std::vector<std::string> sequence);
			bool memoryStackAccess(IRDB_SDK::Instruction_t* p_instr, unsigned p_operandNumber=0);
			std::vector<std::string> getInitSequence(IRDB_SDK::Instruction_t *p_instr, std::string p_free_reg);

		private:
			IRDB_SDK::pqxxDB_t &m_dbinterface;
			std::unique_ptr<IRDB_SDK::FunctionSet_t>      leaf_functions;
			std::unique_ptr<IRDB_SDK::DeadRegisterMap_t>  dead_registers;
			bool m_verbose;
			bool m_split_compare;
			bool m_trace_div;
			std::set<std::string> m_blacklist;
			size_t m_num_cmp;
			size_t m_num_cmp_instrumented;
			size_t m_num_div;
			size_t m_num_div_instrumented;
			size_t m_skip_easy_val;
			size_t m_skip_byte;
			size_t m_skip_word;
			size_t m_skip_qword;
			size_t m_skip_relocs;
			size_t m_skip_stack_access;
			size_t m_skip_no_free_regs;
			size_t m_skip_unknown;
};

} 

#endif
