#include "postgres.h"

#include "fmgr.h"  
#include "access/hash.h"
#include "utils/builtins.h"
#include <ctype.h>

PG_MODULE_MAGIC;

typedef struct PersonName {
    int32 length;
    char pname[FLEXIBLE_ARRAY_MEMBER];
} PersonName;


/*****************************************************************************
 * Input/Output functions
 *****************************************************************************/

static int check_valid_input(char* str) {
    int res = 0, comma = 0, idx = 0;
    int len = strlen(str);
    if (len == 0) return 0;
    if (!isupper(str[0])) return 0;

    for (int i = 0; i < len; i++) {
        //check other symbol besides letter, comma, - and '
        if (!isalpha(str[i]) && str[i] != ',' && !isspace(str[i]) && str[i] != '-' && (int)str[i] != 39) //acsii 39 is '
            return 0;
        //check tailing
        if (i+1 != len && str[i] == ' ' && str[i+1] == ' ') 
            return 0;
        
        //check initial 
        if (isupper(str[i])) {
            if (i == 0 && str[i+1] == ' ')
                return 0;
            if (i > 0 && str[i-1] == ' ' && i+1 == len)
                return 0;
            if (i+1 != len && (str[i-1] == ' ' || str[i-1] == ',') && (str[i+1] == ' ' || str[i+1] == ','))
                return 0;
        } 
        if (str[i] == ',') {
            //name must at least 2 letters, no space before comma
            if (i < 2 || (i > 0 && str[i-1] == ' '))  return 0; 
        
            idx = i+1;
            while (idx < len && str[idx] == ' ') {
                idx++;
            }
            //check only single place before , and it must be upper
            if (idx > i+2 || !isupper(str[idx])) {
                return 0;
            }
            comma++;
        }
    }
    if (comma == 1) res = 1;
    return res;
}

static char* get_given_name(char* name) {
    char* result;
    int res_len = 0, com_i = 0;
    for (int i = 0; i < strlen(name); i++) {
        if (name[i] == ',') {
            if (name[i+1] != ' ') {
                com_i = i;
            }
            else {
                com_i = i+1;
            }
            break;
        }
    }
    res_len = strlen(name)-com_i-1;
    result = (char*)palloc((res_len+1)*sizeof(char));
    strncpy(result, (name+com_i+1), res_len);
    result[res_len] = '\0';
    return result;
}

static char* get_family_name(char* name) {
    char* result;
    int com_i = 0;
    for (int i = 0; i < strlen(name); i++) {
        if (name[i] == ',') {
            com_i = i;
            break;
        }
    }
    result = (char*)palloc((com_i+1)*sizeof(char));
    strncpy(result, name, com_i);
    result[com_i] = '\0';
    return result;
}

static char* get_whole_name (char* name) {
    char* result;
    char* given;
    char* family;
    int g_len, f_len, res_len, termin_i;
    given = get_given_name(name);
    g_len = strlen(given);
    termin_i = g_len;
    for (int i = 0; i < g_len; i++) {
        if (given[i] == ' ') {
            termin_i = i;
            break;
        }
    }

    family = get_family_name(name);
    
    f_len = strlen(family);
    res_len = termin_i+f_len+1;

    result = (char*)palloc((res_len+1)*sizeof(char));
    strncpy(result, given, termin_i);
    result[termin_i] = ' ';
    strncpy((result+termin_i+1), family, f_len);
    
    result[res_len] = '\0';
    return result;
}


static char* p_out_name (char* in) {
    char* result;
    int i, j;
    int res_len;
    
    res_len = strlen(in);
    result = (char*)palloc((res_len+1)*sizeof(char));
    j = 0;
    for (i = 0; i < res_len; i++) {
        if (i > 0 && in[i] == ' ' && in[i-1] == ',') {
            continue;
        }
        result[j] = in[i];
        j++;
    }
    result[j] = '\0';
    return result;
}

PG_FUNCTION_INFO_V1(pname_in);

Datum
pname_in(PG_FUNCTION_ARGS) {
    char       *str = PG_GETARG_CSTRING(0);
    PersonName    *result;
    int length = 0;
    if (!check_valid_input(str))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                 errmsg("invalid input syntax for type %s: \"%s\"",
                        "PersonName", str)));
    length = strlen(str) + 1;

    result = (PersonName *) palloc(VARHDRSZ + length);
    SET_VARSIZE(result, VARHDRSZ + length);
    strcpy(result->pname, str);
    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(pname_out);

