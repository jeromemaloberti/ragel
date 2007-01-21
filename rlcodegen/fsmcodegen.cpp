/*
 *  Copyright 2001-2006 Adrian Thurston <thurston@cs.queensu.ca>
 *            2004 Eric Ocean <eric.ocean@ampede.com>
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

#include "rlcodegen.h"
#include "fsmcodegen.h"
#include "redfsm.h"
#include "gendata.h"
#include <sstream>
#include <string>
#include <assert.h>

using std::ostream;
using std::ostringstream;
using std::string;
using std::cerr;
using std::endl;


/* Determine if a string is only whitespace. Code blocks that are only
 * whitespace need not be output. */
bool onlyWhitespace( char *str )
{
	while ( *str != 0 ) {
		if ( *str != ' ' && *str != '\t' && *str != '\n' &&
				*str != '\v' && *str != '\f' && *str != '\r' )
			return false;
		str += 1;
	}
	return true;
}

/* Init code gen with in parameters. */
FsmCodeGen::FsmCodeGen( )
:
	fsmName(0), 
	cgd(0), 
	redFsm(0), 
	out(*outStream),
	bAnyToStateActions(false),
	bAnyFromStateActions(false),
	bAnyRegActions(false),
	bAnyEofActions(false),
	bAnyActionGotos(false),
	bAnyActionCalls(false),
	bAnyActionRets(false),
	bAnyRegActionRets(false),
	bAnyRegActionByValControl(false),
	bAnyRegNextStmt(false),
	bAnyRegCurStateRef(false),
	bAnyRegBreak(false),
	bAnyLmSwitchError(false),
	bAnyConditions(false)
{
}

/* Does the machine have any actions. */
bool FsmCodeGen::anyActions()
{
	return redFsm->actionMap.length() > 0;
}

void FsmCodeGen::findFinalActionRefs()
{
	for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
		/* Rerence count out of single transitions. */
		for ( RedTransList::Iter rtel = st->outSingle; rtel.lte(); rtel++ ) {
			if ( rtel->value->action != 0 ) {
				rtel->value->action->numTransRefs += 1;
				for ( ActionTable::Iter item = rtel->value->action->key; item.lte(); item++ )
					item->value->numTransRefs += 1;
			}
		}

		/* Reference count out of range transitions. */
		for ( RedTransList::Iter rtel = st->outRange; rtel.lte(); rtel++ ) {
			if ( rtel->value->action != 0 ) {
				rtel->value->action->numTransRefs += 1;
				for ( ActionTable::Iter item = rtel->value->action->key; item.lte(); item++ )
					item->value->numTransRefs += 1;
			}
		}

		/* Reference count default transition. */
		if ( st->defTrans != 0 && st->defTrans->action != 0 ) {
			st->defTrans->action->numTransRefs += 1;
			for ( ActionTable::Iter item = st->defTrans->action->key; item.lte(); item++ )
				item->value->numTransRefs += 1;
		}

		/* Reference count to state actions. */
		if ( st->toStateAction != 0 ) {
			st->toStateAction->numToStateRefs += 1;
			for ( ActionTable::Iter item = st->toStateAction->key; item.lte(); item++ )
				item->value->numToStateRefs += 1;
		}

		/* Reference count from state actions. */
		if ( st->fromStateAction != 0 ) {
			st->fromStateAction->numFromStateRefs += 1;
			for ( ActionTable::Iter item = st->fromStateAction->key; item.lte(); item++ )
				item->value->numFromStateRefs += 1;
		}

		/* Reference count EOF actions. */
		if ( st->eofAction != 0 ) {
			st->eofAction->numEofRefs += 1;
			for ( ActionTable::Iter item = st->eofAction->key; item.lte(); item++ )
				item->value->numEofRefs += 1;
		}
	}
}

/* Assign ids to referenced actions. */
void FsmCodeGen::assignActionIds()
{
	int nextActionId = 0;
	for ( ActionList::Iter act = cgd->actionList; act.lte(); act++ ) {
		/* Only ever interested in referenced actions. */
		if ( act->numRefs() > 0 )
			act->actionId = nextActionId++;
	}
}

