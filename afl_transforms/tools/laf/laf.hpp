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
// the actual transform.
class Laf_t : public IRDB_SDK::Transform
{
public:
	// explicitly disable default and copy constructors
	Laf_t() = delete;
	Laf_t(const Laf::Laf_t&) = delete;
	Laf_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, bool p_verbose=false);
	int execute();
	void setSplitCompare(bool);
	void setSplitBranch(bool);
	bool getSplitCompare() const;
	bool getSplitBranch() const;

private:
	RegisterSet_t getDeadRegs(IRDB_SDK::Instruction_t* insn) const;
	RegisterSet_t getFreeRegs(const RegisterSet_t& candidates, const RegisterSet_t& allowed) const;
	void doSplitCompare(IRDB_SDK::Instruction_t*);
	int doSplitCompare();
	bool isBlacklisted(IRDB_SDK::Function_t*) const;

private:
	IRDB_SDK::pqxxDB_t &m_dbinterface;
	std::unique_ptr<IRDB_SDK::FunctionSet_t>      leaf_functions;
	std::unique_ptr<IRDB_SDK::DeadRegisterMap_t>  dead_registers;
	bool m_verbose;
	bool m_split_compare;
	bool m_split_branch;
	std::set<std::string> m_blacklist;
};

} 

#endif
