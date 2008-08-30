/*
 *  Copyright 2001-2006 Adrian Thurston <thurston@cs.queensu.ca>
 *            2004 Erich Ocean <eric.ocean@ampede.com>
 *            2005 Alan West <alan@alanz.com>
 */

/*  This file is part of Ragel.
 *
 *  Ragel is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  Ragel is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with Ragel; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#ifndef _FTABCODEGEN_H
#define _FTABCODEGEN_H

#include <iostream>
#include "tabcodegen.h"

/* Forwards. */
struct CodeGenData;


/*
 * CSharpFTabCodeGen
 */
class CSharpFTabCodeGen : public CSharpTabCodeGen
{
public:
	CSharpFTabCodeGen( ostream &out ) : CSharpFsmCodeGen(out), CSharpTabCodeGen(out) {}
private:
	std::ostream &TO_STATE_ACTION_SWITCH();
	std::ostream &FROM_STATE_ACTION_SWITCH();
	std::ostream &EOF_ACTION_SWITCH();
	std::ostream &ACTION_SWITCH();

	virtual std::ostream &TO_STATE_ACTION( RedStateAp *state );
	virtual std::ostream &FROM_STATE_ACTION( RedStateAp *state );
	virtual std::ostream &EOF_ACTION( RedStateAp *state );
	virtual std::ostream &TRANS_ACTION( RedTransAp *trans );
	virtual void writeData();
	virtual void writeExec();
	virtual void calcIndexSize();
};

#endif /* _FTABCODEGEN_H */