void FsmCodeGen::setValueLimits()
{
	maxSingleLen = 0;
	maxRangeLen = 0;
	maxKeyOffset = 0;
	maxIndexOffset = 0;
	maxActListId = 0;
	maxActionLoc = 0;
	maxActArrItem = 0;
	maxSpan = 0;
	maxCondSpan = 0;
	maxFlatIndexOffset = 0;
	maxCondOffset = 0;
	maxCondLen = 0;
	maxCondSpaceId = 0;
	maxCondIndexOffset = 0;

	/* In both of these cases the 0 index is reserved for no value, so the max
	 * is one more than it would be if they started at 0. */
	maxIndex = redFsm->transSet.length();
	maxCond = cgd->condSpaceList.length(); 

	/* The nextStateId - 1 is the last state id assigned. */
	maxState = redFsm->nextStateId - 1;

	for ( CondSpaceList::Iter csi = cgd->condSpaceList; csi.lte(); csi++ ) {
		if ( csi->condSpaceId > maxCondSpaceId )
			maxCondSpaceId = csi->condSpaceId;
	}

	for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
		/* Maximum cond length. */
		if ( st->stateCondList.length() > maxCondLen )
			maxCondLen = st->stateCondList.length();

		/* Maximum single length. */
		if ( st->outSingle.length() > maxSingleLen )
			maxSingleLen = st->outSingle.length();

		/* Maximum range length. */
		if ( st->outRange.length() > maxRangeLen )
			maxRangeLen = st->outRange.length();

		/* The key offset index offset for the state after last is not used, skip it.. */
		if ( ! st.last() ) {
			maxCondOffset += st->stateCondList.length();
			maxKeyOffset += st->outSingle.length() + st->outRange.length()*2;
			maxIndexOffset += st->outSingle.length() + st->outRange.length() + 1;
		}

		/* Max cond span. */
		if ( st->condList != 0 ) {
			unsigned long long span = keyOps->span( st->condLowKey, st->condHighKey );
			if ( span > maxCondSpan )
				maxCondSpan = span;
		}

		/* Max key span. */
		if ( st->transList != 0 ) {
			unsigned long long span = keyOps->span( st->lowKey, st->highKey );
			if ( span > maxSpan )
				maxSpan = span;
		}

		/* Max cond index offset. */
		if ( ! st.last() ) {
			if ( st->condList != 0 )
				maxCondIndexOffset += keyOps->span( st->condLowKey, st->condHighKey );
		}

		/* Max flat index offset. */
		if ( ! st.last() ) {
			if ( st->transList != 0 )
				maxFlatIndexOffset += keyOps->span( st->lowKey, st->highKey );
			maxFlatIndexOffset += 1;
		}
	}

	for ( ActionTableMap::Iter at = redFsm->actionMap; at.lte(); at++ ) {
		/* Maximum id of action lists. */
		if ( at->actListId+1 > maxActListId )
			maxActListId = at->actListId+1;

		/* Maximum location of items in action array. */
		if ( at->location+1 > maxActionLoc )
			maxActionLoc = at->location+1;

		/* Maximum values going into the action array. */
		if ( at->key.length() > maxActArrItem )
			maxActArrItem = at->key.length();
		for ( ActionTable::Iter item = at->key; item.lte(); item++ ) {
			if ( item->value->actionId > maxActArrItem )
				maxActArrItem = item->value->actionId;
		}
	}
}

