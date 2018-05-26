/***************************************************************************
 * Copyright (c)  2018  Zephyr Software LLC. All rights reserved.
 *
 * This software is furnished under a license and/or other restrictive
 * terms and may be used and copied only in accordance with such terms
 * and the inclusion of the above copyright notice. This software or
 * any other copies thereof may not be provided or otherwise made
 * available to any other person without the express written consent
 * of an authorized representative of Zephyr Software LCC. Title to,
 * ownership of, and all rights in the software is retained by
 * Zephyr Software LCC.
 *
 * Zephyr Software LLC. Proprietary Information
 *
 * Unless otherwise specified, the information contained in this
 * directory, following this legend, and/or referenced herein is
 * Zephyr Software LLC. (Zephyr) Proprietary Information.
 *
 * CONTACT INFO
 *
 * E-mail: jwd@zephyr-software.com
 **************************************************************************/

#include "constant_unroll.hpp"

#include <Rewrite_Utility.hpp>
#include <utils.hpp>

using namespace std;
using namespace libTransform;
using namespace libIRDB;
using namespace ConstantUnroll;
using namespace IRDBUtility;

ConstantUnroll_t::ConstantUnroll_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, bool p_verbose)
	:
	Transform(NULL, p_variantIR, NULL),
	m_dbinterface(p_dbinterface),
	m_verbose(p_verbose)
{
}


/*
 * Execute the transform.
 *
 * preconditions: the FileIR is read as from the IRDB. valid file listing functions to auto-initialize
 * postcondition: instructions added to auto-initialize stack for each specified function
 *
 */
int ConstantUnroll_t::execute()
{
	return 1;	 // true means success
}
