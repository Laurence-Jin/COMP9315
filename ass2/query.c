// query.c ... query scan functions
// part of Multi-attribute Linear-hashed Files
// Manage creating and using Query objects
// Last modified by John Shepherd, July 2019

#include "defs.h"
#include "query.h"
#include "reln.h"
#include "tuple.h"
#include "hash.h"


// A suggestion ... you can change however you like

struct QueryRep {
	Reln    rel;       // need to remember Relation info
	Bits    known;     // the hash value from MAH
	Bits    unknown;   // the unknown bits from MAH
	PageID  curpage;   // current page in scan data page
	int     is_ovflow; // are we in the overflow pages?
	Offset  curtup;    // offset of current tuple within page eg 0x1000 address
	//TODO
	Bits start;
	int depth;
	int num_unknown;

	Count unknown_bit; //the position of bit in unknown
	Count curScanPage; //current data page or overflow page
	Bits unbits;    // current unknow bits change level
	char * queryString;
};

// take a query string (e.g. "1234,?,abc,?")
// set up a QueryRep object for the scan

Bits GetknownBit(Bits res, int i, ChVecItem* cv, Count nvals, Bits* hashBits, int flag) {
	int a, b;
	for (int j = 0; j < MAXBITS; j++) {
		a = cv[j].att;
		b = cv[j].bit; //the position of bit
		if (flag == 0 && a == i) {
			res = setBit(res,j);
		}
		else if (flag == 1 && a == i && bitIsSet(hashBits[a],b)) {
			res = setBit(res,j);
		}	
	}
	return res;
}

Query startQuery(Reln r, char *q)
{
	// TODO
	// Partial algorithm:
	// form known bits from known attributes
	// form unknown bits from '?' attributes
	// compute PageID of first page
	//   using known bits and first "unknown" value
	// set all values in QueryRep object	
	
	Query new = malloc(sizeof(struct QueryRep));
	assert(new != NULL);
	new->depth = depth(r);
	Count nvals = nattrs(r);
	char* c = q;
	int j;
	char **attris = malloc(nvals * sizeof(char *));
	for (int i = 0; i < nvals; i++, c++) {
		j = 0;
		attris[i] = malloc(sizeof(char)*50);
		for (; *c != '\0' && *c != ','; c++, j++) {
			attris[i][j] = *c;
		}
		attris[i][j] = '\0';
		attris[i] = realloc(attris[i], sizeof(char)*(j+1));
	}

	ChVecItem* cv = chvec(r);
	Bits hashBits[nvals];
	
	for (int i = 0; i < nvals; i++) {
		hashBits[i] = hash_any((unsigned char *)attris[i], strlen(attris[i]));
	}
	//printf("depth: %d\n", depth(r));
	Bits known = 0, unknown = 0;
	new->num_unknown = 0;

	for (int i = 0; i < nvals; i++) {
		if (*attris[i] == '?') {
			unknown = GetknownBit(unknown, i, cv, nvals, hashBits, 0);
			new->num_unknown++;
		}
		else {
			known = GetknownBit(known, i, cv, nvals, hashBits, 1);
		}
	}

	char buffer[MAXBITS];
	bitsString(known,buffer);
	printf("KNOWN:  %s\n",buffer);

	char temp[MAXBITS];
	bitsString(unknown,temp);
	printf("UNKNOWN:%s\n",temp);

	Bits comp = 0;

	for (int i = 0; i < depth(r); i++) 
		comp = setBit(comp, i); 

	PageID start = comp & known;
	if (start < splitp(r)) {
		new->depth++;
		start = setBit(comp, depth(r)) & known;
	}

	int counts = 0, move = 1;
	for (int i = 0; i < MAXBITS - 1; i++) {
		if (i >= new->depth) break; // out of range
		if (move & unknown) {
			counts++;
		}
		move = move << 1;	
	}
	
	new->start = start;
	new->num_unknown = counts;
	new->known = known;
	new->unknown = unknown;
	new->rel = r;
	new->curpage = 0;
	new->curtup = 0;
	new->is_ovflow = 0;
	new->unbits = 0;
	new->curScanPage = 0;
	new->queryString = q;
	
	return new;
}

Tuple FindNextMatchTuple(FILE* f, Query q, Count Ntuple) {
	Tuple next;
	while (q->curScanPage++ < Ntuple) {
		char tuple[MAXTUPLEN];

		fseek(f, PAGESIZE * q->curpage + 2 * sizeof(Offset) + sizeof(Count) + q->curtup, SEEK_SET);
		fgets(tuple, MAXTUPLEN - 1, f);
		next = copyString(tuple);
		q->curtup += strlen(next) + 1;

		if (tupleMatch(q->rel, q->queryString, next)) {
			return next;
		}
		else 
			free(next);
	}
	return NULL;
}

Tuple getNextTuple(Query q)
{
	// TODO
	// Partial algorithm:
	// if (more tuples in current page)
	//    get next matching tuple from current page
	// else if (current page has overflow)
	//    move to overflow page
	//    grab first matching tuple from page
	// else
	//    move to "next" bucket
	//    grab first matching tuple from data page
	// endif
	// if (current page has no matching tuples)
	//    go to next page (try again)
	// endif
	Reln r = q->rel;
	for (;;) {

		FILE* f = (q->is_ovflow != 1 ? dataFile(r) : ovflowFile(r));

		// get current page and the corresponding tuple number
		Page current = getPage(f, q->curpage);
		Count Ntuple = pageNTuples(current);
		int overflow = pageOvflow(current);
		
		// get next matching tuple from the current page and return
		Tuple next = FindNextMatchTuple(f, q, Ntuple);
		if (next != NULL) return next; 

		// if not overflow, update attribute
		if (overflow != -1) {
			q->is_ovflow = 1;
			q->curtup = 0;
			q->curScanPage = 0;
			q->curpage = pageOvflow(current);
		}
		else {
			// overflow
			// mask for set & compare	
			Bits uncount = q->unbits;
			int unnums = q->num_unknown;
			int new_mask = 1 << unnums;
			if (new_mask <= uncount++) 
				break;

			Bits set_mask = 0;
			Bits unknown = q->unknown;	
			int Page_num = npages(r);

			// bit set position
			for (int pos = 0, i = 0; i < unnums; i++, pos++) {
				for (; !(setBit(0, pos) & unknown); pos++) {}
				if (setBit(0, i) & uncount) 
					set_mask = setBit(set_mask, pos);
			}
			// check  pages and mask
			Bits curpageBit = q->start | set_mask;
			if (Page_num > curpageBit) {
				q->curpage = curpageBit;
				q->is_ovflow = 0;
				q->curtup = 0;
				q->curScanPage = 0;
				q->unbits = uncount;
			}
			else {	
				break;
			}
		}
	}	
	return NULL;
}

// clean up a QueryRep object and associated data

void closeQuery(Query q)
{
	free(q);
}