void FsmCodeGen::analyzeAction( Action *act, InlineList *inlineList )
{
	for ( InlineList::Iter item = *inlineList; item.lte(); item++ ) {
		/* Only consider actions that are referenced. */
		if ( act->numRefs() > 0 ) {
			if ( item->type == InlineItem::Goto || item->type == InlineItem::GotoExpr )
				bAnyActionGotos = true;
			else if ( item->type == InlineItem::Call || item->type == InlineItem::CallExpr )
				bAnyActionCalls = true;
			else if ( item->type == InlineItem::Ret )
				bAnyActionRets = true;
		}

		/* Check for various things in regular actions. */
		if ( act->numTransRefs > 0 || act->numToStateRefs > 0 || act->numFromStateRefs > 0 ) {
			/* Any returns in regular actions? */
			if ( item->type == InlineItem::Ret )
				bAnyRegActionRets = true;

			/* Any next statements in the regular actions? */
			if ( item->type == InlineItem::Next || item->type == InlineItem::NextExpr )
				bAnyRegNextStmt = true;

			/* Any by value control in regular actions? */
			if ( item->type == InlineItem::CallExpr || item->type == InlineItem::GotoExpr )
				bAnyRegActionByValControl = true;

			/* Any references to the current state in regular actions? */
			if ( item->type == InlineItem::Curs )
				bAnyRegCurStateRef = true;

			if ( item->type == InlineItem::Break )
				bAnyRegBreak = true;

			if ( item->type == InlineItem::LmSwitch && item->handlesError )
				bAnyLmSwitchError = true;
		}

		if ( item->children != 0 )
			analyzeAction( act, item->children );
	}
}

void FsmCodeGen::analyzeActionList( RedAction *redAct, InlineList *inlineList )
{
	for ( InlineList::Iter item = *inlineList; item.lte(); item++ ) {
		/* Any next statements in the action table? */
		if ( item->type == InlineItem::Next || item->type == InlineItem::NextExpr )
			redAct->bAnyNextStmt = true;

		/* Any references to the current state. */
		if ( item->type == InlineItem::Curs )
			redAct->bAnyCurStateRef = true;

		if ( item->type == InlineItem::Break )
			redAct->bAnyBreakStmt = true;

		if ( item->children != 0 )
			analyzeActionList( redAct, item->children );
	}
}

/* Gather various info on the machine. */
void FsmCodeGen::analyzeMachine()
{
	/* Find the true count of action references.  */
	findFinalActionRefs();

	/* Check if there are any calls in action code. */
	for ( ActionList::Iter act = cgd->actionList; act.lte(); act++ ) {
		/* Record the occurrence of various kinds of actions. */
		if ( act->numToStateRefs > 0 )
			bAnyToStateActions = true;
		if ( act->numFromStateRefs > 0 )
			bAnyFromStateActions = true;
		if ( act->numEofRefs > 0 )
			bAnyEofActions = true;
		if ( act->numTransRefs > 0 )
			bAnyRegActions = true;

		/* Recurse through the action's parse tree looking for various things. */
		analyzeAction( act, act->inlineList );
	}

	/* Analyze reduced action lists. */
	for ( ActionTableMap::Iter redAct = redFsm->actionMap; redAct.lte(); redAct++ ) {
		for ( ActionTable::Iter act = redAct->key; act.lte(); act++ )
			analyzeActionList( redAct, act->value->inlineList );
	}

	/* Find states that have transitions with actions that have next
	 * statements. */
	for ( RedStateList::Iter st = redFsm->stateList; st.lte(); st++ ) {
		/* Check any actions out of outSinge. */
		for ( RedTransList::Iter rtel = st->outSingle; rtel.lte(); rtel++ ) {
			if ( rtel->value->action != 0 && rtel->value->action->anyCurStateRef() )
				st->bAnyRegCurStateRef = true;
		}

		/* Check any actions out of outRange. */
		for ( RedTransList::Iter rtel = st->outRange; rtel.lte(); rtel++ ) {
			if ( rtel->value->action != 0 && rtel->value->action->anyCurStateRef() )
				st->bAnyRegCurStateRef = true;
		}

		/* Check any action out of default. */
		if ( st->defTrans != 0 && st->defTrans->action != 0 && 
				st->defTrans->action->anyCurStateRef() )
			st->bAnyRegCurStateRef = true;
		
		if ( st->stateCondList.length() > 0 )
			bAnyConditions = true;
	}

	/* Assign ids to actions that are referenced. */
	assignActionIds();

	/* Set the maximums of various values used for deciding types. */
	setValueLimits();

	/* Determine if we should use indicies. */
	calcIndexSize();
}

unsigned int FsmCodeGen::arrayTypeSize( unsigned long maxVal )
{
	long long maxValLL = (long long) maxVal;
	HostType *arrayType = keyOps->typeSubsumes( maxValLL );
	assert( arrayType != 0 );
	return arrayType->size;
}

