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
	using namespace IRDB_SDK;
	using RegisterSet_t = IRDB_SDK::RegisterIDSet_t;

	class Laf_t : public Transform_t
	{
		public:
			// explicitly disable default and copy constructors
			Laf_t() = delete;
			Laf_t(const Laf::Laf_t&) = delete;
			Laf_t(pqxxDB_t &p_dbinterface, FileIR_t *p_variantIR, bool p_verbose=false);
			int execute();
			void setTraceCompare(bool);
			bool getTraceCompare() const;
			void setTraceDiv(bool);
			bool getTraceDiv() const;

		private:
			RegisterSet_t getDeadRegs(Instruction_t* insn) const;
			RegisterSet_t getFreeRegs(const RegisterSet_t& candidates, const RegisterSet_t& allowed) const;
			int doTraceCompare();
			int doTraceDiv();
			bool isBlacklisted(Function_t*) const;
			bool getFreeRegister(Instruction_t* p_instr, std::string& p_freereg, RegisterSet_t);
			bool traceBytesNested(Instruction_t *p_instr, int64_t p_immediate);

		private:
			pqxxDB_t &m_dbinterface;
			std::unique_ptr<DeadRegisterMap_t>  dead_registers;
			bool m_verbose;
			bool m_trace_compare;
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