Datum
pname_out(PG_FUNCTION_ARGS) {
    PersonName    *personName = (PersonName *) PG_GETARG_POINTER(0);
    char* in;
    char* result;
    in = psprintf("%s", personName->pname);
    result = p_out_name(in);
    PG_RETURN_CSTRING(result);
}

static int pname_abs_cmp_internal(PersonName * a, PersonName * b) {
    char* a_name;
    char* b_name;
    char* aa;
    char* bb;
    aa = psprintf("%s", a->pname);
    bb = psprintf("%s", b->pname);

    a_name = p_out_name(aa);
    b_name = p_out_name(bb);

    return strcmp(a_name, b_name);
}


PG_FUNCTION_INFO_V1(pname_abs_lt);

Datum
pname_abs_lt(PG_FUNCTION_ARGS)
{
    PersonName    *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName    *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(pname_abs_cmp_internal(a, b) < 0);
}

PG_FUNCTION_INFO_V1(pname_abs_le);

Datum
pname_abs_le(PG_FUNCTION_ARGS) {
    PersonName    *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName    *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(pname_abs_cmp_internal(a, b) <= 0);
}

PG_FUNCTION_INFO_V1(pname_abs_eq);

Datum
pname_abs_eq(PG_FUNCTION_ARGS)
{
    PersonName    *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName    *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(pname_abs_cmp_internal(a, b) == 0);
}

PG_FUNCTION_INFO_V1(pname_abs_nq);

Datum
pname_abs_nq(PG_FUNCTION_ARGS) {
    PersonName    *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName    *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(pname_abs_cmp_internal(a, b) != 0);
}

PG_FUNCTION_INFO_V1(pname_abs_ge);

Datum
pname_abs_ge(PG_FUNCTION_ARGS) {
    PersonName    *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName    *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(pname_abs_cmp_internal(a, b) >= 0);
}

PG_FUNCTION_INFO_V1(pname_abs_gt);

Datum
pname_abs_gt(PG_FUNCTION_ARGS) {
    PersonName    *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName    *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(pname_abs_cmp_internal(a, b) > 0);
}

PG_FUNCTION_INFO_V1(pname_abs_cmp);

Datum
pname_abs_cmp(PG_FUNCTION_ARGS) {
	PersonName    *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName    *b = (PersonName *) PG_GETARG_POINTER(1);

	PG_RETURN_INT32(pname_abs_cmp_internal(a, b));
}

PG_FUNCTION_INFO_V1(family);

Datum
family(PG_FUNCTION_ARGS) {
    char* name;
    char* result;
    PersonName    *personName = (PersonName *) PG_GETARG_POINTER(0);
    name = psprintf("%s", personName->pname);
    result = get_family_name(name);
    PG_RETURN_TEXT_P(cstring_to_text(result));
}

PG_FUNCTION_INFO_V1(given);


Datum
given(PG_FUNCTION_ARGS) {
    char* name;
    char* result;
    PersonName   *personName = (PersonName *) PG_GETARG_POINTER(0);
    name = psprintf("%s", personName->pname);
    result = get_given_name(name);
    PG_RETURN_TEXT_P(cstring_to_text(result));
}

PG_FUNCTION_INFO_V1(show);


Datum
show(PG_FUNCTION_ARGS) {
    char* name;
    char* result;
    PersonName* personName = (PersonName*) PG_GETARG_POINTER(0);
    name = psprintf("%s", personName->pname);
    result = get_whole_name(name);
    PG_RETURN_TEXT_P(cstring_to_text(result));
}

PG_FUNCTION_INFO_V1(pname_hash);

Datum
pname_hash(PG_FUNCTION_ARGS) {
    int hash_code = 0;
    char* name;
    char* result;
    PersonName* personName = (PersonName*) PG_GETARG_POINTER(0);
    name = psprintf("%s", personName->pname);
    result = p_out_name(name);
    
    hash_code = DatumGetUInt32(hash_any((const unsigned char*) result, strlen(result)));
    PG_RETURN_INT32(hash_code);
}