string FsmCodeGen::ARRAY_TYPE( unsigned long maxVal )
{
	long long maxValLL = (long long) maxVal;
	HostType *arrayType = keyOps->typeSubsumes( maxValLL );
	assert( arrayType != 0 );

	string ret = arrayType->data1;
	if ( arrayType->data2 != 0 ) {
		ret += " ";
		ret += arrayType->data2;
	}
	return ret;
}


/* Write out the fsm name. */
string FsmCodeGen::FSM_NAME()
{
	return fsmName;
}

/* Emit the offset of the start state as a decimal integer. */
string FsmCodeGen::START_STATE_ID()
{
	ostringstream ret;
	ret << redFsm->startState->id;
	return ret.str();
};

/* Write out the array of actions. */
std::ostream &FsmCodeGen::ACTIONS_ARRAY()
{
	out << "\t0, ";
	int totalActions = 1;
	for ( ActionTableMap::Iter act = redFsm->actionMap; act.lte(); act++ ) {
		/* Write out the length, which will never be the last character. */
		out << act->key.length() << ", ";
		/* Put in a line break every 8 */
		if ( totalActions++ % 8 == 7 )
			out << "\n\t";

		for ( ActionTable::Iter item = act->key; item.lte(); item++ ) {
			out << item->value->actionId;
			if ( ! (act.last() && item.last()) )
				out << ", ";

			/* Put in a line break every 8 */
			if ( totalActions++ % 8 == 7 )
				out << "\n\t";
		}
	}
	out << "\n";
	return out;
}


string FsmCodeGen::CS()
{
	ostringstream ret;
	if ( cgd->curStateExpr != 0 ) { 
		/* Emit the user supplied method of retrieving the key. */
		ret << "(";
		INLINE_LIST( ret, cgd->curStateExpr, 0, false );
		ret << ")";
	}
	else {
		/* Expression for retrieving the key, use simple dereference. */
		ret << ACCESS() << "cs";
	}
	return ret.str();
}

string FsmCodeGen::ACCESS()
{
	ostringstream ret;
	if ( cgd->accessExpr != 0 )
		INLINE_LIST( ret, cgd->accessExpr, 0, false );
	return ret.str();
}

string FsmCodeGen::GET_WIDE_KEY()
{
	if ( anyConditions() ) 
		return "_widec";
	else
		return GET_KEY();
}

string FsmCodeGen::GET_WIDE_KEY( RedStateAp *state )
{
	if ( state->stateCondList.length() > 0 )
		return "_widec";
	else
		return GET_KEY();
}

string FsmCodeGen::GET_KEY()
{
	ostringstream ret;
	if ( cgd->getKeyExpr != 0 ) { 
		/* Emit the user supplied method of retrieving the key. */
		ret << "(";
		INLINE_LIST( ret, cgd->getKeyExpr, 0, false );
		ret << ")";
	}
	else {
		/* Expression for retrieving the key, use simple dereference. */
		ret << "(*" << P() << ")";
	}
	return ret.str();
}

/* Write out level number of tabs. Makes the nested binary search nice
 * looking. */
string FsmCodeGen::TABS( int level )
{
	string result;
	while ( level-- > 0 )
		result += "\t";
	return result;
}

/* Write out a key from the fsm code gen. Depends on wether or not the key is
 * signed. */
string FsmCodeGen::KEY( Key key )
{
	ostringstream ret;
	if ( keyOps->isSigned || !hostLang->explicitUnsigned )
		ret << key.getVal();
	else
		ret << (unsigned long) key.getVal() << 'u';
	return ret.str();
}

void FsmCodeGen::EXEC( ostream &ret, InlineItem *item, int targState, int inFinish )
{
	/* The parser gives fexec two children. The double brackets are for D
	 * code. If the inline list is a single word it will get interpreted as a
	 * C-style cast by the D compiler. */
	ret << "{" << P() << " = ((";
	INLINE_LIST( ret, item->children, targState, inFinish );
	ret << "))-1;}";
}

