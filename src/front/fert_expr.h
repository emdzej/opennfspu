/* FERT's expressions (0x52d910 .. 0x52f5a0): a control's update (FERT_Control_v14 0x524f70) defines
   every attribute's text as a symbol ("control.size.width", "label.pos.x", ...), then evaluates each
   attribute's text: its symbols expanded as text, recursively (0x52df30), the dots made 'X', and the
   result run through an embedded C interpreter (0x52f120 .. 0x532000: integer C expressions and the
   functions abs, sqrt, intsin, intcos and the is* / to* character ones; errors give 0).

   Both are ported (fert_expr.c): the interpreter is an operator-precedence evaluator that reads what
   it can (an empty operand is 0) and gives 0 only for an operator where none can be or brackets that
   don't match. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The symbol table emptied (0x52d910). */
void FertSyms_Clear(void);
/* name = text (0x52d930 -> 0x52dd80), replacing an earlier definition. */
void FertSyms_Define(const char *name, const char *text);
const char *FertSyms_Get(const char *name);
/* text with its symbols expanded (0x52e330 -> 0x52df30): identifiers (a letter, '_' or '.', then
   letters, digits, '_' and '.') replaced by their text, expanded in turn; the rest copied, but an exact
   "##" dropped. False (out empty) when a symbol refers back to itself. */
bool FertExpr_Expand(const char *text, char *out, int cap);
/* text expanded and evaluated (0x52d950); 0 on an error. */
int32_t FertExpr_Eval(const char *text);