void FsmCodeGen::EXECTE( ostream &ret, InlineItem *item, int targState, int inFinish )
{
	/* Tokend version of exec. */

	/* The parser gives fexec two children. The double brackets are for D
	 * code. If the inline list is a single word it will get interpreted as a
	 * C-style cast by the D compiler. */
	ret << "{" << TOKEND() << " = ((";
	INLINE_LIST( ret, item->children, targState, inFinish );
	ret << "));}";
}


void FsmCodeGen::LM_SWITCH( ostream &ret, InlineItem *item, 
		int targState, int inFinish )
{
	ret << 
		"	switch( act ) {\n";

	/* If the switch handles error then we also forced the error state. It
	 * will exist. */
	if ( item->handlesError ) {
		ret << "	case 0: " << TOKEND() << " = " << TOKSTART() << "; ";
		GOTO( ret, redFsm->errState->id, inFinish );
		ret << "\n";
	}

	for ( InlineList::Iter lma = *item->children; lma.lte(); lma++ ) {
		/* Write the case label, the action and the case break. */
		ret << "	case " << lma->lmId << ":\n";

		/* Write the block and close it off. */
		ret << "	{";
		INLINE_LIST( ret, lma->children, targState, inFinish );
		ret << "}\n";

		ret << "	break;\n";
	}
	/* Default required for D code. */
	ret << 
		"	default: break;\n"
		"	}\n"
		"\t";
}

void FsmCodeGen::SET_ACT( ostream &ret, InlineItem *item )
{
	ret << ACT() << " = " << item->lmId << ";";
}

void FsmCodeGen::SET_TOKEND( ostream &ret, InlineItem *item )
{
	/* The tokend action sets tokend. */
	ret << TOKEND() << " = " << P();
	if ( item->offset != 0 ) 
		out << "+" << item->offset;
	out << ";";
}

void FsmCodeGen::GET_TOKEND( ostream &ret, InlineItem *item )
{
	ret << TOKEND();
}

void FsmCodeGen::INIT_TOKSTART( ostream &ret, InlineItem *item )
{
	ret << TOKSTART() << " = " << NULL_ITEM() << ";";
}

void FsmCodeGen::INIT_ACT( ostream &ret, InlineItem *item )
{
	ret << ACT() << " = 0;";
}

void FsmCodeGen::SET_TOKSTART( ostream &ret, InlineItem *item )
{
	ret << TOKSTART() << " = " << P() << ";";
}

void FsmCodeGen::SUB_ACTION( ostream &ret, InlineItem *item, 
		int targState, bool inFinish )
{
	if ( item->children->length() > 0 ) {
		/* Write the block and close it off. */
		ret << "{";
		INLINE_LIST( ret, item->children, targState, inFinish );
		ret << "}";
	}
}


/* Write out an inline tree structure. Walks the list and possibly calls out
 * to virtual functions than handle language specific items in the tree. */
void FsmCodeGen::INLINE_LIST( ostream &ret, InlineList *inlineList, 
		int targState, bool inFinish )
{
	for ( InlineList::Iter item = *inlineList; item.lte(); item++ ) {
		switch ( item->type ) {
		case InlineItem::Text:
			ret << item->data;
			break;
		case InlineItem::Goto:
			GOTO( ret, item->targState->id, inFinish );
			break;
		case InlineItem::Call:
			CALL( ret, item->targState->id, targState, inFinish );
			break;
		case InlineItem::Next:
			NEXT( ret, item->targState->id, inFinish );
			break;
		case InlineItem::Ret:
			RET( ret, inFinish );
			break;
		case InlineItem::PChar:
			ret << P();
			break;
		case InlineItem::Char:
			ret << GET_KEY();
			break;
		case InlineItem::Hold:
			ret << P() << "--;";
			break;
		case InlineItem::Exec:
			EXEC( ret, item, targState, inFinish );
			break;
		case InlineItem::HoldTE:
			ret << TOKEND() << "--;";
			break;
		case InlineItem::ExecTE:
			EXECTE( ret, item, targState, inFinish );
			break;
		case InlineItem::Curs:
			CURS( ret, inFinish );
			break;
		case InlineItem::Targs:
			TARGS( ret, inFinish, targState );
			break;
		case InlineItem::Entry:
			ret << item->targState->id;
			break;
		case InlineItem::GotoExpr:
			GOTO_EXPR( ret, item, inFinish );
			break;
		case InlineItem::CallExpr:
			CALL_EXPR( ret, item, targState, inFinish );
			break;
		case InlineItem::NextExpr:
			NEXT_EXPR( ret, item, inFinish );
			break;
		case InlineItem::LmSwitch:
			LM_SWITCH( ret, item, targState, inFinish );
			break;
		case InlineItem::LmSetActId:
			SET_ACT( ret, item );
			break;
		case InlineItem::LmSetTokEnd:
			SET_TOKEND( ret, item );
			break;
		case InlineItem::LmGetTokEnd:
			GET_TOKEND( ret, item );
			break;
		case InlineItem::LmInitTokStart:
			INIT_TOKSTART( ret, item );
			break;
		case InlineItem::LmInitAct:
			INIT_ACT( ret, item );
			break;
		case InlineItem::LmSetTokStart:
			SET_TOKSTART( ret, item );
			break;
		case InlineItem::SubAction:
			SUB_ACTION( ret, item, targState, inFinish );
			break;
		case InlineItem::Break:
			BREAK( ret, targState );
			break;
		}
	}
}
/* Write out paths in line directives. Escapes any special characters. */
string FsmCodeGen::LDIR_PATH( char *path )
{
	ostringstream ret;
	for ( char *pc = path; *pc != 0; pc++ ) {
		if ( *pc == '\\' )
			ret << "\\\\";
		else
			ret << *pc;
	}
	return ret.str();
}

void FsmCodeGen::ACTION( ostream &ret, Action *action, int targState, bool inFinish )
{
	/* Write the preprocessor line info for going into the source file. */
	lineDirective( ret, cgd->fileName, action->loc.line );

	/* Write the block and close it off. */
	ret << "\t{";
	INLINE_LIST( ret, action->inlineList, targState, inFinish );
	ret << "}\n";
}

void FsmCodeGen::CONDITION( ostream &ret, Action *condition )
{
	ret << "\n";
	lineDirective( ret, cgd->fileName, condition->loc.line );
	INLINE_LIST( ret, condition->inlineList, 0, false );
}

string FsmCodeGen::ERROR_STATE()
{
	ostringstream ret;
	if ( redFsm->errState != 0 )
		ret << redFsm->errState->id;
	else
		ret << "-1";
	return ret.str();
}

string FsmCodeGen::FIRST_FINAL_STATE()
{
	ostringstream ret;
	if ( redFsm->firstFinState != 0 )
		ret << redFsm->firstFinState->id;
	else
		ret << redFsm->nextStateId;
	return ret.str();
}

void FsmCodeGen::writeOutInit()
{
	out << "	{\n";
	out << "\t" << CS() << " = " << START() << ";\n";
	
	/* If there are any calls, then the stack top needs initialization. */
	if ( anyActionCalls() || anyActionRets() )
		out << "\t" << TOP() << " = 0;\n";

	if ( cgd->hasLongestMatch ) {
		out << 
			"	" << TOKSTART() << " = " << NULL_ITEM() << ";\n"
			"	" << TOKEND() << " = " << NULL_ITEM() << ";\n"
			"	" << ACT() << " = 0;\n";
	}
	out << "	}\n";
}

string FsmCodeGen::DATA_PREFIX()
{
	if ( cgd->dataPrefix )
		return FSM_NAME() + "_";
	return "";
}

/* Emit the alphabet data type. */
string FsmCodeGen::ALPH_TYPE()
{
	string ret = keyOps->alphType->data1;
	if ( keyOps->alphType->data2 != 0 ) {
		ret += " ";
		ret += + keyOps->alphType->data2;
	}
	return ret;
}

/* Emit the alphabet data type. */
string FsmCodeGen::WIDE_ALPH_TYPE()
{
	string ret;
	if ( maxKey <= keyOps->maxKey )
		ret = ALPH_TYPE();
	else {
		long long maxKeyVal = maxKey.getLongLong();
		HostType *wideType = keyOps->typeSubsumes( keyOps->isSigned, maxKeyVal );
		assert( wideType != 0 );

		ret = wideType->data1;
		if ( wideType->data2 != 0 ) {
			ret += " ";
			ret += wideType->data2;
		}
	}
	return ret;
}


/*
 * Language specific, but style independent code generators functions.
 */

string CCodeGen::PTR_CONST()
{
	return "const ";
}

std::ostream &CCodeGen::OPEN_ARRAY( string type, string name )
{
	out << "static const " << type << " " << name << "[] = {\n";
	return out;
}

std::ostream &CCodeGen::CLOSE_ARRAY()
{
	return out << "};\n";
}

std::ostream &CCodeGen::STATIC_VAR( string type, string name )
{
	out << "static const " << type << " " << name;
	return out;
}

string CCodeGen::UINT( )
{
	return "unsigned int";
}

string CCodeGen::ARR_OFF( string ptr, string offset )
{
	return ptr + " + " + offset;
}

string CCodeGen::CAST( string type )
{
	return "(" + type + ")";
}

string CCodeGen::NULL_ITEM()
{
	return "0";
}

string CCodeGen::POINTER()
{
	return " *";
}

std::ostream &CCodeGen::SWITCH_DEFAULT()
{
	return out;
}

string CCodeGen::CTRL_FLOW()
{
	return "";
}

/*
 * D Specific
 */

string DCodeGen::NULL_ITEM()
{
	return "null";
}

string DCodeGen::POINTER()
{
	// multiple items seperated by commas can also be pointer types.
	return "* ";
}

string DCodeGen::PTR_CONST()
{
	return "";
}

std::ostream &DCodeGen::OPEN_ARRAY( string type, string name )
{
	out << "static const " << type << "[] " << name << " = [\n";
	return out;
}

std::ostream &DCodeGen::CLOSE_ARRAY()
{
	return out << "];\n";
}

std::ostream &DCodeGen::STATIC_VAR( string type, string name )
{
	out << "static const " << type << " " << name;
	return out;
}

string DCodeGen::ARR_OFF( string ptr, string offset )
{
	return "&" + ptr + "[" + offset + "]";
}

string DCodeGen::CAST( string type )
{
	return "cast(" + type + ")";
}

string DCodeGen::UINT( )
{
	return "uint";
}

std::ostream &DCodeGen::SWITCH_DEFAULT()
{
	out << "		default: break;\n";
	return out;
}

string DCodeGen::CTRL_FLOW()
{
	return "if (true) ";
}


/* 
 * Java Specific
 */

string JavaCodeGen::PTR_CONST()
{
	/* Not used in Java code. */
	assert( false );
	return "final";
}

std::ostream &JavaCodeGen::OPEN_ARRAY( string type, string name )
{
	out << "static final " << type << "[] " << name << " = {\n";
	return out;
}

std::ostream &JavaCodeGen::CLOSE_ARRAY()
{
	return out << "};\n";
}

std::ostream &JavaCodeGen::STATIC_VAR( string type, string name )
{
	out << "static final " << type << " " << name;
	return out;
}

string JavaCodeGen::UINT( )
{
	/* Not used. */
	assert( false );
	return "long";
}

string JavaCodeGen::ARR_OFF( string ptr, string offset )
{
	return ptr + " + " + offset;
}

string JavaCodeGen::CAST( string type )
{
	return "(" + type + ")";
}

string JavaCodeGen::NULL_ITEM()
{
	/* In java we use integers instead of pointers. */
	return "-1";
}

string JavaCodeGen::POINTER()
{
	/* Not used. */
	assert( false );
	return " *";
}

std::ostream &JavaCodeGen::SWITCH_DEFAULT()
{
	return out;
}

string JavaCodeGen::GET_KEY()
{
	ostringstream ret;
	if ( cgd->getKeyExpr != 0 ) { 
		/* Emit the user supplied method of retrieving the key. */
		ret << "(";
		INLINE_LIST( ret, cgd->getKeyExpr, 0, false );
		ret << ")";
	}
	else {
		/* Expression for retrieving the key, use simple dereference. */
		ret << "data[" << P() << "]";
	}
	return ret.str();
}

string JavaCodeGen::CTRL_FLOW()
{
	return "if (true) ";
}

